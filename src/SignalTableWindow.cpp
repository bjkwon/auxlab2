#include "SignalTableWindow.h"

#include <QCloseEvent>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QMouseEvent>
#include <QSpinBox>
#include <QKeyEvent>
#include <QAbstractTableModel>
#include <QHash>
#include <QHBoxLayout>
#include <QIdentityProxyModel>
#include <QScreen>
#include <QProxyStyle>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
constexpr int kMatrixIndexColumnWidth = 72;
constexpr int kMatrixValueColumnWidth = 88;
// Deliberately process-local: reopening a variable restores its size and position.
struct SignalTableGeometry {
  QByteArray geometry;
  int valueColumnWidth;
};
QHash<QString, SignalTableGeometry> signalTableGeometries;

// Keep scrollbar tracks available for mouse dragging even when macOS uses
// transient overlay scrollbars. Scope this policy to signal tables only.
class SignalTableStyle : public QProxyStyle {
public:
  int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr, QStyleHintReturn* result = nullptr) const override {
    if (hint == QStyle::SH_ScrollBar_Transient) return 0;
    return QProxyStyle::styleHint(hint, option, widget, result);
  }
};

// A native header stays pinned vertically; provide an index editor on demand
// instead of adding another scrolling row to the matrix data.
class EditableIndexHeader : public QHeaderView {
public:
  explicit EditableIndexHeader(QTableView* table)
      : QHeaderView(Qt::Horizontal, table), table_(table), editor_(new QSpinBox(viewport())) {
    editor_->hide();
    editor_->setAlignment(Qt::AlignCenter);
    editor_->setKeyboardTracking(false);
    editor_->installEventFilter(this);
    connect(editor_, &QSpinBox::editingFinished, this, [this]() {
      if (!editing_) return;
      const int column = editor_->value();
      cancelEditing();
      if (!model() || column >= model()->columnCount() || model()->rowCount() == 0) return;
      const int verticalPosition = verticalPosition_;
      const int row = std::max(0, table_->rowAt(0));
      const auto target = model()->index(row, column);
      table_->scrollTo(target, QAbstractItemView::PositionAtCenter);
      table_->setCurrentIndex(target);
      table_->setFocus();
      table_->verticalScrollBar()->setValue(verticalPosition);
      QTimer::singleShot(0, table_, [table = table_, verticalPosition]() {
        table->verticalScrollBar()->setValue(verticalPosition);
      });
    });
    connect(this, &QHeaderView::geometriesChanged, this, [this]() { cancelEditing(); });
    connect(this, &QHeaderView::sectionResized, this, [this]() { cancelEditing(); });
    connect(table->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { cancelEditing(); });
  }
protected:
  void mousePressEvent(QMouseEvent* event) override {
    const int column = logicalIndexAt(event->position().toPoint());
    const int x = event->position().toPoint().x();
    if (column > 0 && x > sectionViewportPosition(column) + 4 &&
        x < sectionViewportPosition(column) + sectionSize(column) - 4) {
      // A header click starts navigation, not whole-column selection (which
      // would scroll the matrix back to its first row before editing).
      event->accept();
      return;
    }
    QHeaderView::mousePressEvent(event);
  }
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    const int column = logicalIndexAt(event->position().toPoint());
    if (event->button() != Qt::LeftButton || column <= 0 || !model()) {
      QHeaderView::mouseDoubleClickEvent(event);
      return;
    }
    verticalPosition_ = table_->verticalScrollBar()->value();
    editor_->setRange(1, model()->columnCount() - 1);
    editor_->setValue(column);
    editor_->setGeometry(sectionViewportPosition(column), 0, sectionSize(column), viewport()->height());
    editing_ = true;
    editor_->show();
    editor_->setFocus();
    editor_->selectAll();
    event->accept();
  }
  bool eventFilter(QObject* object, QEvent* event) override {
    if (object == editor_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      cancelEditing();
      table_->setFocus();
      return true;
    }
    return QHeaderView::eventFilter(object, event);
  }
private:
  void cancelEditing() {
    editing_ = false;
    editor_->hide();
  }
  QTableView* table_;
  QSpinBox* editor_;
  bool editing_ = false;
  int verticalPosition_ = 0;
};

