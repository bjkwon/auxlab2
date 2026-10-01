#pragma once

#include <QColor>
#include <QPlainTextEdit>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

class CommandConsole : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit CommandConsole(QWidget* parent = nullptr);

  QString currentCommand() const;
  void setCurrentCommand(const QString& cmd);
  void setPrompt(const QString& prompt);
  void submitCurrentCommand();
  void appendExecutionResult(const QString& output);
  void appendAsyncOutput(const QString& output);

  // Busy mode covers a submission that finishes asynchronously (shell commands). Output is
  // appended without a prompt, input is blocked, and Ctrl+C (physical Control on macOS)
  // emits interruptRequested(). endBusy() restores the prompt.
  void beginBusy();
  void appendBusyOutput(const QString& output);
  void endBusy();
  bool isBusy() const { return busy_; }

signals:
  void commandSubmitted(const QString& cmd);
  void interruptRequested();
  void historyNavigateRequested(int delta);
  void reverseSearchRequested();
  void objectUndoRequested();
  void objectRedoRequested();

protected:
  bool event(QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  void appendPrompt();
  void ensureEditableCursor();
  bool documentEndsWithNewline() const;
  bool isInterruptKey(QKeyEvent* event) const;
  QString quotedPathListFromMimeData(const QMimeData* mimeData) const;

  QString prompt_ = "AUX> ";
  QColor promptColor_ = QColor(90, 180, 255);
  int inputStartPos_ = 0;
  bool busy_ = false;
};
