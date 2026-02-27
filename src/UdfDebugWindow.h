#pragma once

#include <QMainWindow>
#include <QHash>
#include <QSet>

class QLabel;
class QPushButton;
class QKeyEvent;
class QTabWidget;
class DebugCodeEditor;

class UdfDebugWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit UdfDebugWindow(QWidget* parent = nullptr);

  void setFile(const QString& filePath);
  void closeFile(const QString& filePath);
  QString currentFilePath() const;
  void setPauseLocation(const QString& filePath, int lineNumber);
  void setPaused(bool paused);
  int cursorLine() const;
  bool hasBreakpoint(int lineNumber) const;
  void setBreakpoints(const QSet<int>& lines);
  void setBreakpointsForFile(const QString& filePath, const QSet<int>& lines);
  void toggleBreakpointAtCursor();

signals:
  void debugStepOver();
  void debugStepIn();
  void debugStepOut();
  void debugContinue();
  void debugAbort();
  void breakpointToggleRequested(int lineNumber, bool enable);

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  int findTabByPath(const QString& filePath) const;
  DebugCodeEditor* currentEditor() const;
  bool ensureTab(const QString& filePath, bool activate);
  bool saveTab(int index);
  bool maybeSaveTab(int index);
  bool loadEditorFromFile(DebugCodeEditor* editor, const QString& filePath);
  void refreshSelections(DebugCodeEditor* editor, const QString& filePath);
  void refreshSelections();
  void refreshLocationLabel();
  void updateSaveEnabled();
  void updateTabTitle(int index);

  QHash<QString, QSet<int>> breakpointsByFile_;
  int pausedLine_ = -1;

  QLabel* statusLabel_ = nullptr;
  QLabel* locationLabel_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QPushButton* saveBtn_ = nullptr;
  QPushButton* stepBtn_ = nullptr;
  QPushButton* stepInBtn_ = nullptr;
  QPushButton* stepOutBtn_ = nullptr;
  QPushButton* continueBtn_ = nullptr;
  QPushButton* abortBtn_ = nullptr;
};
