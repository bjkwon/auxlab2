#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;

class BinaryObjectWindow : public QWidget {
  Q_OBJECT
public:
  BinaryObjectWindow(const QString& varName, const QByteArray& data, QWidget* parent = nullptr);
  QString varName() const;

private:
  static QString combinedDump(const QByteArray& data);

  QString varName_;
  QLabel* nameLabel_ = nullptr;
  QPlainTextEdit* dumpView_ = nullptr;
};
