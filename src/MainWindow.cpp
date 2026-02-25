#include "MainWindow.h"

#include "BinaryObjectWindow.h"
#include "CommandConsole.h"
#include "SignalGraphWindow.h"
#include "SignalTableWindow.h"
#include "TextObjectWindow.h"
#include "UdfDebugWindow.h"

#include <QAbstractItemView>
#include <QAudioFormat>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSplitter>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace {
constexpr int kMaxRecentUdfFiles = 8;

QString historyFilePath() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) {
    dir = QDir::homePath();
  }
  QDir d(dir);
  d.mkpath(".");
  return d.filePath("auxlab2.history");
}

QString truncateDisplayText(const std::string& s, int maxChars = 140) {
  const QString q = QString::fromStdString(s);
  if (q.size() <= maxChars) {
    return q;
  }
  return q.left(maxChars - 3) + "...";
}

#ifdef Q_OS_MAC
constexpr Qt::KeyboardModifier kPrimaryWindowModifier = Qt::MetaModifier;
#else
constexpr Qt::KeyboardModifier kPrimaryWindowModifier = Qt::ControlModifier;
#endif

QKeySequence primaryWindowShortcut(Qt::Key key, Qt::KeyboardModifiers extra = Qt::NoModifier) {
  return QKeySequence(QKeyCombination(kPrimaryWindowModifier | extra, key));
}
}  // namespace

MainWindow::MainWindow() {
  if (!engine_.init()) {
    QMessageBox::critical(nullptr, "AUX", "Failed to initialize AUX engine.");
  }

  loadPersistedRuntimeSettings();
  buildUi();
  loadRecentUdfFiles();
  buildMenus();
  connectSignals();
  loadHistory();
  refreshVariables();
  refreshDebugView();
}

MainWindow::~MainWindow() {
  if (varAudioSink_) {
    varAudioSink_->stop();
  }
}

