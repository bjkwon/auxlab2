#include "UdfDebugWindow.h"
#include "DebugCodeEditor.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

UdfDebugWindow::UdfDebugWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("UDF Debug Window");
  resize(920, 620);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  statusLabel_ = new QLabel("Debug state: idle", this);
  locationLabel_ = new QLabel("Location: -", this);

  layout->addWidget(statusLabel_);
  layout->addWidget(locationLabel_);

  tabs_ = new QTabWidget(this);
  tabs_->setTabsClosable(true);
  tabs_->setDocumentMode(true);
  layout->addWidget(tabs_, 1);

  auto* buttons = new QHBoxLayout();
  saveBtn_ = new QPushButton("Save", this);
  saveBtn_->setShortcut(QKeySequence::Save);
  stepBtn_ = new QPushButton("Step Over", this);
  stepInBtn_ = new QPushButton("Step In", this);
  stepOutBtn_ = new QPushButton("Step Out", this);
  continueBtn_ = new QPushButton("Continue", this);
  abortBtn_ = new QPushButton("Abort", this);

  buttons->addWidget(saveBtn_);
  buttons->addStretch(1);
  buttons->addWidget(stepBtn_);
  buttons->addWidget(stepInBtn_);
  buttons->addWidget(stepOutBtn_);
  buttons->addWidget(continueBtn_);
  buttons->addWidget(abortBtn_);
  layout->addLayout(buttons);

  setCentralWidget(central);

  connect(saveBtn_, &QPushButton::clicked, this, [this]() {
    saveTab(tabs_->currentIndex());
  });
  connect(stepBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepOver);
  connect(stepInBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepIn);
  connect(stepOutBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepOut);
  connect(continueBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugContinue);
  connect(abortBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugAbort);

  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    refreshLocationLabel();
    refreshSelections();
    updateSaveEnabled();
  });

  connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
    if (!maybeSaveTab(index)) {
      return;
    }
    QWidget* w = tabs_->widget(index);
    tabs_->removeTab(index);
    delete w;
    refreshLocationLabel();
    updateSaveEnabled();
  });

  setPaused(false);
  updateSaveEnabled();
}

int UdfDebugWindow::findTabByPath(const QString& filePath) const {
  for (int i = 0; i < tabs_->count(); ++i) {
    if (tabs_->tabToolTip(i) == filePath) {
      return i;
    }
  }
  return -1;
}

DebugCodeEditor* UdfDebugWindow::currentEditor() const {
  return qobject_cast<DebugCodeEditor*>(tabs_->currentWidget());
}

bool UdfDebugWindow::loadEditorFromFile(DebugCodeEditor* editor, const QString& filePath) {
  if (!editor) {
    return false;
  }

  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    editor->clear();
    editor->document()->setModified(false);
    return false;
  }

  QTextStream ts(&f);
  const QString all = ts.readAll();
  editor->setPlainText(all);
  editor->document()->setModified(false);
  return true;
}

bool UdfDebugWindow::ensureTab(const QString& filePath, bool activate) {
  if (filePath.isEmpty()) {
    return false;
  }

  int index = findTabByPath(filePath);
  if (index < 0) {
    auto* editor = new DebugCodeEditor(tabs_);
    index = tabs_->addTab(editor, QFileInfo(filePath).fileName());
    tabs_->setTabToolTip(index, filePath);

    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
      if (editor == currentEditor()) {
        refreshLocationLabel();
      }
    });
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor](bool) {
      const int tabIndex = tabs_->indexOf(editor);
      if (tabIndex >= 0) {
        updateTabTitle(tabIndex);
      }
      if (editor == currentEditor()) {
        updateSaveEnabled();
      }
    });

    loadEditorFromFile(editor, filePath);
    updateTabTitle(index);
  } else {
    auto* editor = qobject_cast<DebugCodeEditor*>(tabs_->widget(index));
    if (editor && !editor->document()->isModified()) {
      loadEditorFromFile(editor, filePath);
      updateTabTitle(index);
    }
  }

  if (activate) {
    tabs_->setCurrentIndex(index);
  }
  return true;
}

