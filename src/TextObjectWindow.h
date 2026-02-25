#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;

class TextObjectWindow : public QWidget {
  Q_OBJECT
public:
  TextObjectWindow(const QString& varName, const QString& text, QWidget* parent = nullptr);
  QString varName() const;

private:
  QString varName_;
  QLabel* nameLabel_ = nullptr;
  QPlainTextEdit* textView_ = nullptr;
};