// Expose only the label column to the stationary view, without hiding millions
// of audio columns or allocating a second set of table data.
class IndexColumnModel : public QIdentityProxyModel {
public:
  using QIdentityProxyModel::QIdentityProxyModel;
  int columnCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() || !sourceModel() || sourceModel()->columnCount() == 0 ? 0 : 1;
  }
};

// Values are fetched only for visible cells, so long audio signals can expose
// every sample without allocating millions of items or index editors.
class SignalTableModel : public QAbstractTableModel {
public:
  SignalTableModel(const SignalData& data, bool matrix, QObject* parent)
      : QAbstractTableModel(parent), data_(data), matrix_(matrix) {}

  int rowCount(const QModelIndex& parent = {}) const override {
    if (parent.isValid() || data_.channels.empty()) return 0;
    return matrix_ ? static_cast<int>(data_.matrixRows)
                   : static_cast<int>(data_.channels.size()) * (data_.isComplex ? 2 : 1) + 1;
  }
  int columnCount(const QModelIndex& parent = {}) const override {
    if (parent.isValid() || data_.channels.empty()) return 0;
    size_t count = matrix_ ? data_.matrixCols : 0;
    if (!matrix_) {
      for (const auto& channel : data_.channels) count = std::max(count, channel.samples.size());
    }
    return static_cast<int>(std::min(count, static_cast<size_t>(std::numeric_limits<int>::max() - 1))) + 1;
  }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return section == 0 ? QVariant(matrix_ ? "Index" : "Channel") : QVariant(section);
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid()) return {};
    if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
    const bool heading = index.column() == 0 || (!matrix_ && index.row() == 0);
    if (heading && role == Qt::BackgroundRole) return QBrush(QColor("#484848"));
    if (heading && role == Qt::ForegroundRole) return QBrush(QColor("#ffffff"));
    if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole) return {};
    const int row = index.row();
    const int col = index.column();
    if (col == 0) {
      if (matrix_) return row + 1;
      if (row == 0) return QStringLiteral("Index");
      const int channel = data_.isComplex ? (row - 1) / 2 : row - 1;
      QString label = QString("Ch%1").arg(channel + 1);
      if (data_.isComplex) label += (row - 1) % 2 ? " Imag" : " Real";
      return label;
    }
    if (!matrix_ && row == 0) return col;
    const int channel = matrix_ ? 0 : (data_.isComplex ? (row - 1) / 2 : row - 1);
    const size_t sample = matrix_ ? static_cast<size_t>(row) * data_.matrixCols + col - 1 : col - 1;
    const auto& ch = data_.channels[channel];
    if (sample >= ch.samples.size()) return {};
    const double real = ch.samples[sample];
    const double imag = sample < ch.imagSamples.size() ? ch.imagSamples[sample] : 0.0;
    if (matrix_ && data_.isComplex && imag != 0.0) {
      return QStringLiteral("%1%2%3i").arg(QString::number(real, 'g', 8),
          imag < 0 ? " - " : " + ", QString::number(std::abs(imag), 'g', 8));
    }
    return QString::number(!matrix_ && data_.isComplex && (row - 1) % 2 ? imag : real, 'g', 8);
  }
  Qt::ItemFlags flags(const QModelIndex& index) const override {
    auto flags = QAbstractTableModel::flags(index);
    if ((matrix_ && index.column() == 0) || (!matrix_ && index.row() == 0 && index.column() > 0)) {
      flags |= Qt::ItemIsEditable;
    }
    return flags;
  }
  bool setData(const QModelIndex& index, const QVariant& value, int role) override {
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable)) return false;
    bool ok = false;
    const qulonglong requested = value.toULongLong(&ok);
    if (!ok || requested == 0) return false;
    const bool across = !matrix_ || data_.matrixRows == 1;
    const int limit = across ? columnCount() - 1 : rowCount();
    if (limit <= 0) return false;
    const int target = static_cast<int>(std::min<qulonglong>(requested, limit));
    if (navigate) navigate(this->index(across ? 0 : target - 1, across ? target : 0));
    return true;
  }
  std::function<void(const QModelIndex&)> navigate;
private:
  const SignalData& data_;
  bool matrix_;
};

}

