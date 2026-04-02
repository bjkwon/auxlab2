#pragma once

#include <QWidget>

class QPlainTextEdit;

class BinaryObjectWindow : public QWidget {
  Q_OBJECT
public:
  BinaryObjectWindow(const QString& varName, const QByteArray& data, QWidget* parent = nullptr);
  QString varName() const;

private:
  static QString combinedDump(const QByteArray& data);
  static QString rawTextFromBytes(const QByteArray& data);
  static int countRenderedLines(const QString& text);
  static QString lineNumberText(int lineCount);
  void setHexView();
  void setRawTextView();
  void toggleViewMode();
  void toggleRawWrap();

  QString varName_;
  QByteArray data_;
  QString rawText_;
  int rawLineCount_ = 0;
  bool rawTextMode_ = false;
  bool rawWrapEnabled_ = false;
  QPlainTextEdit* lineNumberView_ = nullptr;
  QPlainTextEdit* dumpView_ = nullptr;
};