void UdfDebugWindow::setFile(const QString& filePath) {
  if (filePath.isEmpty()) {
    pausedLine_ = -1;
    refreshLocationLabel();
    refreshSelections();
    updateSaveEnabled();
    return;
  }

  if (ensureTab(filePath, true)) {
    pausedLine_ = -1;
    refreshLocationLabel();
    refreshSelections();
    updateSaveEnabled();
  }
}

void UdfDebugWindow::closeFile(const QString& filePath) {
  if (filePath.isEmpty()) {
    return;
  }

  const int index = findTabByPath(filePath);
  if (index < 0) {
    return;
  }

  if (!maybeSaveTab(index)) {
    return;
  }

  QWidget* w = tabs_->widget(index);
  tabs_->removeTab(index);
  delete w;

  breakpointsByFile_.remove(filePath);
  if (currentFilePath() == filePath) {
    pausedLine_ = -1;
  }

  refreshLocationLabel();
  refreshSelections();
  updateSaveEnabled();
}

QString UdfDebugWindow::currentFilePath() const {
  const int index = tabs_->currentIndex();
  if (index < 0) {
    return {};
  }
  return tabs_->tabToolTip(index);
}

void UdfDebugWindow::setPauseLocation(const QString& filePath, int lineNumber) {
  if (!ensureTab(filePath, true)) {
    return;
  }

  pausedLine_ = lineNumber;

  if (auto* editor = currentEditor()) {
    QTextCursor cursor(editor->document());
    cursor.movePosition(QTextCursor::Start);
    for (int i = 1; i < lineNumber; ++i) {
      if (!cursor.movePosition(QTextCursor::NextBlock)) {
        break;
      }
    }
    editor->setTextCursor(cursor);
    editor->centerCursor();
  }

  refreshLocationLabel();
  refreshSelections();
}

void UdfDebugWindow::setPaused(bool paused) {
  statusLabel_->setText(paused ? "Debug state: paused" : "Debug state: idle/running");
  stepBtn_->setEnabled(paused);
  stepInBtn_->setEnabled(paused);
  stepOutBtn_->setEnabled(paused);
  continueBtn_->setEnabled(paused);
  abortBtn_->setEnabled(paused);
  if (!paused) {
    pausedLine_ = -1;
    refreshSelections();
  }
}

int UdfDebugWindow::cursorLine() const {
  const auto* editor = currentEditor();
  if (!editor) {
    return -1;
  }
  return editor->textCursor().blockNumber() + 1;
}

bool UdfDebugWindow::hasBreakpoint(int lineNumber) const {
  const auto it = breakpointsByFile_.find(currentFilePath());
  if (it == breakpointsByFile_.end()) {
    return false;
  }
  return it.value().contains(lineNumber);
}

void UdfDebugWindow::setBreakpoints(const QSet<int>& lines) {
  setBreakpointsForFile(currentFilePath(), lines);
}

void UdfDebugWindow::setBreakpointsForFile(const QString& filePath, const QSet<int>& lines) {
  if (filePath.isEmpty()) {
    return;
  }
  breakpointsByFile_.insert(filePath, lines);
  refreshSelections();
}

void UdfDebugWindow::toggleBreakpointAtCursor() {
  const int line = cursorLine();
  if (line <= 0) {
    return;
  }
  const bool enable = !hasBreakpoint(line);
  emit breakpointToggleRequested(line, enable);
}