void MainWindow::buildUi() {
  setWindowTitle("auxlab2");
  resize(1200, 760);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* splitter = new QSplitter(this);

  commandBox_ = new CommandConsole(this);

  auto* variablePanel = new QWidget(this);
  auto* variableLayout = new QVBoxLayout(variablePanel);
  variableLayout->setContentsMargins(0, 0, 0, 0);

  auto* variableSectionSplitter = new QSplitter(Qt::Vertical, variablePanel);

  auto* audioSection = new QWidget(variableSectionSplitter);
  auto* audioLayout = new QVBoxLayout(audioSection);
  audioLayout->setContentsMargins(0, 0, 0, 0);
  audioLayout->addWidget(new QLabel("Audio Objects", audioSection));
  audioVariableBox_ = new QTreeWidget(audioSection);
  audioVariableBox_->setColumnCount(4);
  audioVariableBox_->setHeaderLabels({"Name", "dbRMS", "Size", "Signal Intervals (ms)"});
  audioVariableBox_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  audioVariableBox_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  audioVariableBox_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  audioVariableBox_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
  audioVariableBox_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  audioVariableBox_->installEventFilter(this);
  audioLayout->addWidget(audioVariableBox_);

  auto* nonAudioSection = new QWidget(variableSectionSplitter);
  auto* nonAudioLayout = new QVBoxLayout(nonAudioSection);
  nonAudioLayout->setContentsMargins(0, 0, 0, 0);
  nonAudioLayout->addWidget(new QLabel("Non-Audio Objects", nonAudioSection));
  nonAudioVariableBox_ = new QTreeWidget(nonAudioSection);
  nonAudioVariableBox_->setColumnCount(4);
  nonAudioVariableBox_->setHeaderLabels({"Name", "Type", "Size", "Content"});
  nonAudioVariableBox_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  nonAudioVariableBox_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  nonAudioVariableBox_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  nonAudioVariableBox_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
  nonAudioVariableBox_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  nonAudioVariableBox_->installEventFilter(this);
  nonAudioLayout->addWidget(nonAudioVariableBox_);

  variableSectionSplitter->addWidget(audioSection);
  variableSectionSplitter->addWidget(nonAudioSection);
  variableSectionSplitter->setStretchFactor(0, 1);
  variableSectionSplitter->setStretchFactor(1, 1);
  variableLayout->addWidget(variableSectionSplitter);

  historyBox_ = new QListWidget(this);
  historyBox_->setSelectionMode(QAbstractItemView::SingleSelection);
  historyBox_->installEventFilter(this);

  splitter->addWidget(commandBox_);
  splitter->addWidget(variablePanel);
  splitter->addWidget(historyBox_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  splitter->setStretchFactor(2, 2);

  layout->addWidget(splitter);
  setCentralWidget(central);

  debugWindow_ = new UdfDebugWindow(this);
  debugWindow_->hide();
  debugWindow_->installEventFilter(this);
}

void MainWindow::buildMenus() {
  auto* fileMenu = menuBar()->addMenu("&File");
  openUdfFileAction_ = fileMenu->addAction("&Open UDF...");
  openUdfFileAction_->setShortcut(QKeySequence::Open);
  openUdfFileAction_->setShortcutContext(Qt::ApplicationShortcut);
  openRecentMenu_ = fileMenu->addMenu("Open &Recent");
  updateRecentUdfMenu();
  closeUdfFileAction_ = fileMenu->addAction("&Close UDF");
  closeUdfFileAction_->setEnabled(false);

  auto* viewMenu = menuBar()->addMenu("&View");
  showDebugWindowAction_ = viewMenu->addAction("Show &Debug Window");
  showDebugWindowAction_->setCheckable(true);
  showDebugWindowAction_->setChecked(false);
  showDebugWindowAction_->setShortcut(QKeySequence("Ctrl+Alt+D"));
  showDebugWindowAction_->setShortcutContext(Qt::ApplicationShortcut);
  focusDebugWindowAction_ = viewMenu->addAction("Focus &Debug Window");
  focusDebugWindowAction_->setShortcutContext(Qt::ApplicationShortcut);
  focusMainWindowAction_ = viewMenu->addAction("Focus &Main Window");
  focusMainWindowAction_->setShortcutContext(Qt::ApplicationShortcut);

  auto* windowMenu = menuBar()->addMenu("&Window");

  auto* nextWindowAction = windowMenu->addAction("Next Window");
  nextWindowAction->setShortcut(primaryWindowShortcut(Qt::Key_Tab));
  nextWindowAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(nextWindowAction, &QAction::triggered, this, [this]() { focusScopedWindowByOffset(+1); });

  auto* prevWindowAction = windowMenu->addAction("Previous Window");
  prevWindowAction->setShortcut(primaryWindowShortcut(Qt::Key_Tab, Qt::ShiftModifier));
  prevWindowAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(prevWindowAction, &QAction::triggered, this, [this]() { focusScopedWindowByOffset(-1); });

  windowMenu->addSeparator();
  for (int i = 1; i <= 9; ++i) {
    auto* nthAction = windowMenu->addAction(QString("Focus Window %1").arg(i));
    nthAction->setShortcut(primaryWindowShortcut(static_cast<Qt::Key>(Qt::Key_0 + i)));
    nthAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(nthAction, &QAction::triggered, this, [this, i]() { focusScopedWindowByIndex(i); });
  }

  windowMenu->addSeparator();
  auto* nextGraphAction = windowMenu->addAction("Next Graph Window");
  nextGraphAction->setShortcut(primaryWindowShortcut(Qt::Key_G));
  nextGraphAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(nextGraphAction, &QAction::triggered, this, [this]() {
    focusScopedWindowByOffset(+1, WindowKind::Graph);
  });

  auto* prevGraphAction = windowMenu->addAction("Previous Graph Window");
  prevGraphAction->setShortcut(primaryWindowShortcut(Qt::Key_G, Qt::ShiftModifier));
  prevGraphAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(prevGraphAction, &QAction::triggered, this, [this]() {
    focusScopedWindowByOffset(-1, WindowKind::Graph);
  });

  auto* nextTableAction = windowMenu->addAction("Next Table Window");
  nextTableAction->setShortcut(primaryWindowShortcut(Qt::Key_T));
  nextTableAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(nextTableAction, &QAction::triggered, this, [this]() {
    focusScopedWindowByOffset(+1, WindowKind::Table);
  });

  auto* prevTableAction = windowMenu->addAction("Previous Table Window");
  prevTableAction->setShortcut(primaryWindowShortcut(Qt::Key_T, Qt::ShiftModifier));
  prevTableAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(prevTableAction, &QAction::triggered, this, [this]() {
    focusScopedWindowByOffset(-1, WindowKind::Table);
  });

  windowMenu->addSeparator();
  auto* toggleLastTwoAction = windowMenu->addAction("Toggle Last Two Windows");
  toggleLastTwoAction->setShortcut(primaryWindowShortcut(Qt::Key_QuoteLeft));
  toggleLastTwoAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(toggleLastTwoAction, &QAction::triggered, this, &MainWindow::toggleLastTwoScopedWindows);

  auto* closeAllScopedAction = windowMenu->addAction("Close All Windows In Scope");
  closeAllScopedAction->setShortcut(primaryWindowShortcut(Qt::Key_W, Qt::ShiftModifier));
  closeAllScopedAction->setShortcutContext(Qt::ApplicationShortcut);
  connect(closeAllScopedAction, &QAction::triggered, this, &MainWindow::closeAllScopedWindowsInCurrentScope);

  auto* settingsMenu = menuBar()->addMenu("&Settings");
  showSettingsAction_ = settingsMenu->addAction("View Runtime &Settings");

  auto* debugMenu = menuBar()->addMenu("&Debug");
  toggleBreakpointAction_ = debugMenu->addAction("Toggle &Breakpoint");
  toggleBreakpointAction_->setShortcut(QKeySequence(Qt::Key_F9));
  toggleBreakpointAction_->setShortcutContext(Qt::ApplicationShortcut);

  debugContinueAction_ = debugMenu->addAction("&Continue");
  debugContinueAction_->setShortcut(QKeySequence(Qt::Key_F5));
  debugContinueAction_->setShortcutContext(Qt::ApplicationShortcut);

  debugStepOverAction_ = debugMenu->addAction("Step &Over");
  debugStepOverAction_->setShortcut(QKeySequence(Qt::Key_F10));
  debugStepOverAction_->setShortcutContext(Qt::ApplicationShortcut);

  debugStepInAction_ = debugMenu->addAction("Step &In");
  debugStepInAction_->setShortcut(QKeySequence(Qt::Key_F11));
  debugStepInAction_->setShortcutContext(Qt::ApplicationShortcut);

  debugStepOutAction_ = debugMenu->addAction("Step O&ut");
  debugStepOutAction_->setShortcut(QKeySequence("Shift+F11"));
  debugStepOutAction_->setShortcutContext(Qt::ApplicationShortcut);

  debugAbortAction_ = debugMenu->addAction("&Abort");
  debugAbortAction_->setShortcut(QKeySequence("Shift+F5"));
  debugAbortAction_->setShortcutContext(Qt::ApplicationShortcut);
}

void MainWindow::connectSignals() {
  connect(commandBox_, &CommandConsole::commandSubmitted, this, &MainWindow::runCommand);
  connect(commandBox_, &CommandConsole::historyNavigateRequested, this, &MainWindow::navigateHistoryFromCommand);
  connect(commandBox_, &CommandConsole::reverseSearchRequested, this, &MainWindow::reverseSearchFromCommand);

  connect(openUdfFileAction_, &QAction::triggered, this, &MainWindow::openUdfFile);
  connect(closeUdfFileAction_, &QAction::triggered, this, &MainWindow::closeUdfFile);
  connect(showDebugWindowAction_, &QAction::toggled, this, &MainWindow::toggleDebugWindowVisible);
  connect(focusMainWindowAction_, &QAction::triggered, this, &MainWindow::focusMainWindow);
  connect(focusDebugWindowAction_, &QAction::triggered, this, &MainWindow::focusDebugWindow);
  connect(showSettingsAction_, &QAction::triggered, this, &MainWindow::showSettingsDialog);
  connect(toggleBreakpointAction_, &QAction::triggered, this, &MainWindow::toggleBreakpointAtCursor);
  connect(debugContinueAction_, &QAction::triggered, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_CONTINUE);
  });
  connect(debugStepOverAction_, &QAction::triggered, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP);
  });
  connect(debugStepInAction_, &QAction::triggered, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP_IN);
  });
  connect(debugStepOutAction_, &QAction::triggered, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP_OUT);
  });
  connect(debugAbortAction_, &QAction::triggered, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_ABORT_BASE);
  });

  connect(historyBox_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (!item) {
      return;
    }
    injectCommandFromHistory(item->text(), true);
  });

  auto variableDoubleClick = [this](QTreeWidgetItem*, int) {
    openSignalTableForSelected();
  };
  connect(audioVariableBox_, &QTreeWidget::itemDoubleClicked, this, variableDoubleClick);
  connect(nonAudioVariableBox_, &QTreeWidget::itemDoubleClicked, this, variableDoubleClick);

  connect(debugWindow_, &UdfDebugWindow::debugStepOver, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP);
  });
  connect(debugWindow_, &UdfDebugWindow::debugStepIn, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP_IN);
  });
  connect(debugWindow_, &UdfDebugWindow::debugStepOut, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_STEP_OUT);
  });
  connect(debugWindow_, &UdfDebugWindow::debugContinue, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_CONTINUE);
  });
  connect(debugWindow_, &UdfDebugWindow::debugAbort, this, [this]() {
    handleDebugAction(auxDebugAction::AUX_DEBUG_ABORT_BASE);
  });
  connect(debugWindow_, &UdfDebugWindow::breakpointToggleRequested, this, &MainWindow::setBreakpointAtLine);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveHistory();
  saveRecentUdfFiles();
  savePersistedRuntimeSettings();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::WindowActivate || event->type() == QEvent::FocusIn) {
    if (auto* w = qobject_cast<QWidget*>(watched)) {
      noteScopedWindowFocus(w);
    }
  }

  if (watched == debugWindow_) {
    if (event->type() == QEvent::Hide && showDebugWindowAction_) {
      showDebugWindowAction_->setChecked(false);
    } else if (event->type() == QEvent::Show && showDebugWindowAction_) {
      showDebugWindowAction_->setChecked(true);
    }
  }

  if (watched == historyBox_ && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      auto* item = historyBox_->currentItem();
      if (item) {
        injectCommandFromHistory(item->text(), false);
      }
      return true;
    }
  }

  if ((watched == audioVariableBox_ || watched == nonAudioVariableBox_) && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    auto* box = qobject_cast<QTreeWidget*>(watched);
    if (ke->key() == Qt::Key_Delete && (ke->modifiers() & Qt::ShiftModifier) && box) {
      deleteVariablesFromBox(box);
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (watched == audioVariableBox_) {
        focusSignalGraphForSelected();
      } else if (watched == nonAudioVariableBox_) {
        auto* item = nonAudioVariableBox_->currentItem();
        if (item && item->text(1) == "VECT") {
          focusSignalGraphForSelected();
        }
      }
      return true;
    }
    if (ke->key() == Qt::Key_Space) {
      playSelectedAudioFromVarBox();
      return true;
    }
  }

  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleDebugWindowVisible(bool visible) {
  if (!debugWindow_) {
    return;
  }
  if (visible) {
    debugWindow_->show();
    debugWindow_->raise();
    debugWindow_->activateWindow();
  } else {
    debugWindow_->hide();
  }
}

