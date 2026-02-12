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
class CommandConsole;
class SignalGraphWindow;
class SignalTableWindow;
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
  };

  struct ScopedWindow {
    QString varName;
    auxContext* scope = nullptr;
    WindowKind kind = WindowKind::Graph;
    QPointer<QWidget> window;
  };

  void buildUi();
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
  void openSignalTableForSelected();
  void playSelectedAudioFromVarBox();

  void trackWindow(const QString& varName, QWidget* window, WindowKind kind);
  void reconcileScopedWindows();

  bool variableSupportsSignalDisplay(const QString& varName) const;
  bool variableIsAudio(const QString& varName) const;

  void handleDebugAction(auxDebugAction action);

  AuxEngineFacade engine_;

  CommandConsole* commandBox_ = nullptr;
  QTreeWidget* variableBox_ = nullptr;
  QListWidget* historyBox_ = nullptr;
  UdfDebugWindow* debugWindow_ = nullptr;

  std::vector<ScopedWindow> scopedWindows_;

  QAudioSink* varAudioSink_ = nullptr;
  QBuffer* varAudioBuffer_ = nullptr;
  QByteArray varPcmData_;

  int historyNavIndex_ = -1;
  QString historyDraft_;
  bool reverseSearchActive_ = false;
  QString reverseSearchTerm_;
  int reverseSearchIndex_ = -1;
};
