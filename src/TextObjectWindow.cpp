#include "TextObjectWindow.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

TextObjectWindow::TextObjectWindow(const QString& varName, const QString& text, QWidget* parent)
    : QWidget(parent), varName_(varName) {
  setWindowTitle(QString("Text Object - %1").arg(varName_));
  resize(700, 420);

  auto* layout = new QVBoxLayout(this);

  nameLabel_ = new QLabel(QString("Name: %1").arg(varName_), this);
  layout->addWidget(nameLabel_);

  textView_ = new QPlainTextEdit(this);
  textView_->setReadOnly(true);
  textView_->setPlainText(text);
  layout->addWidget(textView_, 1);
}

QString TextObjectWindow::varName() const {
  return varName_;
}