void MainWindow::focusMainWindow() {
  show();
  raise();
  activateWindow();
}

void MainWindow::focusDebugWindow() {
  toggleDebugWindowVisible(true);
}

void MainWindow::loadRecentUdfFiles() {
  QSettings settings("auxlab2", "auxlab2");
  recentUdfFiles_ = settings.value("recent_udf_files").toStringList();
  while (recentUdfFiles_.size() > kMaxRecentUdfFiles) {
    recentUdfFiles_.removeLast();
  }
}

void MainWindow::saveRecentUdfFiles() const {
  QSettings settings("auxlab2", "auxlab2");
  settings.setValue("recent_udf_files", recentUdfFiles_);
}

void MainWindow::loadPersistedRuntimeSettings() {
  QSettings settings("auxlab2", "auxlab2");
  if (!settings.contains("runtime_settings/sample_rate")) {
    return;
  }

  RuntimeSettingsSnapshot cfg = engine_.runtimeSettings();
  cfg.sampleRate = settings.value("runtime_settings/sample_rate", cfg.sampleRate).toInt();
  cfg.displayPrecision = settings.value("runtime_settings/display_precision", cfg.displayPrecision).toInt();
  cfg.displayLimitX = settings.value("runtime_settings/display_limit_x", cfg.displayLimitX).toInt();
  cfg.displayLimitY = settings.value("runtime_settings/display_limit_y", cfg.displayLimitY).toInt();
  cfg.displayLimitBytes = settings.value("runtime_settings/display_limit_bytes", cfg.displayLimitBytes).toInt();
  cfg.displayLimitStr = settings.value("runtime_settings/display_limit_str", cfg.displayLimitStr).toInt();

  cfg.udfPaths.clear();
  const QStringList savedPaths = settings.value("runtime_settings/udf_paths").toStringList();
  for (const QString& p : savedPaths) {
    const QString trimmed = p.trimmed();
    if (!trimmed.isEmpty()) {
      cfg.udfPaths.push_back(trimmed.toStdString());
    }
  }

  std::string err;
  engine_.applyRuntimeSettings(cfg, err);
}

