#include "CellMembersWindow.h"

#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kPathRole = Qt::UserRole + 100;
}

CellMembersWindow::CellMembersWindow(const QString& cellPath, const std::vector<VarSnapshot>& members, QWidget* parent)
    : QWidget(parent), cellPath_(cellPath) {
  setWindowTitle(QString("Cell Members - %1").arg(cellPath_));
  resize(760, 460);

  auto* layout = new QVBoxLayout(this);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({"Index", "Type/dbRMS", "Size", "Content"});
  tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
  tree_->installEventFilter(this);
  layout->addWidget(tree_);

  for (const auto& m : members) {
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(0, QString::fromStdString(m.name));
    item->setText(1, m.isAudio ? QString::fromStdString(m.rms) : QString::fromStdString(m.typeTag));
    item->setText(2, QString::fromStdString(m.size));
    item->setText(3, QString::fromStdString(m.preview));
    item->setData(0, kPathRole, QString("%1{%2}").arg(cellPath_, QString::fromStdString(m.name)));
  }

  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) {
    const auto path = selectedFullPath();
    if (!path.isEmpty()) {
      emit requestOpenDetail(path);
    }
  });
}

QString CellMembersWindow::cellPath() const {
  return cellPath_;
}

bool CellMembersWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == tree_ && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const auto path = selectedFullPath();
    if (path.isEmpty()) {
      return QWidget::eventFilter(watched, event);
    }

    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      emit requestOpenGraph(path);
      return true;
    }
    if (ke->key() == Qt::Key_Space) {
      emit requestPlayAudio(path);
      return true;
    }
  }

  return QWidget::eventFilter(watched, event);
}

QString CellMembersWindow::selectedFullPath() const {
  auto* item = tree_->currentItem();
  if (!item) {
    return {};
  }
  return item->data(0, kPathRole).toString();
}
