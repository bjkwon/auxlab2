#include "MainWindow.h"

#include "CommandConsole.h"
#include "SignalGraphWindow.h"
#include "SignalTableWindow.h"
#include "UdfDebugWindow.h"

#include <QAudioFormat>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSplitter>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
QString historyFilePath() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) {
    dir = QDir::homePath();
  }
  QDir d(dir);
  d.mkpath(".");
  return d.filePath("auxlab2.history");
}
}  // namespace

MainWindow::MainWindow() {
  if (!engine_.init()) {
    QMessageBox::critical(nullptr, "AUX", "Failed to initialize AUX engine.");
  }

  buildUi();
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

  variableBox_ = new QTreeWidget(this);
  variableBox_->setColumnCount(3);
  variableBox_->setHeaderLabels({"Variable", "Type", "Info"});
  variableBox_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  variableBox_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  variableBox_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  variableBox_->installEventFilter(this);

  historyBox_ = new QListWidget(this);
  historyBox_->setSelectionMode(QAbstractItemView::SingleSelection);
  historyBox_->installEventFilter(this);

  splitter->addWidget(commandBox_);
  splitter->addWidget(variableBox_);
  splitter->addWidget(historyBox_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  splitter->setStretchFactor(2, 2);

  layout->addWidget(splitter);
  setCentralWidget(central);

  debugWindow_ = new UdfDebugWindow(this);
  debugWindow_->show();
}

void MainWindow::connectSignals() {
  connect(commandBox_, &CommandConsole::commandSubmitted, this, &MainWindow::runCommand);
  connect(commandBox_, &CommandConsole::historyNavigateRequested, this, &MainWindow::navigateHistoryFromCommand);
  connect(commandBox_, &CommandConsole::reverseSearchRequested, this, &MainWindow::reverseSearchFromCommand);

  connect(historyBox_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (!item) {
      return;
    }
    injectCommandFromHistory(item->text(), true);
  });

  connect(variableBox_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) {
    openSignalTableForSelected();
  });

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
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveHistory();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
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

  if (watched == variableBox_ && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      openSignalGraphForSelected();
      return true;
    }
    if (ke->key() == Qt::Key_Space) {
      playSelectedAudioFromVarBox();
      return true;
    }
  }

  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::runCommand(const QString& cmd) {
  const QString actual = cmd;
  if (!actual.trimmed().isEmpty()) {
    addHistory(actual);
    auto result = engine_.eval(actual.toStdString());
    commandBox_->appendExecutionResult(QString::fromStdString(result.output));
    historyNavIndex_ = -1;
    historyDraft_.clear();
    reverseSearchActive_ = false;
    reverseSearchTerm_.clear();
    reverseSearchIndex_ = -1;
  } else {
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

QString MainWindow::selectedVarName() const {
  auto* item = variableBox_->currentItem();
  if (!item) {
    return {};
  }
  return item->text(0);
}

void MainWindow::refreshVariables() {
  variableBox_->clear();

  const auto vars = engine_.listVariables();
  for (const auto& v : vars) {
    auto* item = new QTreeWidgetItem(variableBox_);
    item->setText(0, QString::fromStdString(v.name));
    item->setText(1, QString("0x%1").arg(v.type, 4, 16, QLatin1Char('0')));
    item->setText(2, QString::fromStdString(v.preview));
  }
}

void MainWindow::refreshDebugView() {
  const bool paused = engine_.isPaused();
  debugWindow_->setPaused(paused);

  if (paused) {
    auto infoOpt = engine_.pauseInfo();
    if (infoOpt) {
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

  auto* w = new SignalGraphWindow(var, *sig);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(var, w, WindowKind::Graph);
  w->show();
}

void MainWindow::openSignalTableForSelected() {
  const QString var = selectedVarName();
  if (var.isEmpty() || !variableSupportsSignalDisplay(var)) {
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
  const int frames = static_cast<int>(sig->channels.front().samples.size());

  QAudioFormat fmt;
  fmt.setSampleRate(sig->sampleRate > 0 ? sig->sampleRate : 22050);
  fmt.setChannelCount(chCount);
  fmt.setSampleFormat(QAudioFormat::Int16);

  varPcmData_.resize(frames * chCount * static_cast<int>(sizeof(qint16)));
  auto* out = reinterpret_cast<qint16*>(varPcmData_.data());
  for (int i = 0; i < frames; ++i) {
    for (int c = 0; c < chCount; ++c) {
      const auto& src = sig->channels[static_cast<size_t>(c)].samples;
      const double v = i < static_cast<int>(src.size()) ? std::clamp(src[static_cast<size_t>(i)], -1.0, 1.0) : 0.0;
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

  reconcileScopedWindows();
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
    } else {
      it->window->setEnabled(it->scope == currentScope);
    }

    ++it;
  }
}

bool MainWindow::variableSupportsSignalDisplay(const QString& varName) const {
  auto sig = engine_.getSignalData(varName.toStdString());
  return sig.has_value();
}

bool MainWindow::variableIsAudio(const QString& varName) const {
  auto sig = engine_.getSignalData(varName.toStdString());
  return sig.has_value() && sig->isAudio;
}

void MainWindow::handleDebugAction(auxDebugAction action) {
  engine_.debugResume(action);
  refreshVariables();
  refreshDebugView();
  reconcileScopedWindows();
}
