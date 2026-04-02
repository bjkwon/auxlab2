#include "BinaryObjectWindow.h"

#include <QByteArray>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStringBuilder>
#include <QVBoxLayout>

namespace {
constexpr int kBytesPerRow = 16;
}

BinaryObjectWindow::BinaryObjectWindow(const QString& varName, const QByteArray& data, QWidget* parent)
    : QWidget(parent), varName_(varName), data_(data) {
  setWindowTitle(QString("Binary Object - %1 (%2 bytes)").arg(varName_).arg(data_.size()));
  resize(980, 540);

  auto* layout = new QVBoxLayout(this);
  auto* contentLayout = new QHBoxLayout();
  layout->addLayout(contentLayout, 1);

  const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  lineNumberView_ = new QPlainTextEdit(this);
  lineNumberView_->setFont(mono);
  lineNumberView_->setReadOnly(true);
  lineNumberView_->setLineWrapMode(QPlainTextEdit::NoWrap);
  lineNumberView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  lineNumberView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  lineNumberView_->setFrameShape(QFrame::NoFrame);
  lineNumberView_->setFixedWidth(72);
  lineNumberView_->hide();
  contentLayout->addWidget(lineNumberView_);

  dumpView_ = new QPlainTextEdit(this);
  dumpView_->setFont(mono);
  dumpView_->setReadOnly(true);
  dumpView_->setLineWrapMode(QPlainTextEdit::NoWrap);
  dumpView_->setPlainText(combinedDump(data_));
  contentLayout->addWidget(dumpView_, 1);

  connect(dumpView_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int v) { lineNumberView_->verticalScrollBar()->setValue(v); });
  connect(lineNumberView_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int v) { dumpView_->verticalScrollBar()->setValue(v); });

  auto* returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
  returnShortcut->setContext(Qt::WindowShortcut);
  connect(returnShortcut, &QShortcut::activated, this, [this]() { toggleViewMode(); });

  auto* enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
  enterShortcut->setContext(Qt::WindowShortcut);
  connect(enterShortcut, &QShortcut::activated, this, [this]() { toggleViewMode(); });

  auto* tabShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), this);
  tabShortcut->setContext(Qt::WindowShortcut);
  connect(tabShortcut, &QShortcut::activated, this, [this]() { toggleRawWrap(); });
}

QString BinaryObjectWindow::varName() const {
  return varName_;
}

QString BinaryObjectWindow::combinedDump(const QByteArray& data) {
  QString out;
  for (int i = 0; i < data.size(); i += kBytesPerRow) {
    if (i > 0) {
      out += '\n';
    }
    out += QString("%1: ").arg(i, 8, 16, QChar('0')).toUpper();

    const int rowEnd = std::min(i + kBytesPerRow, static_cast<int>(data.size()));
    for (int j = i; j < rowEnd; ++j) {
      if (j > i) {
        out += ' ';
      }
      out += QString("%1").arg(static_cast<unsigned char>(data[j]), 2, 16, QChar('0')).toUpper();
    }

    if (rowEnd - i < kBytesPerRow) {
      const int pad = kBytesPerRow - (rowEnd - i);
      for (int k = 0; k < pad; ++k) {
        out += "   ";
      }
    }

    out += " | ";
    for (int j = i; j < rowEnd; ++j) {
      const unsigned char c = static_cast<unsigned char>(data[j]);
      if (c >= 32 && c <= 126) {
        out += QChar(c);
      } else {
        out += '.';
      }
    }
  }
  return out;
}

int BinaryObjectWindow::countRenderedLines(const QString& text) {
  if (text.isEmpty()) {
    return 0;
  }
  int breaks = 0;
  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text[i];
    if (ch == QChar('\r')) {
      ++breaks;
      if (i + 1 < text.size() && text[i + 1] == QChar('\n')) {
        ++i;
      }
    } else if (ch == QChar('\n')) {
      ++breaks;
    }
  }
  return breaks + 1;
}

QString BinaryObjectWindow::rawTextFromBytes(const QByteArray& data) {
  QString out;
  out.reserve(data.size());
  for (int i = 0; i < data.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c == '\r' || c == '\n' || c == '\t' || c >= 0x20) {
      out += QChar(c);
    }
    // Ignore all other control characters below 0x20.
  }
  return out;
}

QString BinaryObjectWindow::lineNumberText(int lineCount) {
  QString out;
  if (lineCount <= 0) {
    return out;
  }
  for (int i = 1; i <= lineCount; ++i) {
    out += QString::number(i);
    if (i < lineCount) {
      out += '\n';
    }
  }
  return out;
}

void BinaryObjectWindow::setHexView() {
  rawTextMode_ = false;
  setWindowTitle(QString("Binary Object - %1 (%2 bytes)").arg(varName_).arg(data_.size()));
  lineNumberView_->hide();
  dumpView_->setLineWrapMode(QPlainTextEdit::NoWrap);
  dumpView_->setPlainText(combinedDump(data_));
}

void BinaryObjectWindow::setRawTextView() {
  rawTextMode_ = true;
  rawText_ = rawTextFromBytes(data_);
  rawLineCount_ = countRenderedLines(rawText_);
  setWindowTitle(QString("Raw Text View - %1 (%2 bytes, %3 lines)")
                     .arg(varName_)
                     .arg(data_.size())
                     .arg(rawLineCount_));
  lineNumberView_->setPlainText(lineNumberText(rawLineCount_));
  lineNumberView_->show();
  dumpView_->setLineWrapMode(rawWrapEnabled_ ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
  dumpView_->setPlainText(rawText_);
  lineNumberView_->verticalScrollBar()->setValue(dumpView_->verticalScrollBar()->value());
}

void BinaryObjectWindow::toggleViewMode() {
  if (rawTextMode_) {
    setHexView();
  } else {
    setRawTextView();
  }
}

void BinaryObjectWindow::toggleRawWrap() {
  if (!rawTextMode_ || !isActiveWindow()) {
    return;
  }
  rawWrapEnabled_ = !rawWrapEnabled_;
  dumpView_->setLineWrapMode(rawWrapEnabled_ ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}