SignalTableWindow::SignalTableWindow(const QString& varName, const SignalData& data, int displayLimitX, int displayLimitY,
                                     QWidget* parent)
    : QWidget(parent),
      varName_(varName),
      data_(data),
      displayLimitX_(std::max(1, displayLimitX)),
      displayLimitY_(std::max(1, displayLimitY)) {
  setWindowTitle(QString("Signal Table - %1").arg(varName_));

  auto* layout = new QVBoxLayout(this);

  table_ = new QTableView(this);
  table_->setHorizontalHeader(new EditableIndexHeader(table_));
  table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStyleSheet(
      "QHeaderView::section { background-color: #484848; color: #ffffff; }");
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table_->horizontalHeader()->setDefaultSectionSize(kMatrixValueColumnWidth);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
  auto* tables = new QHBoxLayout();
  tables->setSpacing(0);
  indexTable_ = new QTableView(this);
  auto* tableStyle = new SignalTableStyle();
  tableStyle->setParent(this);
  for (auto* view : {table_, indexTable_}) {
    view->setStyle(tableStyle);
    view->horizontalScrollBar()->setStyle(tableStyle);
    view->verticalScrollBar()->setStyle(tableStyle);
  }
  indexTable_->setObjectName("signalTableIndex");
  table_->setObjectName("signalTableData");
  indexTable_->setFixedWidth(kMatrixIndexColumnWidth + 2 * indexTable_->frameWidth());
  indexTable_->verticalHeader()->hide();
  indexTable_->horizontalHeader()->setStyleSheet(table_->horizontalHeader()->styleSheet());
  indexTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  indexTable_->horizontalHeader()->setDefaultSectionSize(kMatrixIndexColumnWidth);
  indexTable_->setEditTriggers(table_->editTriggers());
  indexTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  indexTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // Reserve the same bottom space as the data scrollbar, keeping rows aligned.
  indexTable_->horizontalScrollBar()->setEnabled(false);
  connect(table_->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this](int minimum, int maximum) {
    indexTable_->setHorizontalScrollBarPolicy(maximum > minimum ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
  });
  connect(table_->verticalScrollBar(), &QScrollBar::valueChanged,
          indexTable_->verticalScrollBar(), &QScrollBar::setValue);
  connect(indexTable_->verticalScrollBar(), &QScrollBar::valueChanged,
          table_->verticalScrollBar(), &QScrollBar::setValue);
  connect(table_->verticalHeader(), &QHeaderView::sectionResized, this, [this](int row, int, int height) {
    indexTable_->setRowHeight(row, height);
  });
  connect(indexTable_->verticalHeader(), &QHeaderView::sectionResized, this, [this](int row, int, int height) {
    table_->setRowHeight(row, height);
  });
  tables->addWidget(indexTable_);
  tables->addWidget(table_, 1);
  layout->addLayout(tables);

  fillTable();
  if (signalTableGeometries.contains(varName_)) {
    const auto saved = signalTableGeometries.value(varName_);
    table_->horizontalHeader()->setDefaultSectionSize(saved.valueColumnWidth);
    restoreGeometry(saved.geometry);
    didInitialAutoSize_ = true;
  } else {
    autoSizeForData();
  }
}

QString SignalTableWindow::varName() const {
  return varName_;
}

void SignalTableWindow::updateData(const SignalData& data) {
  data_ = data;
  fillTable();
}

void SignalTableWindow::closeEvent(QCloseEvent* event) {
  // An untouched auto-sized window must use the current limits next time.
  // Once adjusted, its saved geometry takes precedence over those limits.
  if (capturedInitialGeometry_ && geometry() != initialGeometry_) {
    signalTableGeometries.insert(varName_,
        {saveGeometry(), table_->horizontalHeader()->defaultSectionSize()});
  }
  QWidget::closeEvent(event);
}

void SignalTableWindow::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (!capturedInitialGeometry_) {
    // Let the window manager finish its initial placement before comparing
    // subsequent user moves/resizes with the initial geometry.
    QTimer::singleShot(0, this, [this]() {
      if (!capturedInitialGeometry_) {
        initialGeometry_ = geometry();
        capturedInitialGeometry_ = true;
      }
    });
  }
}

