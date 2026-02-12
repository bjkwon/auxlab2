#pragma once

#include <QMainWindow>

class QPlainTextEdit;
class QLabel;
class QPushButton;

class UdfDebugWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit UdfDebugWindow(QWidget* parent = nullptr);

  void setPauseLocation(const QString& filePath, int lineNumber);
  void setPaused(bool paused);

signals:
  void debugStepOver();
  void debugStepIn();
  void debugStepOut();
  void debugContinue();
  void debugAbort();

private:
  QLabel* statusLabel_ = nullptr;
  QLabel* locationLabel_ = nullptr;
  QPlainTextEdit* editor_ = nullptr;
  QPushButton* stepBtn_ = nullptr;
  QPushButton* stepInBtn_ = nullptr;
  QPushButton* stepOutBtn_ = nullptr;
  QPushButton* continueBtn_ = nullptr;
  QPushButton* abortBtn_ = nullptr;
};