void UdfDebugWindow::keyPressEvent(QKeyEvent* event) {
  if (event->matches(QKeySequence::Save)) {
    saveTab(tabs_->currentIndex());
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_F9) {
    toggleBreakpointAtCursor();
    event->accept();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

bool UdfDebugWindow::saveTab(int index) {
  if (index < 0 || index >= tabs_->count()) {
    return false;
  }

  const QString filePath = tabs_->tabToolTip(index);
  auto* editor = qobject_cast<DebugCodeEditor*>(tabs_->widget(index));
  if (!editor || filePath.isEmpty()) {
    return false;
  }

  QFile f(filePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::warning(this, "Save", QString("Failed to save file:\n%1").arg(filePath));
    return false;
  }

  QTextStream ts(&f);
  ts << editor->toPlainText();
  editor->document()->setModified(false);
  updateTabTitle(index);
  updateSaveEnabled();
  statusBar()->showMessage(QString("Saved %1").arg(QFileInfo(filePath).fileName()), 1800);
  return true;
}

bool UdfDebugWindow::maybeSaveTab(int index) {
  if (index < 0 || index >= tabs_->count()) {
    return true;
  }

  auto* editor = qobject_cast<DebugCodeEditor*>(tabs_->widget(index));
  if (!editor || !editor->document()->isModified()) {
    return true;
  }

  const QString filePath = tabs_->tabToolTip(index);
  const QString title = QFileInfo(filePath).fileName();
  const auto choice = QMessageBox::question(
      this,
      "Unsaved Changes",
      QString("Save changes to %1 before closing?").arg(title),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return saveTab(index);
  }
  return true;
}

void UdfDebugWindow::refreshLocationLabel() {
  const QString filePath = currentFilePath();
  if (filePath.isEmpty()) {
    locationLabel_->setText("Location: -");
    return;
  }

  const int line = cursorLine();
  if (line > 0) {
    locationLabel_->setText(QString("Location: %1:%2").arg(filePath).arg(line));
  } else {
    locationLabel_->setText(QString("Location: %1").arg(filePath));
  }
}

void UdfDebugWindow::refreshSelections(DebugCodeEditor* editor, const QString& filePath) {
  if (!editor) {
    return;
  }

  const QSet<int> fileBreakpoints = breakpointsByFile_.value(filePath);
  editor->setBreakpointLines(fileBreakpoints);

  QList<QTextEdit::ExtraSelection> sels;

  for (int line : fileBreakpoints) {
    if (line <= 0) {
      continue;
    }
    QTextCursor c(editor->document());
    c.movePosition(QTextCursor::Start);
    for (int i = 1; i < line; ++i) {
      if (!c.movePosition(QTextCursor::NextBlock)) {
        break;
      }
    }
    QTextEdit::ExtraSelection sel;
    sel.cursor = c;
    sel.format.setBackground(QColor(120, 32, 32, 120));
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sels.push_back(sel);
  }

  if (pausedLine_ > 0 && filePath == currentFilePath()) {
    QTextCursor c(editor->document());
    c.movePosition(QTextCursor::Start);
    for (int i = 1; i < pausedLine_; ++i) {
      if (!c.movePosition(QTextCursor::NextBlock)) {
        break;
      }
    }
    QTextEdit::ExtraSelection sel;
    sel.cursor = c;
    sel.format.setBackground(QColor(210, 180, 60, 120));
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sels.push_back(sel);
  }

  editor->setExtraSelections(sels);
}

void UdfDebugWindow::refreshSelections() {
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* editor = qobject_cast<DebugCodeEditor*>(tabs_->widget(i));
    refreshSelections(editor, tabs_->tabToolTip(i));
  }
}

void UdfDebugWindow::updateSaveEnabled() {
  auto* editor = currentEditor();
  saveBtn_->setEnabled(editor && editor->document()->isModified());
}

void UdfDebugWindow::updateTabTitle(int index) {
  if (index < 0 || index >= tabs_->count()) {
    return;
  }
  auto* editor = qobject_cast<DebugCodeEditor*>(tabs_->widget(index));
  const QString filePath = tabs_->tabToolTip(index);
  QString title = QFileInfo(filePath).fileName();
  if (title.isEmpty()) {
    title = "(untitled)";
  }
  if (editor && editor->document()->isModified()) {
    title += "*";
  }
  tabs_->setTabText(index, title);
}