void MainWindow::savePersistedRuntimeSettings() const {
  const RuntimeSettingsSnapshot cfg = engine_.runtimeSettings();
  QSettings settings("auxlab2", "auxlab2");
  settings.setValue("runtime_settings/sample_rate", cfg.sampleRate);
  settings.setValue("runtime_settings/display_precision", cfg.displayPrecision);
  settings.setValue("runtime_settings/display_limit_x", cfg.displayLimitX);
  settings.setValue("runtime_settings/display_limit_y", cfg.displayLimitY);
  settings.setValue("runtime_settings/display_limit_bytes", cfg.displayLimitBytes);
  settings.setValue("runtime_settings/display_limit_str", cfg.displayLimitStr);

  QStringList paths;
  for (const std::string& p : cfg.udfPaths) {
    paths.push_back(QString::fromStdString(p));
  }
  settings.setValue("runtime_settings/udf_paths", paths);
}

void MainWindow::updateRecentUdfMenu() {
  if (!openRecentMenu_) {
    return;
  }

  openRecentMenu_->clear();
  if (recentUdfFiles_.isEmpty()) {
    auto* none = openRecentMenu_->addAction("(No recent files)");
    none->setEnabled(false);
    return;
  }

  for (const QString& path : recentUdfFiles_) {
    QFileInfo fi(path);
    auto* a = openRecentMenu_->addAction(fi.fileName());
    a->setToolTip(path);
    a->setData(path);
    connect(a, &QAction::triggered, this, &MainWindow::openRecentUdf);
  }
}

void MainWindow::addRecentUdfFile(const QString& filePath) {
  recentUdfFiles_.removeAll(filePath);
  recentUdfFiles_.prepend(filePath);
  while (recentUdfFiles_.size() > kMaxRecentUdfFiles) {
    recentUdfFiles_.removeLast();
  }
  updateRecentUdfMenu();
}

void MainWindow::openRecentUdf() {
  auto* action = qobject_cast<QAction*>(sender());
  if (!action) {
    return;
  }
  const QString filePath = action->data().toString();
  if (filePath.isEmpty()) {
    return;
  }
  if (!QFileInfo::exists(filePath)) {
    recentUdfFiles_.removeAll(filePath);
    updateRecentUdfMenu();
    statusBar()->showMessage("Recent file no longer exists.", 2500);
    return;
  }

  std::string err;
  if (!engine_.loadUdfFile(filePath.toStdString(), err)) {
    QMessageBox::warning(this, "Open Recent UDF", QString::fromStdString(err));
    return;
  }

  QFileInfo fi(filePath);
  currentUdfFilePath_ = fi.absoluteFilePath();
  currentUdfName_ = fi.completeBaseName();
  closeUdfFileAction_->setEnabled(true);

  debugWindow_->setFile(currentUdfFilePath_);
  const auto bps = engine_.getBreakpoints(currentUdfName_.toStdString());
  QSet<int> qbps;
  for (int line : bps) {
    qbps.insert(line);
  }
  debugWindow_->setBreakpoints(qbps);
  addRecentUdfFile(currentUdfFilePath_);
  toggleDebugWindowVisible(true);
  refreshDebugView();
}

void MainWindow::openUdfFile() {
  const QString filePath = QFileDialog::getOpenFileName(this, "Open UDF File", QString(), "AUX UDF (*.aux);;All Files (*.*)");
  if (filePath.isEmpty()) {
    return;
  }

  std::string err;
  if (!engine_.loadUdfFile(filePath.toStdString(), err)) {
    QMessageBox::warning(this, "Open UDF", QString::fromStdString(err));
    return;
  }

  QFileInfo fi(filePath);
  currentUdfFilePath_ = fi.absoluteFilePath();
  currentUdfName_ = fi.completeBaseName();
  closeUdfFileAction_->setEnabled(true);

  debugWindow_->setFile(currentUdfFilePath_);
  const auto bps = engine_.getBreakpoints(currentUdfName_.toStdString());
  QSet<int> qbps;
  for (int line : bps) {
    qbps.insert(line);
  }
  debugWindow_->setBreakpoints(qbps);
  addRecentUdfFile(currentUdfFilePath_);
  toggleDebugWindowVisible(true);
  refreshDebugView();
}

void MainWindow::closeUdfFile() {
  if (!currentUdfFilePath_.isEmpty()) {
    debugWindow_->closeFile(currentUdfFilePath_);
  }
  currentUdfFilePath_.clear();
  currentUdfName_.clear();
  debugWindow_->setFile(QString());
  debugWindow_->setBreakpoints(QSet<int>{});
  closeUdfFileAction_->setEnabled(false);
  refreshDebugView();
}

void MainWindow::toggleBreakpointAtCursor() {
  if (currentUdfName_.isEmpty()) {
    statusBar()->showMessage("Open a UDF file first.", 2000);
    return;
  }
  toggleDebugWindowVisible(true);
  debugWindow_->toggleBreakpointAtCursor();
}

void MainWindow::setBreakpointAtLine(int lineNumber, bool enable) {
  if (currentUdfName_.isEmpty() || lineNumber <= 0) {
    return;
  }

  std::string err;
  if (!engine_.setBreakpoint(currentUdfName_.toStdString(), lineNumber, enable, err)) {
    statusBar()->showMessage(QString::fromStdString(err), 2500);
    return;
  }

  const auto bps = engine_.getBreakpoints(currentUdfName_.toStdString());
  QSet<int> qbps;
  for (int line : bps) {
    qbps.insert(line);
  }
  debugWindow_->setBreakpoints(qbps);
  statusBar()->showMessage(enable ? QString("Breakpoint set at line %1").arg(lineNumber)
                                  : QString("Breakpoint cleared at line %1").arg(lineNumber),
                           1500);
}

