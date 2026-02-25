#include "BinaryObjectWindow.h"

#include <QByteArray>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStringBuilder>
#include <QVBoxLayout>

namespace {
constexpr int kBytesPerRow = 16;
}

BinaryObjectWindow::BinaryObjectWindow(const QString& varName, const QByteArray& data, QWidget* parent)
    : QWidget(parent), varName_(varName) {
  setWindowTitle(QString("Binary Object - %1").arg(varName_));
  resize(980, 540);

  auto* layout = new QVBoxLayout(this);
  nameLabel_ = new QLabel(QString("Name: %1 (%2 bytes)").arg(varName_).arg(data.size()), this);
  layout->addWidget(nameLabel_);

  dumpView_ = new QPlainTextEdit(this);

  const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  dumpView_->setFont(mono);
  dumpView_->setReadOnly(true);
  dumpView_->setLineWrapMode(QPlainTextEdit::NoWrap);
  dumpView_->setPlainText(combinedDump(data));
  layout->addWidget(dumpView_, 1);
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
