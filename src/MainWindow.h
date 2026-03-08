#pragma once

#include "AuxEngineFacade.h"

#include <QAudioSink>
#include <QBuffer>
#include <QCloseEvent>
#include <QDateTime>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QMenu;
class QSplitter;
class QFileSystemWatcher;
class CommandConsole;
class SignalGraphWindow;
class SignalTableWindow;
class StructMembersWindow;
class CellMembersWindow;
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
  void onAsyncPollTick();
  void updateCommandPrompt();
  QString selectedVarName() const;
  QStringList selectedVarNames(QTreeWidget* box) const;
  void deleteVariablesFromBox(QTreeWidget* box);
  void refreshVariables();
  void refreshDebugView();

  void addHistory(const QString& cmd);
  void addHistoryComment(const QString& text);
  void updateHistoryItemDisplay(QListWidgetItem* item) const;
  bool isHistoryCommentItem(const QListWidgetItem* item) const;
  QString historyItemCommand(const QListWidgetItem* item) const;
  QVector<int> historyCommandRows() const;
  void addHistorySessionBanner();
  void injectCommandFromHistory(const QString& cmd, bool execute);
  void loadHistory();
  void saveHistory() const;
  void navigateHistoryFromCommand(int delta);
  void reverseSearchFromCommand();

  void openSignalGraphForSelected();
  void focusSignalGraphForSelected();
  void openSignalTableForSelected();
  void playSelectedAudioFromVarBox();
  void openSignalGraphForPath(const QString& path);
  void openPathDetail(const QString& path);
  void playAudioForPath(const QString& path);
  void openStructMembersForPath(const QString& path);
  void openCellMembersForPath(const QString& path);

  void trackWindow(const QString& varName, QWidget* window, WindowKind kind);
  SignalGraphWindow* findSignalGraphWindow(const QString& varName, auxContext* scope) const;
  void focusWindow(QWidget* window) const;
  void reconcileScopedWindows();
  std::vector<QWidget*> focusableScopedWindows(std::optional<WindowKind> kind = std::nullopt) const;
  void focusScopedWindowByOffset(int delta, std::optional<WindowKind> kind = std::nullopt);
  void focusScopedWindowByIndex(int oneBasedIndex);
  void toggleLastTwoScopedWindows();
  void closeAllScopedWindowsInCurrentScope();
  void noteScopedWindowFocus(QWidget* window);

  bool variableSupportsSignalDisplay(const QString& varName) const;
  bool variableIsAudio(const QString& varName) const;
  bool variableIsString(const QString& varName) const;
  bool variableIsBinary(const QString& varName) const;
  bool variableIsStruct(const QString& varName) const;
  bool variableIsCell(const QString& varName) const;

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
  void loadPersistedWindowLayout();
  void savePersistedWindowLayout() const;
  void startWatchingCurrentUdf();
  bool reloadCurrentUdfIfStale(const QString& reason, bool forceReload = false);
  void toggleBreakpointAtCursor();
  void setBreakpointAtLine(int lineNumber, bool enable);
  QString activeDebugUdfName() const;
  QString activeDebugFilePath() const;
  void showSettingsDialog();
  void showAboutDialog();

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
  QAction* openRecentQuickAction_ = nullptr;
  QAction* closeUdfFileAction_ = nullptr;
  QAction* showSettingsAction_ = nullptr;
  QAction* showAboutAction_ = nullptr;
  QAction* toggleBreakpointAction_ = nullptr;
  QAction* debugContinueAction_ = nullptr;
  QAction* debugStepOverAction_ = nullptr;
  QAction* debugStepInAction_ = nullptr;
  QAction* debugStepOutAction_ = nullptr;
  QAction* debugAbortAction_ = nullptr;
  QMenu* openRecentMenu_ = nullptr;
  QSplitter* mainSplitter_ = nullptr;
  QSplitter* variableSectionSplitter_ = nullptr;
  QFileSystemWatcher* udfFileWatcher_ = nullptr;
  QDateTime currentUdfLastModified_;
  QRect pendingDebugWindowRect_;
  bool pendingDebugWindowRectValid_ = false;
  bool appliedInitialDebugWindowRect_ = false;

  std::vector<ScopedWindow> scopedWindows_;
  mutable QPointer<QWidget> lastFocusedScopedWindow_;
  mutable QPointer<QWidget> prevFocusedScopedWindow_;

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
  QTimer* asyncPollTimer_ = nullptr;
};