void MainWindow::runCommand(const QString& cmd) {
  const QString actual = cmd;
  if (!actual.trimmed().isEmpty()) {
    addHistory(actual);
    auto result = engine_.eval(actual.toStdString());
    updateCommandPrompt();
    const QString trimmed = actual.trimmed();
    const bool suppressEcho = trimmed.endsWith(';');
    const bool isOk = result.status == static_cast<int>(auxEvalStatus::AUX_EVAL_OK);
    if (suppressEcho && isOk) {
      commandBox_->appendExecutionResult({});
    } else {
      commandBox_->appendExecutionResult(QString::fromStdString(result.output));
    }
    historyNavIndex_ = -1;
    historyDraft_.clear();
    reverseSearchActive_ = false;
    reverseSearchTerm_.clear();
    reverseSearchIndex_ = -1;
  } else {
    updateCommandPrompt();
    commandBox_->appendExecutionResult({});
    historyNavIndex_ = -1;
    historyDraft_.clear();
    reverseSearchActive_ = false;
    reverseSearchTerm_.clear();
    reverseSearchIndex_ = -1;
  }

  refreshVariables();
  refreshDebugView();
  reconcileScopedWindows();
}

void MainWindow::updateCommandPrompt() {
  if (!commandBox_) {
    return;
  }

  QString prompt = "AUX> ";
  if (engine_.isPaused()) {
    const auto infoOpt = engine_.pauseInfo();
    if (infoOpt && infoOpt->line > 0) {
      const QString filename = QString::fromStdString(infoOpt->filename);
      const QString udfName = QFileInfo(filename).completeBaseName();
      const QString displayName = udfName.isEmpty() ? QFileInfo(filename).fileName() : udfName;
      if (!displayName.isEmpty()) {
        prompt = QString("%1:%2> ").arg(displayName).arg(infoOpt->line);
      }
    }
  }
  commandBox_->setPrompt(prompt);
}

QString MainWindow::selectedVarName() const {
  QTreeWidgetItem* item = nullptr;
  if (audioVariableBox_->hasFocus()) {
    item = audioVariableBox_->currentItem();
  } else if (nonAudioVariableBox_->hasFocus()) {
    item = nonAudioVariableBox_->currentItem();
  } else if (audioVariableBox_->currentItem()) {
    item = audioVariableBox_->currentItem();
  } else {
    item = nonAudioVariableBox_->currentItem();
  }
  if (!item) {
    return {};
  }
  return item->text(0);
}

QStringList MainWindow::selectedVarNames(QTreeWidget* box) const {
  QStringList names;
  if (!box) {
    return names;
  }

  const auto selectedItems = box->selectedItems();
  names.reserve(selectedItems.size());
  for (auto* item : selectedItems) {
    if (item) {
      names.push_back(item->text(0));
    }
  }
  names.removeDuplicates();
  return names;
}

void MainWindow::deleteVariablesFromBox(QTreeWidget* box) {
  const QStringList names = selectedVarNames(box);
  if (names.isEmpty()) {
    return;
  }

  int deleted = 0;
  for (const QString& name : names) {
    if (engine_.deleteVar(name.toStdString())) {
      ++deleted;
    }
  }

  if (deleted > 0) {
    statusBar()->showMessage(QString("Deleted %1 variable%2").arg(deleted).arg(deleted == 1 ? "" : "s"), 2000);
  } else {
    statusBar()->showMessage("No variables deleted.", 2000);
  }
  refreshVariables();
  refreshDebugView();
  reconcileScopedWindows();
}

void MainWindow::refreshVariables() {
  const QString selected = selectedVarName();
  audioVariableBox_->clear();
  nonAudioVariableBox_->clear();

  const auto vars = engine_.listVariables();
  for (const auto& v : vars) {
    auto* box = v.isAudio ? audioVariableBox_ : nonAudioVariableBox_;
    auto* item = new QTreeWidgetItem(box);
    item->setText(0, QString::fromStdString(v.name));
    const QString infoText = truncateDisplayText(v.preview);
    const QString fullInfo = QString::fromStdString(v.preview);
    if (v.isAudio) {
      item->setText(1, QString::fromStdString(v.rms));
      item->setText(2, QString::fromStdString(v.size));
      item->setText(3, infoText);
      item->setToolTip(3, fullInfo);
    } else {
      item->setText(1, QString::fromStdString(v.typeTag));
      item->setText(2, QString::fromStdString(v.size));
      item->setText(3, infoText);
      item->setToolTip(3, fullInfo);
    }

    if (selected == item->text(0)) {
      box->setCurrentItem(item);
    }
  }
}

void MainWindow::refreshDebugView() {
  const bool paused = engine_.isPaused();
  updateCommandPrompt();
  debugWindow_->setPaused(paused);
  if (toggleBreakpointAction_) toggleBreakpointAction_->setEnabled(!currentUdfName_.isEmpty());
  if (debugContinueAction_) debugContinueAction_->setEnabled(paused);
  if (debugStepOverAction_) debugStepOverAction_->setEnabled(paused);
  if (debugStepInAction_) debugStepInAction_->setEnabled(paused);
  if (debugStepOutAction_) debugStepOutAction_->setEnabled(paused);
  if (debugAbortAction_) debugAbortAction_->setEnabled(paused);

  if (paused) {
    // While paused in UDF debugging, always reflect active child workspace variables.
    refreshVariables();
    auto infoOpt = engine_.pauseInfo();
    if (infoOpt) {
      toggleDebugWindowVisible(true);
      debugWindow_->setPauseLocation(QString::fromStdString(infoOpt->filename), infoOpt->line);
    }
  }
}

void MainWindow::addHistory(const QString& cmd) {
  if (cmd.trimmed().isEmpty()) {
    return;
  }
  historyBox_->addItem(cmd);
  historyBox_->scrollToBottom();
}

void MainWindow::loadHistory() {
  QFile f(historyFilePath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  QTextStream in(&f);
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (!line.trimmed().isEmpty()) {
      historyBox_->addItem(line);
    }
  }
  historyBox_->scrollToBottom();
}

void MainWindow::saveHistory() const {
  QFile f(historyFilePath());
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    return;
  }
  QTextStream out(&f);
  for (int i = 0; i < historyBox_->count(); ++i) {
    const QListWidgetItem* item = historyBox_->item(i);
    if (item) {
      out << item->text() << '\n';
    }
  }
}

