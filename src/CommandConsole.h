#pragma once

#include <QColor>
#include <QPlainTextEdit>

class CommandConsole : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit CommandConsole(QWidget* parent = nullptr);

  QString currentCommand() const;
  void setCurrentCommand(const QString& cmd);
  void submitCurrentCommand();
  void appendExecutionResult(const QString& output);

signals:
  void commandSubmitted(const QString& cmd);
  void historyNavigateRequested(int delta);
  void reverseSearchRequested();

protected:
  bool event(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  void appendPrompt();
  void ensureEditableCursor();

  QString prompt_ = "AUX> ";
  QColor promptColor_ = QColor(90, 180, 255);
  int inputStartPos_ = 0;
};
