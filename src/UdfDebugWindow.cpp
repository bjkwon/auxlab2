#include "UdfDebugWindow.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

UdfDebugWindow::UdfDebugWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("UDF Debug Window");
  resize(780, 540);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  statusLabel_ = new QLabel("Debug state: idle", this);
  locationLabel_ = new QLabel("Location: -", this);

  layout->addWidget(statusLabel_);
  layout->addWidget(locationLabel_);

  editor_ = new QPlainTextEdit(this);
  layout->addWidget(editor_, 1);

  auto* buttons = new QHBoxLayout();
  stepBtn_ = new QPushButton("Step Over", this);
  stepInBtn_ = new QPushButton("Step In", this);
  stepOutBtn_ = new QPushButton("Step Out", this);
  continueBtn_ = new QPushButton("Continue", this);
  abortBtn_ = new QPushButton("Abort", this);

  buttons->addWidget(stepBtn_);
  buttons->addWidget(stepInBtn_);
  buttons->addWidget(stepOutBtn_);
  buttons->addWidget(continueBtn_);
  buttons->addWidget(abortBtn_);
  layout->addLayout(buttons);

  setCentralWidget(central);

  connect(stepBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepOver);
  connect(stepInBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepIn);
  connect(stepOutBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugStepOut);
  connect(continueBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugContinue);
  connect(abortBtn_, &QPushButton::clicked, this, &UdfDebugWindow::debugAbort);

  setPaused(false);
}

void UdfDebugWindow::setPauseLocation(const QString& filePath, int lineNumber) {
  locationLabel_->setText(QString("Location: %1:%2").arg(filePath).arg(lineNumber));

  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }

  QTextStream ts(&f);
  const QString all = ts.readAll();
  editor_->setPlainText(all);

  QTextCursor cursor(editor_->document());
  cursor.movePosition(QTextCursor::Start);
  for (int i = 1; i < lineNumber; ++i) {
    if (!cursor.movePosition(QTextCursor::NextBlock)) {
      break;
    }
  }
  editor_->setTextCursor(cursor);
  editor_->centerCursor();
}

void UdfDebugWindow::setPaused(bool paused) {
  statusLabel_->setText(paused ? "Debug state: paused" : "Debug state: idle/running");
  stepBtn_->setEnabled(paused);
  stepInBtn_->setEnabled(paused);
  stepOutBtn_->setEnabled(paused);
  continueBtn_->setEnabled(paused);
  abortBtn_->setEnabled(paused);
}