void MainWindow::injectCommandFromHistory(const QString& cmd, bool execute) {
  commandBox_->setCurrentCommand(cmd);
  historyNavIndex_ = -1;
  historyDraft_.clear();
  reverseSearchActive_ = false;
  reverseSearchTerm_.clear();
  reverseSearchIndex_ = -1;
  if (execute) {
    commandBox_->submitCurrentCommand();
  }
  commandBox_->setFocus();
}

void MainWindow::navigateHistoryFromCommand(int delta) {
  const int n = historyBox_->count();
  if (n <= 0 || delta == 0) {
    return;
  }

  reverseSearchActive_ = false;
  reverseSearchTerm_.clear();
  reverseSearchIndex_ = -1;

  if (historyNavIndex_ == -1) {
    historyDraft_ = commandBox_->currentCommand();
    historyNavIndex_ = n;
  }

  int next = historyNavIndex_ + delta;
  next = std::clamp(next, 0, n);

  if (next == n) {
    historyNavIndex_ = -1;
    historyBox_->clearSelection();
    commandBox_->setCurrentCommand(historyDraft_);
    return;
  }

  historyNavIndex_ = next;
  auto* item = historyBox_->item(historyNavIndex_);
  if (!item) {
    return;
  }
  historyBox_->setCurrentRow(historyNavIndex_);
  commandBox_->setCurrentCommand(item->text());
}

void MainWindow::reverseSearchFromCommand() {
  const int n = historyBox_->count();
  if (n <= 0) {
    return;
  }

  const QString currentInput = commandBox_->currentCommand().trimmed();
  if (reverseSearchActive_ && reverseSearchIndex_ >= 0 && reverseSearchIndex_ < n) {
    auto* curItem = historyBox_->item(reverseSearchIndex_);
    if (curItem && curItem->text() != commandBox_->currentCommand()) {
      reverseSearchActive_ = false;
      reverseSearchTerm_.clear();
      reverseSearchIndex_ = -1;
    }
  }

  if (!reverseSearchActive_) {
    reverseSearchActive_ = true;
    reverseSearchTerm_ = currentInput;
    reverseSearchIndex_ = n;
  }

  int found = -1;
  for (int i = reverseSearchIndex_ - 1; i >= 0; --i) {
    auto* item = historyBox_->item(i);
    if (!item) {
      continue;
    }
    if (reverseSearchTerm_.isEmpty() || item->text().contains(reverseSearchTerm_, Qt::CaseInsensitive)) {
      found = i;
      break;
    }
  }

  if (found < 0) {
    statusBar()->showMessage(QString("reverse-i-search: no earlier match for \"%1\"").arg(reverseSearchTerm_), 2000);
    return;
  }

  reverseSearchIndex_ = found;
  historyNavIndex_ = found;
  historyBox_->setCurrentRow(found);
  const QString match = historyBox_->item(found)->text();
  commandBox_->setCurrentCommand(match);
  statusBar()->showMessage(QString("reverse-i-search \"%1\": %2").arg(reverseSearchTerm_, match), 2500);
}

void MainWindow::openSignalGraphForSelected() {
  const QString var = selectedVarName();
  if (var.isEmpty() || !variableSupportsSignalDisplay(var)) {
    return;
  }
  auto sig = engine_.getSignalData(var.toStdString());
  if (!sig) {
    return;
  }

  const auto currentScope = engine_.activeContext();
  if (auto* existing = findSignalGraphWindow(var, currentScope)) {
    existing->updateData(*sig);
    focusWindow(existing);
    return;
  }

  auto* w = new SignalGraphWindow(
      var, *sig, nullptr,
      [this, var](int viewStart, int viewLen) { return engine_.getSignalFftPowerDb(var.toStdString(), viewStart, viewLen); });
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(var, w, WindowKind::Graph);
  focusWindow(w);
}

void MainWindow::focusSignalGraphForSelected() {
  const QString var = selectedVarName();
  if (var.isEmpty()) {
    return;
  }

  if (auto* existing = findSignalGraphWindow(var, engine_.activeContext())) {
    focusWindow(existing);
    return;
  }

  // Enter key behavior: open on first use, focus on subsequent presses.
  openSignalGraphForSelected();
}

void MainWindow::openSignalTableForSelected() {
  const QString var = selectedVarName();
  if (var.isEmpty()) {
    return;
  }

  if (variableIsString(var)) {
    auto text = engine_.getStringValue(var.toStdString());
    if (!text) {
      return;
    }
    auto* w = new TextObjectWindow(var, QString::fromStdString(*text));
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(var, w, WindowKind::Text);
    w->show();
    return;
  }

  if (variableIsBinary(var)) {
    auto binary = engine_.getBinaryData(var.toStdString());
    if (!binary) {
      return;
    }

    QByteArray data;
    data.resize(static_cast<int>(binary->bytes.size()));
    if (!binary->bytes.empty()) {
      std::memcpy(data.data(), binary->bytes.data(), binary->bytes.size());
    }

    auto* w = new BinaryObjectWindow(var, data);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(var, w, WindowKind::Text);
    w->show();
    return;
  }

  if (!variableSupportsSignalDisplay(var)) {
    return;
  }

  auto sig = engine_.getSignalData(var.toStdString());
  if (!sig) {
    return;
  }

  auto* w = new SignalTableWindow(var, *sig);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(var, w, WindowKind::Table);
  w->show();
}