void SignalTableWindow::keyPressEvent(QKeyEvent* event) {
  const bool closeShortcut =
#ifdef Q_OS_MAC
      ((event->modifiers() & Qt::MetaModifier) && event->key() == Qt::Key_W);
#else
      ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_W);
#endif
  if (closeShortcut) {
    close();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

size_t SignalTableWindow::maxSignalLength() const {
  size_t maxLen = 0;
  for (const auto& ch : data_.channels) {
    maxLen = std::max(maxLen, ch.samples.size());
  }
  return maxLen;
}

bool SignalTableWindow::hasMatrixShape() const {
  return !data_.isAudio && data_.channels.size() == 1 && data_.matrixRows > 0 && data_.matrixCols > 0 &&
         data_.matrixRows <= data_.channels.front().samples.size() / data_.matrixCols;
}

void SignalTableWindow::autoSizeForData() {
  if (didInitialAutoSize_ || !table_) {
    return;
  }
  didInitialAutoSize_ = true;
  const size_t cols = hasMatrixShape() ? data_.matrixCols : maxSignalLength();
  const size_t rows = hasMatrixShape() ? data_.matrixRows : data_.channels.size() * (data_.isComplex ? 2 : 1) + 1;
  const int visibleCols = static_cast<int>(std::min<size_t>(cols, displayLimitX_));
  const int visibleRows = static_cast<int>(std::min<size_t>(rows, displayLimitY_));
  const QMargins margins = layout()->contentsMargins();
  const int horizontalOverhead = margins.left() + margins.right() + 2 * table_->frameWidth() +
      indexTable_->width() +
      (rows > static_cast<size_t>(visibleRows) ? table_->verticalScrollBar()->sizeHint().width() : 0);
  // Fit the requested number of columns, rather than clipping an 88-pixel
  // column layout to the screen and silently displaying fewer samples.
  const int availableWidth = screen()->availableGeometry().width();
  const int valueWidth = std::max(table_->horizontalHeader()->minimumSectionSize(),
      std::min(kMatrixValueColumnWidth, (availableWidth - horizontalOverhead) / std::max(1, visibleCols)));
  table_->horizontalHeader()->setDefaultSectionSize(valueWidth);
  const int desiredWidth = horizontalOverhead + visibleCols * valueWidth;
  const int desiredHeight = margins.top() + margins.bottom() + 2 * table_->frameWidth() +
      (hasMatrixShape() ? table_->horizontalHeader()->sizeHint().height() : 0) +
      visibleRows * table_->verticalHeader()->defaultSectionSize() +
      (cols > static_cast<size_t>(visibleCols) ? table_->horizontalScrollBar()->sizeHint().height() : 0);
  resize(QSize(desiredWidth, desiredHeight).boundedTo(screen()->availableGeometry().size()));
}

void SignalTableWindow::fillTable() {
  const int horizontalPosition = table_->horizontalScrollBar()->value();
  const int verticalPosition = table_->verticalScrollBar()->value();
  auto* oldModel = table_->model();
  auto* model = new SignalTableModel(data_, hasMatrixShape(), table_);
  model->navigate = [this](const QModelIndex& target) {
    if (target.column() == 0) {
      const auto index = indexTable_->model()->index(target.row(), 0);
      indexTable_->scrollTo(index, QAbstractItemView::PositionAtCenter);
      indexTable_->setCurrentIndex(index);
    } else {
      table_->scrollTo(target, QAbstractItemView::PositionAtCenter);
      table_->setCurrentIndex(target);
    }
  };
  auto* oldIndexModel = indexTable_->model();
  auto* indexModel = new IndexColumnModel(indexTable_);
  indexModel->setSourceModel(model);
  indexTable_->setModel(indexModel);
  table_->setModel(model);
  table_->setColumnHidden(0, true);
  // Audio already has an editable index row; only matrices need the header.
  table_->horizontalHeader()->setVisible(hasMatrixShape());
  indexTable_->horizontalHeader()->setVisible(hasMatrixShape());
  delete oldIndexModel;
  delete oldModel;
  table_->horizontalScrollBar()->setValue(horizontalPosition);
  table_->verticalScrollBar()->setValue(verticalPosition);
}
