#pragma once

#include "AuxEngineFacade.h"

#include <QAudioSink>
#include <QBuffer>
#include <QCloseEvent>
#include <QMainWindow>
#include <QPointer>

class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QMenu;
class CommandConsole;
class SignalGraphWindow;
class SignalTableWindow;
class TextObjectWindow;
class UdfDebugWindow;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow();
  ~MainWindow() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

private:
  enum class WindowKind {
    Graph,
    Table,
    Text,
  };

  struct ScopedWindow {
    QString varName;
    auxContext* scope = nullptr;
    WindowKind kind = WindowKind::Graph;
    QPointer<QWidget> window;
  };

  void buildUi();
  void buildMenus();
  void connectSignals();

  void runCommand(const QString& cmd);
  QString selectedVarName() const;
  void refreshVariables();
  void refreshDebugView();

  void addHistory(const QString& cmd);
  void injectCommandFromHistory(const QString& cmd, bool execute);
  void loadHistory();
  void saveHistory() const;
  void navigateHistoryFromCommand(int delta);
  void reverseSearchFromCommand();

  void openSignalGraphForSelected();
  void focusSignalGraphForSelected();
  void openSignalTableForSelected();
  void playSelectedAudioFromVarBox();

  void trackWindow(const QString& varName, QWidget* window, WindowKind kind);
  SignalGraphWindow* findSignalGraphWindow(const QString& varName, auxContext* scope) const;
  void focusWindow(QWidget* window) const;
  void reconcileScopedWindows();

  bool variableSupportsSignalDisplay(const QString& varName) const;
  bool variableIsAudio(const QString& varName) const;
  bool variableIsString(const QString& varName) const;
  bool variableIsBinary(const QString& varName) const;

  void handleDebugAction(auxDebugAction action);
  void toggleDebugWindowVisible(bool visible);
  void focusMainWindow();
  void focusDebugWindow();
  void openUdfFile();
  void openRecentUdf();
  void closeUdfFile();
  void updateRecentUdfMenu();
  void addRecentUdfFile(const QString& filePath);
  void loadRecentUdfFiles();
  void saveRecentUdfFiles() const;
  void loadPersistedRuntimeSettings();
  void savePersistedRuntimeSettings() const;
  void toggleBreakpointAtCursor();
  void setBreakpointAtLine(int lineNumber, bool enable);
  void showSettingsDialog();

  AuxEngineFacade engine_;

  CommandConsole* commandBox_ = nullptr;
  QTreeWidget* audioVariableBox_ = nullptr;
  QTreeWidget* nonAudioVariableBox_ = nullptr;
  QListWidget* historyBox_ = nullptr;
  UdfDebugWindow* debugWindow_ = nullptr;
  QAction* showDebugWindowAction_ = nullptr;
  QAction* focusMainWindowAction_ = nullptr;
  QAction* focusDebugWindowAction_ = nullptr;
  QAction* openUdfFileAction_ = nullptr;
  QAction* closeUdfFileAction_ = nullptr;
  QAction* showSettingsAction_ = nullptr;
  QAction* toggleBreakpointAction_ = nullptr;
  QAction* debugContinueAction_ = nullptr;
  QAction* debugStepOverAction_ = nullptr;
  QAction* debugStepInAction_ = nullptr;
  QAction* debugStepOutAction_ = nullptr;
  QAction* debugAbortAction_ = nullptr;
  QMenu* openRecentMenu_ = nullptr;

  std::vector<ScopedWindow> scopedWindows_;

  QAudioSink* varAudioSink_ = nullptr;
  QBuffer* varAudioBuffer_ = nullptr;
  QByteArray varPcmData_;

  int historyNavIndex_ = -1;
  QString historyDraft_;
  bool reverseSearchActive_ = false;
  QString reverseSearchTerm_;
  int reverseSearchIndex_ = -1;

  QString currentUdfFilePath_;
  QString currentUdfName_;
  QStringList recentUdfFiles_;
};