void MainWindow::playSelectedAudioFromVarBox() {
  const QString var = selectedVarName();
  if (var.isEmpty() || !variableIsAudio(var)) {
    return;
  }

  auto sig = engine_.getSignalData(var.toStdString());
  if (!sig || !sig->isAudio || sig->channels.empty()) {
    return;
  }

  if (varAudioSink_) {
    if (varAudioSink_->state() == QAudio::ActiveState) {
      varAudioSink_->suspend();
      return;
    }
    if (varAudioSink_->state() == QAudio::SuspendedState) {
      varAudioSink_->resume();
      return;
    }
    varAudioSink_->stop();
    delete varAudioSink_;
    varAudioSink_ = nullptr;
  }

  if (varAudioBuffer_) {
    varAudioBuffer_->close();
    delete varAudioBuffer_;
    varAudioBuffer_ = nullptr;
  }

  const int chCount = std::min<int>(2, static_cast<int>(sig->channels.size()));
  const int dataFrames = static_cast<int>(sig->channels.front().samples.size());

  QAudioFormat fmt;
  const int sampleRate = sig->sampleRate > 0 ? sig->sampleRate : 22050;
  fmt.setSampleRate(sampleRate);
  fmt.setChannelCount(chCount);
  fmt.setSampleFormat(QAudioFormat::Int16);

  const int startOffsetFrames = std::max(0, static_cast<int>(std::llround(sig->startTimeSec * sampleRate)));
  const int totalFrames = startOffsetFrames + dataFrames;

  varPcmData_.resize(totalFrames * chCount * static_cast<int>(sizeof(qint16)));
  auto* out = reinterpret_cast<qint16*>(varPcmData_.data());
  for (int i = 0; i < totalFrames; ++i) {
    const int di = i - startOffsetFrames;
    for (int c = 0; c < chCount; ++c) {
      const auto& src = sig->channels[static_cast<size_t>(c)].samples;
      double v = 0.0;
      if (di >= 0 && di < static_cast<int>(src.size())) {
        v = std::clamp(src[static_cast<size_t>(di)], -1.0, 1.0);
      }
      *out++ = static_cast<qint16>(std::lrint(v * 32767.0));
    }
  }

  varAudioBuffer_ = new QBuffer(this);
  varAudioBuffer_->setData(varPcmData_);
  varAudioBuffer_->open(QIODevice::ReadOnly);

  varAudioSink_ = new QAudioSink(fmt, this);
  varAudioSink_->start(varAudioBuffer_);
}

void MainWindow::trackWindow(const QString& varName, QWidget* window, WindowKind kind) {
  ScopedWindow s;
  s.varName = varName;
  s.scope = engine_.activeContext();
  s.kind = kind;
  s.window = window;
  scopedWindows_.push_back(s);
  window->installEventFilter(this);
  connect(window, &QObject::destroyed, this, [this, window]() {
    if (lastFocusedScopedWindow_ == window) {
      lastFocusedScopedWindow_.clear();
    }
    if (prevFocusedScopedWindow_ == window) {
      prevFocusedScopedWindow_.clear();
    }
    reconcileScopedWindows();
  });

  reconcileScopedWindows();
}

SignalGraphWindow* MainWindow::findSignalGraphWindow(const QString& varName, auxContext* scope) const {
  for (auto it = scopedWindows_.rbegin(); it != scopedWindows_.rend(); ++it) {
    if (it->kind != WindowKind::Graph || it->scope != scope || it->varName != varName || !it->window) {
      continue;
    }
    if (auto* g = qobject_cast<SignalGraphWindow*>(it->window.data())) {
      return g;
    }
  }
  return nullptr;
}

void MainWindow::focusWindow(QWidget* window) const {
  if (!window) {
    return;
  }
  if (window->isMinimized()) {
    window->showNormal();
  } else {
    window->show();
  }
  window->raise();
  window->activateWindow();
}

void MainWindow::reconcileScopedWindows() {
  std::unordered_set<std::string> activeNames;
  for (const auto& v : engine_.listVariables()) {
    activeNames.insert(v.name);
  }

  const auto currentScope = engine_.activeContext();
  for (auto it = scopedWindows_.begin(); it != scopedWindows_.end();) {
    if (!it->window) {
      it = scopedWindows_.erase(it);
      continue;
    }

    // Close windows for variables removed from their own scope.
    if (it->scope == currentScope && activeNames.find(it->varName.toStdString()) == activeNames.end()) {
      if (it->window) {
        it->window->close();
      }
      it = scopedWindows_.erase(it);
      continue;
    }

    // Child scope ended: close child windows when main scope resumes.
    if (!engine_.isPaused() && it->scope != currentScope) {
      if (it->window) {
        it->window->close();
      }
      it = scopedWindows_.erase(it);
      continue;
    }

    if (auto* g = qobject_cast<SignalGraphWindow*>(it->window.data())) {
      g->setWorkspaceActive(it->scope == currentScope);
      if (it->scope == currentScope) {
        auto sig = engine_.getSignalData(it->varName.toStdString());
        if (sig) {
          g->updateData(*sig);
        }
      }
    } else {
      it->window->setEnabled(it->scope == currentScope);
    }

    ++it;
  }
}

std::vector<QWidget*> MainWindow::focusableScopedWindows(std::optional<WindowKind> kind) const {
  std::vector<QWidget*> out;
  const auto currentScope = engine_.activeContext();
  out.reserve(scopedWindows_.size());
  for (const auto& entry : scopedWindows_) {
    if (!entry.window || entry.scope != currentScope) {
      continue;
    }
    if (kind.has_value() && entry.kind != *kind) {
      continue;
    }
    out.push_back(entry.window.data());
  }
  return out;
}

void MainWindow::focusScopedWindowByOffset(int delta, std::optional<WindowKind> kind) {
  if (delta == 0) {
    return;
  }
  reconcileScopedWindows();
  const auto windows = focusableScopedWindows(kind);
  if (windows.empty()) {
    return;
  }

  QWidget* current = QApplication::activeWindow();
  auto currentIt = std::find(windows.begin(), windows.end(), current);
  if (currentIt == windows.end() && lastFocusedScopedWindow_) {
    currentIt = std::find(windows.begin(), windows.end(), lastFocusedScopedWindow_.data());
  }

  int currentIndex = 0;
  if (currentIt != windows.end()) {
    currentIndex = static_cast<int>(std::distance(windows.begin(), currentIt));
  }

  const int n = static_cast<int>(windows.size());
  const int next = ((currentIndex + delta) % n + n) % n;
  focusWindow(windows[static_cast<size_t>(next)]);
}

void MainWindow::focusScopedWindowByIndex(int oneBasedIndex) {
  if (oneBasedIndex < 1) {
    return;
  }
  reconcileScopedWindows();
  const auto windows = focusableScopedWindows();
  if (oneBasedIndex > static_cast<int>(windows.size())) {
    return;
  }
  focusWindow(windows[static_cast<size_t>(oneBasedIndex - 1)]);
}

void MainWindow::toggleLastTwoScopedWindows() {
  reconcileScopedWindows();
  if (!lastFocusedScopedWindow_ || !prevFocusedScopedWindow_) {
    return;
  }
  QWidget* active = QApplication::activeWindow();
  if (active == lastFocusedScopedWindow_.data()) {
    focusWindow(prevFocusedScopedWindow_.data());
  } else {
    focusWindow(lastFocusedScopedWindow_.data());
  }
}

void MainWindow::closeAllScopedWindowsInCurrentScope() {
  reconcileScopedWindows();
  const auto currentScope = engine_.activeContext();
  std::vector<QWidget*> toClose;
  toClose.reserve(scopedWindows_.size());
  for (const auto& entry : scopedWindows_) {
    if (entry.window && entry.scope == currentScope) {
      toClose.push_back(entry.window.data());
    }
  }
  for (QWidget* w : toClose) {
    if (w) {
      w->close();
    }
  }
  reconcileScopedWindows();
}

void MainWindow::noteScopedWindowFocus(QWidget* window) {
  if (!window) {
    return;
  }
  const auto currentScope = engine_.activeContext();
  auto it = std::find_if(scopedWindows_.begin(), scopedWindows_.end(), [window, currentScope](const ScopedWindow& entry) {
    return entry.window.data() == window && entry.scope == currentScope;
  });
  if (it == scopedWindows_.end()) {
    return;
  }
  if (lastFocusedScopedWindow_ == window) {
    return;
  }
  prevFocusedScopedWindow_ = lastFocusedScopedWindow_;
  lastFocusedScopedWindow_ = window;
}

bool MainWindow::variableSupportsSignalDisplay(const QString& varName) const {
  auto sig = engine_.getSignalData(varName.toStdString());
  return sig.has_value();
}

bool MainWindow::variableIsAudio(const QString& varName) const {
  auto sig = engine_.getSignalData(varName.toStdString());
  return sig.has_value() && sig->isAudio;
}

bool MainWindow::variableIsString(const QString& varName) const {
  return engine_.isStringVar(varName.toStdString());
}

bool MainWindow::variableIsBinary(const QString& varName) const {
  return engine_.isBinaryVar(varName.toStdString());
}

void MainWindow::handleDebugAction(auxDebugAction action) {
  engine_.debugResume(action);
  refreshVariables();
  refreshDebugView();
  reconcileScopedWindows();
}

void MainWindow::showSettingsDialog() {
  const RuntimeSettingsSnapshot cfg = engine_.runtimeSettings();
  QDialog dialog(this);
  dialog.setWindowTitle("Runtime Settings");
  dialog.resize(620, 460);

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();

  auto* sampleRateSpin = new QSpinBox(&dialog);
  sampleRateSpin->setRange(1, 384000);
  sampleRateSpin->setValue(std::max(1, cfg.sampleRate));

  auto* limitXSpin = new QSpinBox(&dialog);
  limitXSpin->setRange(0, 1000000);
  limitXSpin->setValue(std::max(0, cfg.displayLimitX));

  auto* limitYSpin = new QSpinBox(&dialog);
  limitYSpin->setRange(0, 1000000);
  limitYSpin->setValue(std::max(0, cfg.displayLimitY));

  auto* limitBytesSpin = new QSpinBox(&dialog);
  limitBytesSpin->setRange(0, 100000000);
  limitBytesSpin->setValue(std::max(0, cfg.displayLimitBytes));

  auto* limitStrSpin = new QSpinBox(&dialog);
  limitStrSpin->setRange(0, 100000000);
  limitStrSpin->setValue(std::max(0, cfg.displayLimitStr));

  auto* precisionSpin = new QSpinBox(&dialog);
  precisionSpin->setRange(0, 20);
  precisionSpin->setValue(std::max(0, cfg.displayPrecision));

  auto* udfPathsEdit = new QPlainTextEdit(&dialog);
  QStringList pathLines;
  for (const std::string& p : cfg.udfPaths) {
    pathLines.push_back(QString::fromStdString(p));
  }
  udfPathsEdit->setPlainText(pathLines.join("\n"));
  udfPathsEdit->setPlaceholderText("One path per line");

  form->addRow("Sampling Rate", sampleRateSpin);
  form->addRow("Display Limit X", limitXSpin);
  form->addRow("Display Limit Y", limitYSpin);
  form->addRow("Display Limit Bytes", limitBytesSpin);
  form->addRow("Display Limit String", limitStrSpin);
  form->addRow("Display Precision", precisionSpin);
  form->addRow("UDF Paths (one per line)", udfPathsEdit);
  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  RuntimeSettingsSnapshot next = cfg;
  next.sampleRate = sampleRateSpin->value();
  next.displayLimitX = limitXSpin->value();
  next.displayLimitY = limitYSpin->value();
  next.displayLimitBytes = limitBytesSpin->value();
  next.displayLimitStr = limitStrSpin->value();
  next.displayPrecision = precisionSpin->value();

  next.udfPaths.clear();
  QSet<QString> seen;
  const QStringList rawLines = udfPathsEdit->toPlainText().split('\n');
  for (QString line : rawLines) {
    line = line.trimmed();
    if (line.isEmpty() || seen.contains(line)) {
      continue;
    }
    seen.insert(line);
    next.udfPaths.push_back(line.toStdString());
  }

  std::string err;
  if (!engine_.applyRuntimeSettings(next, err)) {
    QMessageBox::warning(this, "Settings", QString::fromStdString(err));
    return;
  }

  savePersistedRuntimeSettings();
  statusBar()->showMessage("Runtime settings updated.", 2500);
}
