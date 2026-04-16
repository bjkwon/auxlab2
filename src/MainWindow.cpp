#include "MainWindow.h"

#include "BinaryObjectWindow.h"
#include "CellMembersWindow.h"
#include "CommandConsole.h"
#include "BuildInfo.h"
#include "SignalGraphWindow.h"
#include "SignalTableWindow.h"
#include "StructMembersWindow.h"
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
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QScreen>
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
#include <limits>
#include <unordered_set>

namespace {
constexpr int kMaxRecentUdfFiles = 8;
constexpr int kHistoryCommandRole = Qt::UserRole + 1;
constexpr int kHistoryCountRole = Qt::UserRole + 2;
constexpr int kHistoryIsCommentRole = Qt::UserRole + 3;
constexpr uint16_t kDisplayTypebitHandle = 0x4000;

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

QString makeSessionBannerText() {
  return QString("// %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

QString variableDeleteShortcutText() {
#ifdef Q_OS_MAC
  return QStringLiteral("Cmd+Delete");
#else
  return QStringLiteral("Delete");
#endif
}

#ifdef Q_OS_MAC
constexpr Qt::KeyboardModifier kPrimaryWindowModifier = Qt::MetaModifier;
#else
constexpr Qt::KeyboardModifier kPrimaryWindowModifier = Qt::ControlModifier;
#endif

QKeySequence primaryWindowShortcut(Qt::Key key, Qt::KeyboardModifiers extra = Qt::NoModifier) {
  return QKeySequence(QKeyCombination(kPrimaryWindowModifier | extra, key));
}

QString graphicsHandleText(std::uint64_t id) {
  return id == 0 ? QStringLiteral("[]") : QString::number(id);
}

std::optional<SignalData> signalDataFromAuxObj(AuxObj obj, int defaultSampleRate) {
  if (!obj) {
    return std::nullopt;
  }

  const int channels = aux_num_channels(obj);
  if (channels <= 0) {
    return std::nullopt;
  }

  SignalData data;
  data.isAudio = aux_is_audio(obj);
  data.sampleRate = 0;
  double minStartMs = std::numeric_limits<double>::infinity();
  struct SegmentCopy {
    int startSample = 0;
    std::vector<double> samples;
  };
  std::vector<std::vector<SegmentCopy>> byChannel(static_cast<size_t>(channels));

  for (int ch = 0; ch < channels; ++ch) {
    const int segCount = aux_num_segments(obj, ch);
    for (int segIndex = 0; segIndex < segCount; ++segIndex) {
      AuxSignal seg{};
      if (!aux_get_segment(obj, ch, segIndex, seg)) {
        continue;
      }
      minStartMs = std::min(minStartMs, seg.tmark);
      if (seg.fs > 0) {
        data.sampleRate = seg.fs;
      }
      SegmentCopy copy;
      copy.samples.resize(seg.nSamples);
      if (seg.buf && seg.nSamples > 0) {
        std::copy(seg.buf, seg.buf + seg.nSamples, copy.samples.begin());
      }
      byChannel[static_cast<size_t>(ch)].push_back(std::move(copy));
    }
  }

  if (!std::isfinite(minStartMs)) {
    return std::nullopt;
  }
  if (data.sampleRate <= 0) {
    data.sampleRate = defaultSampleRate > 0 ? defaultSampleRate : 1;
  }

  int globalTotalSamples = 0;
  for (int ch = 0; ch < channels; ++ch) {
    const int segCount = aux_num_segments(obj, ch);
    for (int segIndex = 0; segIndex < segCount; ++segIndex) {
      AuxSignal seg{};
      if (!aux_get_segment(obj, ch, segIndex, seg)) {
        continue;
      }
      const int startSample = std::max(0, static_cast<int>(std::llround((seg.tmark - minStartMs) * data.sampleRate / 1000.0)));
      byChannel[static_cast<size_t>(ch)][static_cast<size_t>(segIndex)].startSample = startSample;
      globalTotalSamples = std::max(globalTotalSamples, startSample + static_cast<int>(seg.nSamples));
    }
  }

  if (globalTotalSamples <= 0) {
    return std::nullopt;
  }

  data.channels.reserve(static_cast<size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
    ChannelData channel;
    channel.samples.assign(static_cast<size_t>(globalTotalSamples), 0.0);
    for (const auto& seg : byChannel[static_cast<size_t>(ch)]) {
      if (seg.samples.empty()) {
        continue;
      }
      const size_t start = static_cast<size_t>(std::max(0, seg.startSample));
      if (start >= channel.samples.size()) {
        continue;
      }
      const size_t count = std::min(seg.samples.size(), channel.samples.size() - start);
      std::copy_n(seg.samples.begin(), count, channel.samples.begin() + static_cast<qsizetype>(start));
    }
    data.channels.push_back(std::move(channel));
  }

  if (data.isAudio && std::isfinite(minStartMs)) {
    data.startTimeSec = minStartMs / 1000.0;
  }
  return data;
}

std::optional<QVector<double>> numericVectorFromAuxObj(AuxObj obj) {
  if (!obj) {
    return std::nullopt;
  }
  if (aux_is_audio(obj)) {
    return std::nullopt;
  }
  const size_t len = aux_vector_length(obj);
  if (len == 0) {
    return std::nullopt;
  }
  QVector<double> values(static_cast<qsizetype>(len));
  if (aux_copy_vector(obj, values.data(), len) != len) {
    return std::nullopt;
  }
  return values;
}

QByteArray buildAudioPcm16(const SignalData& sig, int& outChannelCount, int& outTotalFrames) {
  outChannelCount = std::min<int>(2, static_cast<int>(sig.channels.size()));
  const int dataFrames = sig.channels.empty() ? 0 : static_cast<int>(sig.channels.front().samples.size());
  const int sampleRate = sig.sampleRate > 0 ? sig.sampleRate : 22050;
  const int startOffsetFrames = std::max(0, static_cast<int>(std::llround(sig.startTimeSec * sampleRate)));
  outTotalFrames = startOffsetFrames + dataFrames;

  QByteArray pcm;
  pcm.resize(outTotalFrames * outChannelCount * static_cast<int>(sizeof(qint16)));
  auto* out = reinterpret_cast<qint16*>(pcm.data());
  for (int i = 0; i < outTotalFrames; ++i) {
    const int di = i - startOffsetFrames;
    for (int c = 0; c < outChannelCount; ++c) {
      const auto& src = sig.channels[static_cast<size_t>(c)].samples;
      double v = 0.0;
      if (di >= 0 && di < static_cast<int>(src.size())) {
        v = std::clamp(src[static_cast<size_t>(di)], -1.0, 1.0);
      }
      *out++ = static_cast<qint16>(std::lrint(v * 32767.0));
    }
  }
  return pcm;
}

QString nextGraphicsTempName() {
  static int counter = 1;
  return QString("__auxlab2_graphics_tmp_%1").arg(counter++);
}

bool parseMatlabPosVector(const QString& text, std::array<double, 4>& out) {
  QString t = text.trimmed();
  if (!t.startsWith('[') || !t.endsWith(']')) {
    return false;
  }
  t = t.mid(1, t.size() - 2).trimmed();
  const QStringList parts = t.split(QRegularExpression(R"([\s,;]+)"), Qt::SkipEmptyParts);
  if (parts.size() != 4) {
    return false;
  }
  bool ok = false;
  for (int i = 0; i < 4; ++i) {
    out[static_cast<size_t>(i)] = parts[i].toDouble(&ok);
    if (!ok) {
      return false;
    }
  }
  return true;
}

QRect matlabFigureRectToQt(const std::array<double, 4>& pos) {
  const QRect screen = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1440, 900);
  const int x = static_cast<int>(std::llround(pos[0]));
  const int width = static_cast<int>(std::llround(pos[2]));
  const int height = static_cast<int>(std::llround(pos[3]));
  const int yBottom = static_cast<int>(std::llround(pos[1]));
  const int yTop = screen.y() + screen.height() - yBottom - height;
  return QRect(x, yTop, width, height);
}

QStringList splitTopLevelArgs(const QString& text) {
  QStringList out;
  QString current;
  int paren = 0;
  int bracket = 0;
  bool inString = false;
  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text[i];
    if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
      inString = !inString;
      current += ch;
      continue;
    }
    if (!inString) {
      if (ch == '(') ++paren;
      else if (ch == ')') --paren;
      else if (ch == '[') ++bracket;
      else if (ch == ']') --bracket;
      else if (ch == ',' && paren == 0 && bracket == 0) {
        out.push_back(current.trimmed());
        current.clear();
        continue;
      }
    }
    current += ch;
  }
  if (!current.trimmed().isEmpty()) {
    out.push_back(current.trimmed());
  }
  return out;
}

bool isSimpleIdentifier(const QString& text) {
  static const QRegularExpression kIdentPattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
  return kIdentPattern.match(text.trimmed()).hasMatch();
}

bool isQuotedStringLiteral(const QString& text) {
  const QString t = text.trimmed();
  return t.size() >= 2 && t.startsWith('"') && t.endsWith('"');
}

QString unquoteStringLiteral(const QString& text) {
  const QString t = text.trimmed();
  if (!isQuotedStringLiteral(t)) {
    return t;
  }
  return t.mid(1, t.size() - 2);
}

struct PlotStyleSpec {
  bool valid = true;
  bool hasColor = false;
  QColor color;
  QString marker;
  QString lineStyle = "-";
};

PlotStyleSpec parsePlotStyleString(const QString& text) {
  PlotStyleSpec spec;
  QString s = text.trimmed();
  int idx = 0;

  auto parseColor = [&](QChar ch) -> bool {
    switch (ch.toLatin1()) {
      case 'r': spec.color = Qt::red; return true;
      case 'g': spec.color = QColor(0, 160, 0); return true;
      case 'b': spec.color = Qt::blue; return true;
      case 'y': spec.color = Qt::yellow; return true;
      case 'c': spec.color = Qt::cyan; return true;
      case 'm': spec.color = Qt::magenta; return true;
      case 'h': spec.color = Qt::white; return true;
      case 'k': spec.color = Qt::black; return true;
      default: return false;
    }
  };

  if (idx < s.size() && parseColor(s[idx])) {
    spec.hasColor = true;
    ++idx;
  }

  if (idx < s.size()) {
    const QChar ch = s[idx];
    const QString markers = QStringLiteral("o+*.xsd^v<>ph");
    if (markers.contains(ch) || ch == ' ') {
      spec.marker = (ch == ' ') ? QString() : QString(ch);
      ++idx;
    }
  }

  const QString rest = s.mid(idx);
  if (rest.isEmpty()) {
    if (!spec.marker.isEmpty()) {
      spec.lineStyle = QStringLiteral("none");
    }
    return spec;
  }

  if (rest == "-" || rest == "--" || rest == ":" || rest == "-." || rest == "none") {
    spec.lineStyle = rest;
    return spec;
  }

  spec.valid = false;
  return spec;
}

std::optional<QVector<double>> extractRangeXData(const QString& expr, int expectedLength) {
  if (expectedLength <= 0) {
    return std::nullopt;
  }

  static const QRegularExpression kRange3(R"(([-+]?\d+(?:\.\d+)?)\s*:\s*([-+]?\d+(?:\.\d+)?)\s*:\s*([-+]?\d+(?:\.\d+)?))");
  static const QRegularExpression kRange2(R"(([-+]?\d+(?:\.\d+)?)\s*:\s*([-+]?\d+(?:\.\d+)?))");

  std::optional<QVector<double>> result;

  auto tryBuild = [expectedLength](double start, double step, double end) -> std::optional<QVector<double>> {
    if (std::fabs(step) < 1e-12) {
      return std::nullopt;
    }
    QVector<double> values;
    values.reserve(expectedLength);

    const double eps = std::max(1e-9, std::fabs(step) * 1e-9);
    if (step > 0) {
      for (double v = start; v <= end + eps && values.size() < expectedLength; v += step) {
        values.push_back(v);
      }
    } else {
      for (double v = start; v >= end - eps && values.size() < expectedLength; v += step) {
        values.push_back(v);
      }
    }

    if (values.size() != expectedLength) {
      return std::nullopt;
    }
    return values;
  };

  auto it3 = kRange3.globalMatch(expr);
  while (it3.hasNext()) {
    const auto match = it3.next();
    bool ok1 = false, ok2 = false, ok3 = false;
    const double start = match.captured(1).toDouble(&ok1);
    const double step = match.captured(2).toDouble(&ok2);
    const double end = match.captured(3).toDouble(&ok3);
    if (ok1 && ok2 && ok3) {
      if (auto values = tryBuild(start, step, end)) {
        result = values;
      }
    }
  }
  if (result.has_value()) {
    return result;
  }

  auto it2 = kRange2.globalMatch(expr);
  while (it2.hasNext()) {
    const auto match = it2.next();
    bool ok1 = false, ok2 = false;
    const double start = match.captured(1).toDouble(&ok1);
    const double end = match.captured(2).toDouble(&ok2);
    if (ok1 && ok2) {
      const double step = (end >= start) ? 1.0 : -1.0;
      if (auto values = tryBuild(start, step, end)) {
        result = values;
      }
    }
  }

  return result;
}

QString formatDoubleCompact(double value) {
  const double rounded = std::round(value);
  if (std::fabs(value - rounded) < 1e-9) {
    return QString::number(static_cast<long long>(rounded));
  }
  return QString::number(value, 'g', 8);
}

QString formatDoubleVector(const QVector<double>& values) {
  QStringList parts;
  parts.reserve(values.size());
  for (double value : values) {
    parts.push_back(formatDoubleCompact(value));
  }
  return QString("[%1]").arg(parts.join(' '));
}

QString formatDoubleArray2(const std::array<double, 2>& values) {
  return QString("[%1 %2]").arg(formatDoubleCompact(values[0]), formatDoubleCompact(values[1]));
}

QString formatDoubleArray4(const std::array<double, 4>& values) {
  return QString("[%1 %2 %3 %4]").arg(formatDoubleCompact(values[0]),
                                      formatDoubleCompact(values[1]),
                                      formatDoubleCompact(values[2]),
                                      formatDoubleCompact(values[3]));
}

QString formatChildren(const std::vector<std::uint64_t>& children) {
  if (children.empty()) {
    return QStringLiteral("[]");
  }
  QStringList parts;
  parts.reserve(static_cast<int>(children.size()));
  for (std::uint64_t child : children) {
    parts.push_back(QString::number(child));
  }
  return QString("[%1]").arg(parts.join(' '));
}

QString formatColor(const QColor& color) {
  return QString("[%1 %2 %3]")
      .arg(formatDoubleCompact(color.redF()))
      .arg(formatDoubleCompact(color.greenF()))
      .arg(formatDoubleCompact(color.blueF()));
}

bool parseDoubleVectorExpr(const QString& text, QVector<double>& out) {
  QString t = text.trimmed();
  if (!t.startsWith('[') || !t.endsWith(']')) {
    return false;
  }
  t = t.mid(1, t.size() - 2).trimmed();
  const QStringList parts = t.split(QRegularExpression(R"([\s,;]+)"), Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    out.clear();
    return true;
  }
  QVector<double> values;
  values.reserve(parts.size());
  for (const QString& part : parts) {
    bool ok = false;
    const double value = part.toDouble(&ok);
    if (!ok) {
      return false;
    }
    values.push_back(value);
  }
  out = values;
  return true;
}

bool parseBoolExpr(const QString& text, bool& value) {
  const QString t = text.trimmed().toLower();
  if (t == "true" || t == "1") {
    value = true;
    return true;
  }
  if (t == "false" || t == "0") {
    value = false;
    return true;
  }
  return false;
}

std::optional<SignalData> signalDataFromNumericDisplay(const QString& text) {
  QVector<double> values;
  if (parseDoubleVectorExpr(text, values)) {
    SignalData data;
    ChannelData channel;
    channel.samples.reserve(values.size());
    for (double v : values) {
      channel.samples.push_back(v);
    }
    data.channels.push_back(std::move(channel));
    return data;
  }

  bool ok = false;
  const double scalar = text.trimmed().toDouble(&ok);
  if (!ok) {
    return std::nullopt;
  }

  SignalData data;
  ChannelData channel;
  channel.samples.push_back(scalar);
  data.channels.push_back(std::move(channel));
  return data;
}

int auxlab2GraphicsNotify(void* userdata, const auxGraphicsEvent& event, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 1;
  }
  return window->handleGraphicsBackendEvent(event, errstr) ? 0 : 1;
}

std::uint64_t auxlab2CurrentFigureId(void* userdata) {
  auto* window = static_cast<MainWindow*>(userdata);
  return window ? window->currentGraphicsFigureId() : 0;
}

std::uint64_t auxlab2CurrentAxesId(void* userdata) {
  auto* window = static_cast<MainWindow*>(userdata);
  return window ? window->currentGraphicsAxesId() : 0;
}

std::uint64_t auxlab2CreateFigure(void* userdata, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsFigure(errstr);
}

std::uint64_t auxlab2FigureFromHandle(void* userdata, std::uint64_t handleId, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsFigureFromHandle(handleId, errstr);
}

std::uint64_t auxlab2FigureAtPos(void* userdata, const double pos[4], std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  if (!pos) {
    errstr = "figure() requires a position vector.";
    return 0;
  }
  return window->createGraphicsFigureAtPos({pos[0], pos[1], pos[2], pos[3]}, errstr);
}

std::uint64_t auxlab2NamedFigure(void* userdata, const char* sourceName, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  if (!sourceName || !*sourceName) {
    errstr = "figure() requires a non-empty source name.";
    return 0;
  }
  return window->createGraphicsNamedFigure(sourceName, errstr);
}

std::uint64_t auxlab2Plot(void* userdata,
                          std::uint64_t targetHandleId,
                          AuxObj obj,
                          const char* sourceExpr,
                          const char* styleText,
                          std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsPlot(targetHandleId,
                                    obj,
                                    sourceExpr ? std::string(sourceExpr) : std::string(),
                                    styleText ? std::string(styleText) : std::string(),
                                    errstr);
}

std::uint64_t auxlab2Line(void* userdata,
                          std::uint64_t targetHandleId,
                          AuxObj xObj,
                          AuxObj yObj,
                          std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsLine(targetHandleId, xObj, yObj, errstr);
}

std::uint64_t auxlab2CreateAxes(void* userdata, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsAxes(errstr);
}

std::uint64_t auxlab2AxesFromHandle(void* userdata, std::uint64_t handleId, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->createGraphicsAxesFromHandle(handleId, errstr);
}

std::uint64_t auxlab2AxesAtPos(void* userdata, const double pos[4], std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  if (!pos) {
    errstr = "axes() requires a position vector.";
    return 0;
  }
  return window->createGraphicsAxesAtPos({pos[0], pos[1], pos[2], pos[3]}, errstr);
}

int auxlab2DeleteHandle(void* userdata, std::uint64_t handleId, std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 graphics backend has no MainWindow owner.";
    return 0;
  }
  return window->deleteGraphicsHandle(handleId, errstr) ? 1 : 0;
}

int auxlab2StartPlayback(void* userdata,
                         std::uint64_t handleId,
                         AuxObj obj,
                         int repeatCount,
                         int reuseExistingHandle,
                         std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 playback backend has no MainWindow owner.";
    return 0;
  }
  return window->startPlaybackHandle(handleId, obj, repeatCount, reuseExistingHandle != 0, errstr) ? 1 : 0;
}

int auxlab2ControlPlayback(void* userdata,
                           std::uint64_t handleId,
                           auxPlaybackCommand command,
                           std::string& errstr) {
  auto* window = static_cast<MainWindow*>(userdata);
  if (!window) {
    errstr = "auxlab2 playback backend has no MainWindow owner.";
    return 0;
  }
  return window->controlPlaybackHandle(handleId, command, errstr) ? 1 : 0;
}
}  // namespace

MainWindow::MainWindow() {
  if (!engine_.init()) {
    QMessageBox::critical(nullptr, "AUX", "Failed to initialize AUX engine.");
  } else {
    auxGraphicsBackend backend;
    backend.userdata = this;
    backend.notify = &auxlab2GraphicsNotify;
    backend.current_figure = &auxlab2CurrentFigureId;
    backend.current_axes = &auxlab2CurrentAxesId;
    backend.create_figure = &auxlab2CreateFigure;
    backend.figure_from_handle = &auxlab2FigureFromHandle;
    backend.figure_at_pos = &auxlab2FigureAtPos;
    backend.named_figure = &auxlab2NamedFigure;
    backend.plot = &auxlab2Plot;
    backend.line = &auxlab2Line;
    backend.create_axes = &auxlab2CreateAxes;
    backend.axes_from_handle = &auxlab2AxesFromHandle;
    backend.axes_at_pos = &auxlab2AxesAtPos;
    backend.delete_handle = &auxlab2DeleteHandle;
    std::string err;
    if (!engine_.installGraphicsBackend(backend, err)) {
      QMessageBox::warning(this, "AUX Graphics", QString::fromStdString(err));
    }

    auxPlaybackBackend playbackBackend;
    playbackBackend.userdata = this;
    playbackBackend.start = &auxlab2StartPlayback;
    playbackBackend.control = &auxlab2ControlPlayback;
    if (!engine_.installPlaybackBackend(playbackBackend, err)) {
      QMessageBox::warning(this, "AUX Playback", QString::fromStdString(err));
    }
  }

  loadPersistedRuntimeSettings();
  buildUi();
  loadRecentUdfFiles();
  buildMenus();
  connectSignals();
  loadPersistedWindowLayout();
  loadHistory();
  refreshVariables();
  refreshDebugView();
  asyncPollTimer_ = new QTimer(this);
  asyncPollTimer_->setInterval(300);
  connect(asyncPollTimer_, &QTimer::timeout, this, &MainWindow::onAsyncPollTick);
  asyncPollTimer_->start();
  statusBar()->showMessage(
      QString("%1 v%2 (%3)").arg(AUXLAB2_APP_NAME).arg(AUXLAB2_VERSION).arg(AUXLAB2_GIT_HASH),
      5000);
}

MainWindow::~MainWindow() {
  engine_.clearGraphicsBackend();
  engine_.clearPlaybackBackend();
  if (varAudioSink_) {
    varAudioSink_->stop();
  }
  for (auto& entry : playbackSessions_) {
    if (entry.second.sink) {
      entry.second.sink->stop();
    }
    if (entry.second.buffer) {
      entry.second.buffer->close();
    }
  }
}

bool MainWindow::handleGraphicsBackendEvent(const auxGraphicsEvent& event, std::string& err) {
  Q_UNUSED(err);
  switch (event.kind) {
    case auxGraphicsEventKind::AUX_GRAPHICS_OBJECT_CREATED:
    case auxGraphicsEventKind::AUX_GRAPHICS_OBJECT_DELETED:
    case auxGraphicsEventKind::AUX_GRAPHICS_PROPERTY_CHANGED:
    case auxGraphicsEventKind::AUX_GRAPHICS_CURRENT_FIGURE_CHANGED:
    case auxGraphicsEventKind::AUX_GRAPHICS_CURRENT_AXES_CHANGED:
    case auxGraphicsEventKind::AUX_GRAPHICS_NAMED_PLOT_SOURCE_UPDATED:
      reconcileScopedWindows();
      return true;
  }
  return true;
}

std::uint64_t MainWindow::currentGraphicsFigureId() const {
  return graphicsManager_.currentFigureId();
}

std::uint64_t MainWindow::currentGraphicsAxesId() const {
  return graphicsManager_.currentAxesId();
}

std::uint64_t MainWindow::createGraphicsFigure(std::string& err) {
  auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
  if (!window) {
    err = "Failed to create figure window.";
    return 0;
  }
  return window->graphicsModel().figure().common.id;
}

std::uint64_t MainWindow::createGraphicsFigureFromHandle(std::uint64_t handleId, std::string& err) {
  if (handleId == 0) {
    err = "Error: invalid figure argument: 0";
    return 0;
  }

  auto* figure = graphicsManager_.findFigureById(handleId);
  if (!figure) {
    err = "Error: figure handle not found: " + std::to_string(handleId);
    return 0;
  }

  focusWindow(figure);
  graphicsManager_.markFocused(figure);
  return figure->graphicsModel().figure().common.id;
}

std::uint64_t MainWindow::createGraphicsFigureAtPos(const std::array<double, 4>& pos, std::string& err) {
  auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), matlabFigureRectToQt(pos));
  if (!window) {
    err = "Failed to create figure window.";
    return 0;
  }
  return window->graphicsModel().figure().common.id;
}

std::uint64_t MainWindow::createGraphicsNamedFigure(const std::string& sourcePath, std::string& err) {
  const QString path = QString::fromStdString(sourcePath);
  if (path.isEmpty() || !variableSupportsSignalDisplay(path)) {
    err = "Error: variable not plottable: " + sourcePath;
    return 0;
  }

  if (auto* existing = graphicsManager_.findNamedFigure(path)) {
    focusWindow(existing);
    graphicsManager_.markFocused(existing);
    return existing->graphicsModel().figure().common.id;
  }

  openSignalGraphForPath(path);
  if (auto* current = graphicsManager_.currentFigureWindow()) {
    return current->graphicsModel().figure().common.id;
  }

  err = "Failed to create named figure.";
  return 0;
}

std::uint64_t MainWindow::createGraphicsPlot(std::uint64_t targetHandleId,
                                             AuxObj obj,
                                             const std::string& sourceExpr,
                                             const std::string& styleText,
                                             std::string& err) {
  auto sig = signalDataFromAuxObj(obj, engine_.runtimeSettings().sampleRate);
  if (!sig) {
    err = "Error: plot argument is not plottable.";
    return 0;
  }

  SignalGraphWindow* targetWindow = nullptr;
  if (targetHandleId != 0) {
    targetWindow = graphicsManager_.findFigureById(targetHandleId);
    if (!targetWindow) {
      if (auto* owner = graphicsManager_.findAxesOwner(targetHandleId)) {
        owner->selectAxes(targetHandleId);
        targetWindow = owner;
      }
    }
    if (!targetWindow) {
      err = "Error: figure/axes handle not found: " + std::to_string(targetHandleId);
      return 0;
    }
  }

  if (!targetWindow) {
    targetWindow = createSignalFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), *sig, false, QString(), false);
  } else {
    targetWindow->updateData(*sig);
    focusWindow(targetWindow);
    graphicsManager_.markFocused(targetWindow);
  }

  const QString expr = QString::fromStdString(sourceExpr);
  if (!expr.isEmpty() && !sig->isAudio && !sig->channels.empty()) {
    const int expectedLength = static_cast<int>(sig->channels.front().samples.size());
    if (const auto xdata = extractRangeXData(expr, expectedLength); xdata.has_value()) {
      targetWindow->applyXDataToAllLines(*xdata);
    }
  }

  const QString styleArg = QString::fromStdString(styleText);
  if (!styleArg.isEmpty()) {
    const PlotStyleSpec styleSpec = parsePlotStyleString(styleArg);
    if (!styleSpec.valid) {
      err = "Error: invalid plot style: \"" + styleText + "\"";
      return 0;
    }
    targetWindow->applyStyleToAllLines(styleSpec.hasColor ? std::optional<QColor>(styleSpec.color) : std::nullopt,
                                       styleSpec.marker,
                                       styleSpec.lineStyle);
  }

  return targetWindow ? targetWindow->graphicsModel().figure().common.id : 0;
}

std::uint64_t MainWindow::createGraphicsLine(std::uint64_t targetHandleId,
                                             AuxObj xObj,
                                             AuxObj yObj,
                                             std::string& err) {
  QVector<double> xVals;
  QVector<double> yVals;

  if (yObj == nullptr) {
    auto maybeY = numericVectorFromAuxObj(xObj);
    if (!maybeY.has_value()) {
      err = "Error: line data is not a numeric vector.";
      return 0;
    }
    yVals = *maybeY;
    xVals.reserve(yVals.size());
    for (int i = 0; i < yVals.size(); ++i) {
      xVals.push_back(i + 1);
    }
  } else {
    auto maybeX = numericVectorFromAuxObj(xObj);
    auto maybeY = numericVectorFromAuxObj(yObj);
    if (!maybeX.has_value()) {
      err = "Error: line x data is not a numeric vector.";
      return 0;
    }
    if (!maybeY.has_value()) {
      err = "Error: line data is not a numeric vector.";
      return 0;
    }
    xVals = *maybeX;
    yVals = *maybeY;
    if (xVals.size() != yVals.size()) {
      err = "Error: x and y for line() must have the same length.";
      return 0;
    }
  }

  SignalGraphWindow* targetWindow = nullptr;
  std::uint64_t targetAxesId = 0;
  if (targetHandleId != 0) {
    if (auto* owner = graphicsManager_.findAxesOwner(targetHandleId)) {
      owner->selectAxes(targetHandleId);
      targetWindow = owner;
      targetAxesId = targetHandleId;
    } else if (auto* figure = graphicsManager_.findFigureById(targetHandleId)) {
      targetWindow = figure;
      if (const auto* currentAxes = figure->graphicsModel().currentAxes()) {
        targetAxesId = currentAxes->common.id;
      } else {
        targetAxesId = figure->addAxes({0.08, 0.18, 0.86, 0.72});
      }
    } else {
      err = "Error: figure/axes handle not found: " + std::to_string(targetHandleId);
      return 0;
    }
  }

  if (!targetWindow) {
    SignalData emptyData;
    targetWindow = createSignalFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), emptyData, false, QString(), false);
    if (!targetWindow) {
      err = "Failed to create figure window for line().";
      return 0;
    }
    if (const auto* currentAxes = targetWindow->graphicsModel().currentAxes()) {
      targetAxesId = currentAxes->common.id;
    } else {
      targetAxesId = targetWindow->addAxes({0.08, 0.18, 0.86, 0.72});
    }
  }

  const auto lineId = targetWindow->addLine(targetAxesId, xVals, yVals);
  if (lineId == 0) {
    err = "Error: failed to create line object.";
    return 0;
  }
  focusWindow(targetWindow);
  graphicsManager_.markFocused(targetWindow);
  return lineId;
}

std::uint64_t MainWindow::createGraphicsAxes(std::string& err) {
  SignalGraphWindow* window = graphicsManager_.currentFigureWindow();
  if (!window) {
    window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
  }
  if (!window) {
    err = "Failed to create figure window for axes().";
    return 0;
  }
  const auto axesId = window->addAxes({0.08, 0.18, 0.86, 0.72});
  if (axesId == 0) {
    err = "Failed to create axes.";
    return 0;
  }
  window->selectAxes(axesId);
  focusWindow(window);
  graphicsManager_.markFocused(window);
  return axesId;
}

std::uint64_t MainWindow::createGraphicsAxesFromHandle(std::uint64_t handleId, std::string& err) {
  if (handleId == 0) {
    err = "Error: invalid axes argument: 0";
    return 0;
  }

  if (auto* owner = graphicsManager_.findAxesOwner(handleId)) {
    owner->selectAxes(handleId);
    graphicsManager_.markFocused(owner);
    focusWindow(owner);
    return handleId;
  }

  if (auto* figure = graphicsManager_.findFigureById(handleId)) {
    const auto axesId = figure->addAxes({0.08, 0.18, 0.86, 0.72});
    if (axesId == 0) {
      err = "Failed to create axes.";
      return 0;
    }
    figure->selectAxes(axesId);
    graphicsManager_.markFocused(figure);
    focusWindow(figure);
    return axesId;
  }

  err = "Error: figure/axes handle not found: " + std::to_string(handleId);
  return 0;
}

std::uint64_t MainWindow::createGraphicsAxesAtPos(const std::array<double, 4>& pos, std::string& err) {
  SignalGraphWindow* window = graphicsManager_.currentFigureWindow();
  if (!window) {
    window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
  }
  if (!window) {
    err = "Failed to create figure window for axes().";
    return 0;
  }
  const auto axesId = window->addAxes(pos);
  if (axesId == 0) {
    err = "Failed to create axes.";
    return 0;
  }
  window->selectAxes(axesId);
  graphicsManager_.markFocused(window);
  focusWindow(window);
  return axesId;
}

bool MainWindow::deleteGraphicsHandle(std::uint64_t handleId, std::string& err) {
  if (handleId == 0) {
    err = "Error: delete() requires a graphics handle.";
    return false;
  }

  if (auto* figure = graphicsManager_.findFigureById(handleId)) {
    const bool deletingCurrent = (graphicsManager_.currentFigureWindow() == figure);
    if (deletingCurrent) {
      graphicsManager_.clearCurrentWindow(figure);
    }
    figure->close();
    return true;
  }

  if (auto* owner = graphicsManager_.findAxesOwner(handleId)) {
    const bool deletingCurrentAxes = (graphicsManager_.currentFigureWindow() == owner &&
                                      graphicsManager_.currentAxesId() == handleId);
    if (!owner->removeAxes(handleId)) {
      err = "Error: graphics handle not found: " + std::to_string(handleId);
      return false;
    }
    if (deletingCurrentAxes) {
      graphicsManager_.markFocused(owner);
    }
    return true;
  }

  for (auto it = scopedWindows_.rbegin(); it != scopedWindows_.rend(); ++it) {
    if (it->kind != WindowKind::Graph || !it->window) {
      continue;
    }
    if (auto* g = qobject_cast<SignalGraphWindow*>(it->window.data())) {
      if (g->graphicsModel().containsLine(handleId)) {
        if (!g->removeLine(handleId)) {
          err = "Error: graphics handle not found: " + std::to_string(handleId);
          return false;
        }
        return true;
      }
      if (g->graphicsModel().containsText(handleId)) {
        if (!g->removeText(handleId)) {
          err = "Error: graphics handle not found: " + std::to_string(handleId);
          return false;
        }
        return true;
      }
    }
  }

  err = "Error: graphics handle not found: " + std::to_string(handleId);
  return false;
}

void MainWindow::buildUi() {
  setWindowTitle(QString("%1 v%2").arg(AUXLAB2_APP_NAME).arg(AUXLAB2_VERSION));
  resize(1200, 760);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);

  auto* splitter = new QSplitter(this);
  mainSplitter_ = splitter;

  commandBox_ = new CommandConsole(this);

  auto* variablePanel = new QWidget(this);
  auto* variableLayout = new QVBoxLayout(variablePanel);
  variableLayout->setContentsMargins(0, 0, 0, 0);

  auto* variableSectionSplitter = new QSplitter(Qt::Vertical, variablePanel);
  variableSectionSplitter_ = variableSectionSplitter;

  auto* audioSection = new QWidget(variableSectionSplitter);
  auto* audioLayout = new QVBoxLayout(audioSection);
  audioLayout->setContentsMargins(0, 0, 0, 0);
  audioLayout->addWidget(new QLabel("Audio Objects", audioSection));
  audioVariableBox_ = new QTreeWidget(audioSection);
  audioVariableBox_->setColumnCount(4);
  audioVariableBox_->setHeaderLabels({"Name", "dBRMS", "Size", "Signal Intervals (ms)"});
  audioVariableBox_->header()->setSectionResizeMode(QHeaderView::Interactive);
  audioVariableBox_->header()->setStretchLastSection(false);
  audioVariableBox_->setColumnWidth(0, 180);
  audioVariableBox_->setColumnWidth(1, 90);
  audioVariableBox_->setColumnWidth(2, 120);
  audioVariableBox_->setColumnWidth(3, 360);
  audioVariableBox_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  audioVariableBox_->setContextMenuPolicy(Qt::CustomContextMenu);
  audioVariableBox_->installEventFilter(this);
  audioLayout->addWidget(audioVariableBox_);

  auto* nonAudioSection = new QWidget(variableSectionSplitter);
  auto* nonAudioLayout = new QVBoxLayout(nonAudioSection);
  nonAudioLayout->setContentsMargins(0, 0, 0, 0);
  nonAudioLayout->addWidget(new QLabel("Non-Audio Objects", nonAudioSection));
  nonAudioVariableBox_ = new QTreeWidget(nonAudioSection);
  nonAudioVariableBox_->setColumnCount(4);
  nonAudioVariableBox_->setHeaderLabels({"Name", "Type", "Size", "Content"});
  nonAudioVariableBox_->header()->setSectionResizeMode(QHeaderView::Interactive);
  nonAudioVariableBox_->header()->setStretchLastSection(false);
  nonAudioVariableBox_->setColumnWidth(0, 180);
  nonAudioVariableBox_->setColumnWidth(1, 90);
  nonAudioVariableBox_->setColumnWidth(2, 120);
  nonAudioVariableBox_->setColumnWidth(3, 360);
  nonAudioVariableBox_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  nonAudioVariableBox_->setContextMenuPolicy(Qt::CustomContextMenu);
  nonAudioVariableBox_->installEventFilter(this);
  nonAudioLayout->addWidget(nonAudioVariableBox_);

  variableSectionSplitter->addWidget(audioSection);
  variableSectionSplitter->addWidget(nonAudioSection);
  variableSectionSplitter->setStretchFactor(0, 1);
  variableSectionSplitter->setStretchFactor(1, 1);
  variableSectionSplitter->setChildrenCollapsible(false);
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

  udfFileWatcher_ = new QFileSystemWatcher(this);
}

void MainWindow::buildMenus() {
  auto* fileMenu = menuBar()->addMenu("&File");
  openUdfFileAction_ = fileMenu->addAction("&Open UDF...");
  openUdfFileAction_->setShortcut(QKeySequence::Open);
  openUdfFileAction_->setShortcutContext(Qt::ApplicationShortcut);
  openRecentMenu_ = fileMenu->addMenu("Open &Recent");
  openRecentMenu_->menuAction()->setShortcut(QKeySequence("Ctrl+Shift+R"));
  openRecentMenu_->menuAction()->setShortcutContext(Qt::ApplicationShortcut);
  updateRecentUdfMenu();
  openRecentQuickAction_ = fileMenu->addAction("Open Most &Recent");
  openRecentQuickAction_->setShortcut(QKeySequence("Ctrl+Shift+O"));
  openRecentQuickAction_->setShortcutContext(Qt::ApplicationShortcut);
  closeUdfFileAction_ = fileMenu->addAction("&Close UDF");
  closeUdfFileAction_->setEnabled(false);
  fileMenu->addSeparator();
  auto* exportHistoryAction = fileMenu->addAction("Export &History as Plain Text...");
  connect(exportHistoryAction, &QAction::triggered, this, &MainWindow::exportHistoryAsPlainText);

  auto* viewMenu = menuBar()->addMenu("&View");
  showDebugWindowAction_ = viewMenu->addAction("Show &Debug Window");
  showDebugWindowAction_->setCheckable(true);
  showDebugWindowAction_->setChecked(false);
  showDebugWindowAction_->setShortcut(QKeySequence("Ctrl+Alt+D"));
  showDebugWindowAction_->setShortcutContext(Qt::ApplicationShortcut);
  focusDebugWindowAction_ = viewMenu->addAction("Focus &Debug Window");
  focusDebugWindowAction_->setShortcut(QKeySequence(Qt::Key_F12));
  focusDebugWindowAction_->setShortcutContext(Qt::ApplicationShortcut);
  focusMainWindowAction_ = viewMenu->addAction("Focus &Main Window");
  focusMainWindowAction_->setShortcut(QKeySequence("Shift+F12"));
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

  auto* helpMenu = menuBar()->addMenu("&Help");
  showAboutAction_ = helpMenu->addAction("&About auxlab2");

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
  connect(audioVariableBox_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    showVariableContextMenu(audioVariableBox_, pos);
  });
  connect(nonAudioVariableBox_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    showVariableContextMenu(nonAudioVariableBox_, pos);
  });

  connect(openUdfFileAction_, &QAction::triggered, this, &MainWindow::openUdfFile);
  connect(openRecentQuickAction_, &QAction::triggered, this, [this]() {
    if (recentUdfFiles_.isEmpty()) {
      statusBar()->showMessage("No recent UDF files.", 2000);
      return;
    }
    const QString target = recentUdfFiles_.front();
    if (!QFileInfo::exists(target)) {
      recentUdfFiles_.removeFirst();
      updateRecentUdfMenu();
      statusBar()->showMessage("Most recent UDF file no longer exists.", 2500);
      return;
    }

    std::string err;
    if (!engine_.loadUdfFile(target.toStdString(), err)) {
      QMessageBox::warning(this, "Open Most Recent UDF", QString::fromStdString(err));
      return;
    }

    QFileInfo fi(target);
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
    startWatchingCurrentUdf();
    toggleDebugWindowVisible(true);
    refreshDebugView();
  });
  connect(closeUdfFileAction_, &QAction::triggered, this, &MainWindow::closeUdfFile);
  connect(showDebugWindowAction_, &QAction::toggled, this, &MainWindow::toggleDebugWindowVisible);
  connect(focusMainWindowAction_, &QAction::triggered, this, &MainWindow::focusMainWindow);
  connect(focusDebugWindowAction_, &QAction::triggered, this, &MainWindow::focusDebugWindow);
  connect(showSettingsAction_, &QAction::triggered, this, &MainWindow::showSettingsDialog);
  connect(showAboutAction_, &QAction::triggered, this, &MainWindow::showAboutDialog);
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
    const QString cmd = historyItemCommand(item);
    if (cmd.isEmpty()) {
      return;
    }
    injectCommandFromHistory(cmd, true);
  });

  auto variableDoubleClick = [this](QTreeWidgetItem* item, int) {
    if (!item) {
      return;
    }
    if (auto* box = item->treeWidget()) {
      box->setCurrentItem(item);
    }
    openPathDetail(item->text(0));
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
  connect(udfFileWatcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
    if (!QFileInfo::exists(path)) {
      return;
    }
    if (!udfFileWatcher_->files().contains(path)) {
      udfFileWatcher_->addPath(path);
    }
    reloadCurrentUdfIfStale("File changed on disk", true);
  });
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveHistory();
  saveRecentUdfFiles();
  savePersistedWindowLayout();
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
        const QString cmd = historyItemCommand(item);
        if (!cmd.isEmpty()) {
          injectCommandFromHistory(cmd, false);
        }
      }
      return true;
    }
  }

  if ((watched == audioVariableBox_ || watched == nonAudioVariableBox_) && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    auto* box = qobject_cast<QTreeWidget*>(watched);
    const bool deleteShortcut =
#ifdef Q_OS_MAC
        (ke->modifiers() & Qt::MetaModifier) &&
        (ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Delete);
#else
        ke->key() == Qt::Key_Delete && ke->modifiers() == Qt::NoModifier;
#endif
    if (deleteShortcut && box) {
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
    if (!appliedInitialDebugWindowRect_ && pendingDebugWindowRectValid_ && pendingDebugWindowRect_.isValid()) {
      debugWindow_->setGeometry(pendingDebugWindowRect_);
      appliedInitialDebugWindowRect_ = true;
    }
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

void MainWindow::loadPersistedWindowLayout() {
  QSettings settings("auxlab2", "auxlab2");

  const QByteArray mainGeometry = settings.value("ui/main_geometry").toByteArray();
  if (!mainGeometry.isEmpty()) {
    restoreGeometry(mainGeometry);
  }

  if (mainSplitter_) {
    const QByteArray splitState = settings.value("ui/main_splitter_state").toByteArray();
    if (!splitState.isEmpty()) {
      mainSplitter_->restoreState(splitState);
    }
  }

  if (variableSectionSplitter_) {
    const QByteArray splitState = settings.value("ui/variable_splitter_state").toByteArray();
    if (!splitState.isEmpty()) {
      variableSectionSplitter_->restoreState(splitState);
    }
  }

  if (audioVariableBox_) {
    for (int col = 0; col < audioVariableBox_->columnCount(); ++col) {
      const QString key = QString("ui/audio_column_width_%1").arg(col);
      if (settings.contains(key)) {
        audioVariableBox_->setColumnWidth(col, settings.value(key).toInt());
      }
    }
  }

  if (nonAudioVariableBox_) {
    for (int col = 0; col < nonAudioVariableBox_->columnCount(); ++col) {
      const QString key = QString("ui/non_audio_column_width_%1").arg(col);
      if (settings.contains(key)) {
        nonAudioVariableBox_->setColumnWidth(col, settings.value(key).toInt());
      }
    }
  }

  if (debugWindow_) {
    const QByteArray debugGeometry = settings.value("ui/debug_geometry").toByteArray();
    if (!debugGeometry.isEmpty()) {
      debugWindow_->restoreGeometry(debugGeometry);
    }
    const QRect debugRect = settings.value("ui/debug_rect").toRect();
    pendingDebugWindowRect_ = debugRect;
    pendingDebugWindowRectValid_ = debugRect.isValid();
    appliedInitialDebugWindowRect_ = false;
  }
}

void MainWindow::savePersistedWindowLayout() const {
  QSettings settings("auxlab2", "auxlab2");
  settings.setValue("ui/main_geometry", saveGeometry());
  if (mainSplitter_) {
    settings.setValue("ui/main_splitter_state", mainSplitter_->saveState());
  }
  if (variableSectionSplitter_) {
    settings.setValue("ui/variable_splitter_state", variableSectionSplitter_->saveState());
  }
  if (audioVariableBox_) {
    for (int col = 0; col < audioVariableBox_->columnCount(); ++col) {
      settings.setValue(QString("ui/audio_column_width_%1").arg(col), audioVariableBox_->columnWidth(col));
    }
  }
  if (nonAudioVariableBox_) {
    for (int col = 0; col < nonAudioVariableBox_->columnCount(); ++col) {
      settings.setValue(QString("ui/non_audio_column_width_%1").arg(col), nonAudioVariableBox_->columnWidth(col));
    }
  }
  if (debugWindow_) {
    settings.setValue("ui/debug_geometry", debugWindow_->saveGeometry());
    const QRect rect = debugWindow_->normalGeometry().isValid() ? debugWindow_->normalGeometry() : debugWindow_->geometry();
    settings.setValue("ui/debug_rect", rect);
  }
}

void MainWindow::startWatchingCurrentUdf() {
  if (!udfFileWatcher_) {
    return;
  }
  const QStringList watched = udfFileWatcher_->files();
  if (!watched.isEmpty()) {
    udfFileWatcher_->removePaths(watched);
  }
  currentUdfLastModified_ = QDateTime();
  if (currentUdfFilePath_.isEmpty()) {
    return;
  }

  const QFileInfo fi(currentUdfFilePath_);
  if (!fi.exists()) {
    return;
  }

  udfFileWatcher_->addPath(currentUdfFilePath_);
  currentUdfLastModified_ = fi.lastModified();
}

bool MainWindow::reloadCurrentUdfIfStale(const QString& reason, bool forceReload) {
  if (currentUdfFilePath_.isEmpty()) {
    return false;
  }

  const QFileInfo fi(currentUdfFilePath_);
  if (!fi.exists()) {
    return false;
  }

  const QDateTime lastModified = fi.lastModified();
  const bool stale = !currentUdfLastModified_.isValid() || lastModified > currentUdfLastModified_;
  if (!forceReload && !stale) {
    return false;
  }

  std::string err;
  if (!engine_.loadUdfFile(currentUdfFilePath_.toStdString(), err)) {
    statusBar()->showMessage(QString("Failed to reload UDF: %1").arg(QString::fromStdString(err)), 3500);
    return false;
  }

  currentUdfLastModified_ = lastModified;
  const auto bps = engine_.getBreakpoints(currentUdfName_.toStdString());
  QSet<int> qbps;
  for (int line : bps) {
    qbps.insert(line);
  }
  debugWindow_->setBreakpointsForFile(currentUdfFilePath_, qbps);
  statusBar()->showMessage(QString("%1 (%2)").arg(fi.fileName(), reason), 1800);
  return true;
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
  startWatchingCurrentUdf();
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
  startWatchingCurrentUdf();
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
  startWatchingCurrentUdf();
  refreshDebugView();
}

void MainWindow::toggleBreakpointAtCursor() {
  const QString udfName = activeDebugUdfName();
  if (udfName.isEmpty()) {
    statusBar()->showMessage("Open a UDF file first.", 2000);
    return;
  }

  const QString filePath = activeDebugFilePath();
  if (!filePath.isEmpty()) {
    const auto bps = engine_.getBreakpoints(udfName.toStdString());
    QSet<int> qbps;
    for (int line : bps) {
      qbps.insert(line);
    }
    debugWindow_->setBreakpointsForFile(filePath, qbps);
  }
  toggleDebugWindowVisible(true);
  debugWindow_->toggleBreakpointAtCursor();
}

void MainWindow::setBreakpointAtLine(int lineNumber, bool enable) {
  const QString udfName = activeDebugUdfName();
  const QString filePath = activeDebugFilePath();
  if (udfName.isEmpty() || filePath.isEmpty() || lineNumber <= 0) {
    return;
  }

  std::string err;
  if (!engine_.setBreakpoint(udfName.toStdString(), lineNumber, enable, err)) {
    statusBar()->showMessage(QString::fromStdString(err), 2500);
    return;
  }

  const auto bps = engine_.getBreakpoints(udfName.toStdString());
  QSet<int> qbps;
  for (int line : bps) {
    qbps.insert(line);
  }
  debugWindow_->setBreakpointsForFile(filePath, qbps);
  statusBar()->showMessage(enable ? QString("Breakpoint set at line %1").arg(lineNumber)
                                  : QString("Breakpoint cleared at line %1").arg(lineNumber),
                           1500);
}

void MainWindow::runCommand(const QString& cmd) {
  reloadCurrentUdfIfStale("Reloaded after external edit");
  QString actual = cmd;
  static const QRegularExpression kMethodPlayNoArg(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.play\b(?!\s*\())");
  static const QRegularExpression kMethodPlayArg(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.play\s*\((.*)\))");
  static const QRegularExpression kMethodStopNoArg(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.stop(?:\s*\(\s*\))?\b)");
  static const QRegularExpression kMethodPauseNoArg(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.pause(?:\s*\(\s*\))?\b)");
  static const QRegularExpression kMethodResumeNoArg(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.resume(?:\s*\(\s*\))?\b)");
  if (!actual.trimmed().isEmpty()) {
    actual.replace(kMethodPlayArg, QStringLiteral("play(\\1, \\2)"));
    actual.replace(kMethodPlayNoArg, QStringLiteral("play(\\1)"));
    actual.replace(kMethodStopNoArg, QStringLiteral("stop(\\1)"));
    actual.replace(kMethodPauseNoArg, QStringLiteral("pause(\\1)"));
    actual.replace(kMethodResumeNoArg, QStringLiteral("resume(\\1)"));
  }
  if (!actual.trimmed().isEmpty()) {
    addHistory(actual);
  }
  QString graphicsOutput;
  if (!actual.trimmed().isEmpty() && tryHandleGraphicsCommand(actual, graphicsOutput)) {
    updateCommandPrompt();
    const QString trimmed = actual.trimmed();
    if (trimmed.endsWith(';')) {
      commandBox_->appendExecutionResult({});
    } else {
      commandBox_->appendExecutionResult(graphicsOutput);
    }
    historyNavIndex_ = -1;
    historyDraft_.clear();
    reverseSearchActive_ = false;
    reverseSearchTerm_.clear();
    reverseSearchIndex_ = -1;
    refreshVariables();
    refreshDebugView();
    reconcileScopedWindows();
    return;
  }
  const QString trimmedForRewrite = actual.trimmed();
  static const QRegularExpression kDotAssignPattern(
      R"(^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*=)");
  const QRegularExpressionMatch dotAssignMatch = kDotAssignPattern.match(trimmedForRewrite);
  if (dotAssignMatch.hasMatch()) {
    const QString rootName = dotAssignMatch.captured(1);
    if (!rootName.isEmpty() && variableIsCell(rootName)) {
      engine_.deleteVar(rootName.toStdString());
      refreshVariables();
      reconcileScopedWindows();
    }
  }

  if (!actual.trimmed().isEmpty()) {
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

bool MainWindow::tryHandleGraphicsCommand(const QString& cmd, QString& output) {
  const QString trimmed = cmd.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  static const QRegularExpression kTrailingSemi(R"(;\s*$)");
  QString normalized = QString(trimmed).remove(kTrailingSemi).trimmed();
  QString lhs;
  static const QRegularExpression kAssignPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$)");
  const QRegularExpressionMatch assignMatch = kAssignPattern.match(normalized);
  if (assignMatch.hasMatch()) {
    lhs = assignMatch.captured(1).trimmed();
    normalized = assignMatch.captured(2).trimmed();
  }

  static const QRegularExpression kMethodAxesNoArg(R"(^([A-Za-z_][A-Za-z0-9_]*)\.axes\s*\(\s*\)$)");
  static const QRegularExpression kMethodDeleteNoArg(R"(^([A-Za-z_][A-Za-z0-9_]*)\.delete\s*\(\s*\)$)");
  static const QRegularExpression kMethodPlotNoArg(R"(^([A-Za-z_][A-Za-z0-9_]*)\.plot$)");
  static const QRegularExpression kMethodPlot(R"(^([A-Za-z_][A-Za-z0-9_]*)\.plot\s*\((.*)\)$)");
  static const QRegularExpression kMethodLine(R"(^([A-Za-z_][A-Za-z0-9_]*)\.line\s*\((.*)\)$)");
  static const QRegularExpression kMethodText(R"(^([A-Za-z_][A-Za-z0-9_]*)\.text\s*\((.*)\)$)");
  if (const auto methodPlotNoArgMatch = kMethodPlotNoArg.match(normalized); methodPlotNoArgMatch.hasMatch()) {
    const QString rewritten = QString("plot(%1)").arg(methodPlotNoArgMatch.captured(1));
    const EvalResult result = engine_.eval((lhs.isEmpty() ? rewritten : QString("%1=%2").arg(lhs, rewritten)).toStdString());
    output = QString::fromStdString(result.output);
    return true;
  }
  if (const auto methodAxesMatch = kMethodAxesNoArg.match(normalized); methodAxesMatch.hasMatch()) {
    normalized = QString("axes(%1)").arg(methodAxesMatch.captured(1));
  } else if (const auto methodDeleteMatch = kMethodDeleteNoArg.match(normalized); methodDeleteMatch.hasMatch()) {
    normalized = QString("delete(%1)").arg(methodDeleteMatch.captured(1));
  } else if (const auto methodPlotMatch = kMethodPlot.match(normalized); methodPlotMatch.hasMatch()) {
    normalized = QString("plot(%1, %2)").arg(methodPlotMatch.captured(1), methodPlotMatch.captured(2).trimmed());
  } else if (const auto methodLineMatch = kMethodLine.match(normalized); methodLineMatch.hasMatch()) {
    normalized = QString("line(%1, %2)").arg(methodLineMatch.captured(1), methodLineMatch.captured(2).trimmed());
  } else if (const auto methodTextMatch = kMethodText.match(normalized); methodTextMatch.hasMatch()) {
    normalized = QString("text(%1, %2)").arg(methodTextMatch.captured(1), methodTextMatch.captured(2).trimmed());
  }

  static const QRegularExpression kAxesCallForAuxe(R"(^axes\s*\((.*)\)$)");
  static const QRegularExpression kFigureCallForAuxe(R"(^figure\s*\((.*)\)$)");
  static const QRegularExpression kFigureNoArgForBridge(R"(^figure\s*\(\s*\)$)");
  static const QRegularExpression kPlotCallForBridgeOrAuxe(R"(^plot\s*\((.*)\)$)");
  static const QRegularExpression kLineCallForAuxe(R"(^line\s*\((.*)\)$)");
  static const QRegularExpression kDeleteCallForAuxe(R"(^delete\s*\((.*)\)$)");
  bool simplePlotForAuxe = false;
  if (const auto plotCallMatch = kPlotCallForBridgeOrAuxe.match(normalized); plotCallMatch.hasMatch()) {
    const QStringList plotArgs = splitTopLevelArgs(plotCallMatch.captured(1).trimmed());
    if (plotArgs.size() == 1) {
      simplePlotForAuxe = isSimpleIdentifier(plotArgs[0]);
    } else if (plotArgs.size() == 2) {
      if (isQuotedStringLiteral(plotArgs[1])) {
        simplePlotForAuxe = isSimpleIdentifier(plotArgs[0]);
      } else {
        simplePlotForAuxe = isSimpleIdentifier(plotArgs[1]);
      }
    } else if (plotArgs.size() == 3) {
      simplePlotForAuxe = isQuotedStringLiteral(plotArgs[2]) && isSimpleIdentifier(plotArgs[1]);
    }
  }
  // Let auxe own migrated special variables and builtin forms. More complex
  // expressions such as gcf.color still route through the existing auxlab2 bridge.
  if (normalized == "figure" || normalized == "axes" ||
      (kFigureCallForAuxe.match(normalized).hasMatch() && !kFigureNoArgForBridge.match(normalized).hasMatch()) ||
      simplePlotForAuxe ||
      kLineCallForAuxe.match(normalized).hasMatch() ||
      kAxesCallForAuxe.match(normalized).hasMatch() ||
      kDeleteCallForAuxe.match(normalized).hasMatch()) {
    return false;
  }

  auto finalizeOutput = [this, &lhs, &output](const QString& value) {
    output = value;
    if (!lhs.isEmpty() && !value.startsWith(QStringLiteral("Error:"))) {
      const QString assignExpr = QString("%1=%2").arg(lhs, value);
      engine_.eval(assignExpr.toStdString());
    }
    return true;
  };

  auto finalizeHandleOutput = [this, &lhs, &output](const std::vector<std::uint64_t>& ids, const QString& display) {
    output = display;
    if (!lhs.isEmpty()) {
      engine_.setHandleValues(lhs.toStdString(), ids);
    }
    return true;
  };

  auto resolveHandleId = [this](const QString& text, std::uint64_t& outId, QString* sourceVar = nullptr) -> bool {
    const QString trimmedText = text.trimmed();
    if (trimmedText == "gcf") {
      outId = graphicsManager_.currentFigureId();
      if (sourceVar) {
        sourceVar->clear();
      }
      return outId != 0;
    }
    if (trimmedText == "gca") {
      outId = graphicsManager_.currentAxesId();
      if (sourceVar) {
        sourceVar->clear();
      }
      return outId != 0;
    }
    bool localOk = false;
    const auto directId = trimmedText.toULongLong(&localOk);
    if (localOk && directId > 0) {
      outId = directId;
      if (sourceVar) {
        sourceVar->clear();
      }
      return true;
    }

    auto getScalarHandleValue = [this](const QString& expr) -> std::optional<double> {
      const auto directType = engine_.getValueType(expr.toStdString());
      if (directType.has_value()) {
        if ((*directType & kDisplayTypebitHandle) == 0) {
          return std::nullopt;
        }
        return engine_.getScalarValue(expr.toStdString());
      }

      const QString tmpVar = nextGraphicsTempName();
      const EvalResult evalResult = engine_.eval(QString("%1=%2").arg(tmpVar, expr).toStdString());
      if (evalResult.status != static_cast<int>(auxEvalStatus::AUX_EVAL_OK)) {
        engine_.deleteVar(tmpVar.toStdString());
        return std::nullopt;
      }

      const auto tmpType = engine_.getValueType(tmpVar.toStdString());
      if (!tmpType.has_value() || (*tmpType & kDisplayTypebitHandle) == 0) {
        engine_.deleteVar(tmpVar.toStdString());
        return std::nullopt;
      }
      const auto scalar = engine_.getScalarValue(tmpVar.toStdString());
      engine_.deleteVar(tmpVar.toStdString());
      return scalar;
    };

    const auto scalar = getScalarHandleValue(trimmedText);
    if (!scalar.has_value()) return false;

    const double rounded = std::llround(*scalar);
    if (rounded <= 0 || std::fabs(*scalar - rounded) > 1e-9) {
      return false;
    }

    outId = static_cast<std::uint64_t>(rounded);
    if (sourceVar && isSimpleIdentifier(trimmedText)) {
      *sourceVar = trimmedText;
    } else if (sourceVar) {
      sourceVar->clear();
    }
    return true;
  };

  auto isKnownHandleVariable = [this](const QString& expr) -> bool {
    if (expr == "gcf" || expr == "gca") {
      return true;
    }
    if (!isSimpleIdentifier(expr)) {
      return false;
    }
    const auto vars = engine_.listVariables();
    const auto it = std::find_if(vars.begin(), vars.end(), [&](const VarSnapshot& snap) {
      return QString::fromStdString(snap.name) == expr;
    });
    return it != vars.end() && it->typeTag == "HNDL";
  };

  auto evaluateVectorExpr = [this](const QString& expr) -> std::optional<QVector<double>> {
    const QString tmpVar = nextGraphicsTempName();
    const EvalResult evalResult = engine_.eval(QString("%1=%2").arg(tmpVar, expr).toStdString());
    if (evalResult.status != static_cast<int>(auxEvalStatus::AUX_EVAL_OK)) {
      engine_.deleteVar(tmpVar.toStdString());
      return std::nullopt;
    }
    auto values = engine_.getNumericVector(tmpVar.toStdString());
    engine_.deleteVar(tmpVar.toStdString());
    return values;
  };

  auto evaluateGraphicsExpr = [this](const QString& expr) -> std::optional<QString> {
    const QString tmpVar = nextGraphicsTempName();
    const EvalResult evalResult = engine_.eval(QString("%1=%2").arg(tmpVar, expr).toStdString());
    if (evalResult.status != static_cast<int>(auxEvalStatus::AUX_EVAL_OK)) {
      engine_.deleteVar(tmpVar.toStdString());
      return std::nullopt;
    }
    const auto scalar = engine_.getScalarValue(tmpVar.toStdString());
    if (scalar.has_value()) {
      engine_.deleteVar(tmpVar.toStdString());
      return QString::number(*scalar, 'g', 15);
    }
    const auto values = engine_.getNumericVector(tmpVar.toStdString());
    engine_.deleteVar(tmpVar.toStdString());
    if (values.has_value()) {
      return formatDoubleVector(*values);
    }
    return std::nullopt;
  };

  auto handleSetter = [&](std::uint64_t handleId, const QString& prop, const QString& rhs) -> QString {
    SignalGraphWindow* owner = graphWindowForHandle(handleId);
    if (!owner) {
      return QString("Error: graphics handle not found: %1").arg(handleId);
    }

    auto& model = owner->graphicsModelMutable();
    const QString key = prop.trimmed().toLower();
    const auto figureId = model.figure().common.id;
    bool needsRefresh = true;

    auto setCommon = [&](GraphicsObjectCommon& common) -> bool {
      if (key == "pos") {
        std::array<double, 4> pos{};
        if (!parseMatlabPosVector(rhs, pos)) return false;
        common.pos = pos;
        return true;
      }
      if (key == "color") {
        QVector<double> vals;
        if (!parseDoubleVectorExpr(rhs, vals) || vals.size() != 3) return false;
        common.color = QColor::fromRgbF(vals[0], vals[1], vals[2]);
        return true;
      }
      if (key == "visible") {
        bool visible = false;
        if (!parseBoolExpr(rhs, visible)) return false;
        common.visible = visible;
        return true;
      }
      return false;
    };

    if (handleId == figureId) {
      if (key == "pos") {
        std::array<double, 4> pos{};
        if (!parseMatlabPosVector(rhs, pos)) {
          return QString("Error: unsupported or invalid figure property assignment: %1").arg(prop);
        }
        model.figureMutable().common.pos = pos;
        owner->applyFigurePos(pos);
        return QStringLiteral("[]");
      }
      if (!setCommon(model.figureMutable().common)) {
        return QString("Error: unsupported or invalid figure property assignment: %1").arg(prop);
      }
      owner->refreshGraphics();
      return QStringLiteral("[]");
    }
    if (auto* axes = model.axesByIdMutable(handleId)) {
      if (setCommon(axes->common)) {
        owner->refreshGraphics();
        return QStringLiteral("[]");
      }
      if (key == "box") {
        bool value = false;
        if (!parseBoolExpr(rhs, value)) return QString("Error: invalid axes property value for %1").arg(prop);
        axes->box = value;
      } else if (key == "linewidth") {
        bool okNum = false;
        const int value = rhs.trimmed().toInt(&okNum);
        if (!okNum) return QString("Error: invalid axes property value for %1").arg(prop);
        axes->lineWidth = value;
      } else if (key == "xlim" || key == "ylim") {
        QVector<double> vals;
        if (!parseDoubleVectorExpr(rhs, vals) || vals.size() != 2) return QString("Error: invalid axes property value for %1").arg(prop);
        if (key == "xlim") axes->xlim = {vals[0], vals[1]};
        else axes->ylim = {vals[0], vals[1]};
      } else if (key == "fontname" || key == "xscale" || key == "yscale") {
        if (!isQuotedStringLiteral(rhs)) return QString("Error: invalid axes property value for %1").arg(prop);
        const QString value = unquoteStringLiteral(rhs);
        if (key == "fontname") axes->fontName = value;
        else if (key == "xscale") axes->xscale = value;
        else axes->yscale = value;
      } else if (key == "fontsize") {
        bool okNum = false;
        const int value = rhs.trimmed().toInt(&okNum);
        if (!okNum) return QString("Error: invalid axes property value for %1").arg(prop);
        axes->fontSize = value;
      } else if (key == "xgrid" || key == "ygrid") {
        bool value = false;
        if (!parseBoolExpr(rhs, value)) return QString("Error: invalid axes property value for %1").arg(prop);
        if (key == "xgrid") axes->xgrid = value; else axes->ygrid = value;
      } else {
        return QString("Error: unsupported or invalid axes property assignment: %1").arg(prop);
      }
      owner->refreshGraphics();
      return QStringLiteral("[]");
    }
    if (auto* line = model.lineByIdMutable(handleId)) {
      if (setCommon(line->common)) {
        owner->refreshGraphics();
        return QStringLiteral("[]");
      }
      if (key == "xdata" || key == "ydata") {
        auto values = evaluateVectorExpr(rhs);
        if (!values.has_value()) return QString("Error: invalid line property value for %1").arg(prop);
        if (key == "xdata") line->xdata = *values; else line->ydata = *values;
        if (line->xdata.size() != line->ydata.size()) return QStringLiteral("Error: xdata and ydata must have the same length.");
      } else if (key == "linewidth" || key == "markersize") {
        bool okNum = false;
        const int value = rhs.trimmed().toInt(&okNum);
        if (!okNum) return QString("Error: invalid line property value for %1").arg(prop);
        if (key == "linewidth") line->lineWidth = value; else line->markerSize = value;
      } else if (key == "linestyle" || key == "marker") {
        if (!isQuotedStringLiteral(rhs)) return QString("Error: invalid line property value for %1").arg(prop);
        const QString value = unquoteStringLiteral(rhs);
        if (key == "linestyle") line->lineStyle = value; else line->marker = value;
      } else {
        return QString("Error: unsupported or invalid line property assignment: %1").arg(prop);
      }
      owner->refreshGraphics();
      return QStringLiteral("[]");
    }
    if (auto* text = model.textByIdMutable(handleId)) {
      if (setCommon(text->common)) {
        owner->refreshGraphics();
        return QStringLiteral("[]");
      }
      if (key == "fontname" || key == "string") {
        if (!isQuotedStringLiteral(rhs)) return QString("Error: invalid text property value for %1").arg(prop);
        const QString value = unquoteStringLiteral(rhs);
        if (key == "fontname") text->fontName = value; else text->stringValue = value;
      } else if (key == "fontsize") {
        bool okNum = false;
        const int value = rhs.trimmed().toInt(&okNum);
        if (!okNum) return QString("Error: invalid text property value for %1").arg(prop);
        text->fontSize = value;
      } else {
        return QString("Error: unsupported or invalid text property assignment: %1").arg(prop);
      }
      owner->refreshGraphics();
      return QStringLiteral("[]");
    }
    return QString("Error: graphics handle not found: %1").arg(handleId);
  };

  static const QRegularExpression kHandleCompoundSetPattern(R"(^(.+)\.([A-Za-z_][A-Za-z0-9_]*)\s*(\+=|-=|\*=|/=)\s*(.+)$)");
  if (const auto compoundSetMatch = kHandleCompoundSetPattern.match(normalized); compoundSetMatch.hasMatch()) {
    const QString rootExpr = compoundSetMatch.captured(1).trimmed();
    const QString prop = compoundSetMatch.captured(2);
    const QString op = compoundSetMatch.captured(3).left(1);
    const QString rhs = compoundSetMatch.captured(4).trimmed();
    std::uint64_t handleId = 0;
    if (!resolveHandleId(rootExpr, handleId)) {
      return false;
    }
    const QString currentValue = graphicsHandleProperty(handleId, prop);
    if (currentValue.startsWith(QStringLiteral("Error:"))) {
      output = currentValue;
      return true;
    }
    const auto computedValue = evaluateGraphicsExpr(QString("(%1)%2(%3)").arg(currentValue, op, rhs));
    if (!computedValue.has_value()) {
      return finalizeOutput(QString("Error: invalid graphics property expression for %1").arg(prop));
    }
    const QString result = handleSetter(handleId, prop, *computedValue);
    output = result;
    return true;
  }

  static const QRegularExpression kHandleSetPattern(R"(^(.+)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$)");
  if (const auto setMatch = kHandleSetPattern.match(normalized); setMatch.hasMatch()) {
    std::uint64_t handleId = 0;
    const QString rootExpr = setMatch.captured(1).trimmed();
    if (!resolveHandleId(rootExpr, handleId)) {
      return false;
    }
    const QString result = handleSetter(handleId, setMatch.captured(2), setMatch.captured(3).trimmed());
    output = result;
    return true;
  }

  static const QRegularExpression kHandleGetPattern(R"(^(.+)\.([A-Za-z_][A-Za-z0-9_]*)$)");
  if (const auto getMatch = kHandleGetPattern.match(normalized); getMatch.hasMatch()) {
    std::uint64_t handleId = 0;
    const QString rootExpr = getMatch.captured(1).trimmed();
    if (!resolveHandleId(rootExpr, handleId)) {
      return false;
    }
    const QString prop = getMatch.captured(2);
    const QString value = graphicsHandleProperty(handleId, prop);
    if (!lhs.isEmpty()) {
      if (const auto refIds = graphicsHandleReferenceIds(handleId, prop); refIds.has_value()) {
        return finalizeHandleOutput(*refIds, value);
      }
    }
    return finalizeOutput(value);
  }

  if (!normalized.isEmpty() && !normalized.contains(QRegularExpression(R"(^\d+$)"))) {
    std::uint64_t handleId = 0;
    const bool knownHandleRoot = isKnownHandleVariable(normalized) ||
                                 normalized.contains('(') ||
                                 normalized.contains('.');
    if (knownHandleRoot && resolveHandleId(normalized, handleId) && graphWindowForHandle(handleId)) {
      if (!lhs.isEmpty()) {
        return finalizeHandleOutput({handleId}, graphicsHandleText(handleId));
      }
      return finalizeOutput(graphicsHandleDump(handleId));
    }
  }

  if (normalized == "figure") {
    auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
    const std::uint64_t id = window ? window->graphicsModel().figure().common.id : 0;
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  if (normalized == "gcf") {
    const std::uint64_t id = graphicsManager_.currentFigureId();
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }
  if (normalized == "gca") {
    const std::uint64_t id = graphicsManager_.currentAxesId();
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  static const QRegularExpression kFigureCall(R"(^figure\s*\((.*)\)$)");
  static const QRegularExpression kFigureNoArg(R"(^figure\s*\(\s*\)$)");
  static const QRegularExpression kPlotCall(R"(^plot\s*\((.*)\)$)");
  static const QRegularExpression kLineCall(R"(^line\s*\((.*)\)$)");
  static const QRegularExpression kTextCall(R"(^text\s*\((.*)\)$)");
  static const QRegularExpression kAxesCall(R"(^axes\s*\((.*)\)$)");
  static const QRegularExpression kDeleteCall(R"(^delete\s*\((.*)\)$)");
  if (kFigureNoArg.match(normalized).hasMatch()) {
    auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
    const std::uint64_t id = window ? window->graphicsModel().figure().common.id : 0;
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  bool ok = false;

  const QRegularExpressionMatch axesMatch = kAxesCall.match(normalized);
  if (axesMatch.hasMatch()) {
    const QString axesArg = axesMatch.captured(1).trimmed();
    if (axesArg.isEmpty()) {
      return finalizeOutput(QStringLiteral("Error: axes() requires a handle or position."));
    }

    std::array<double, 4> axesPos{};
    if (parseMatlabPosVector(axesArg, axesPos)) {
      SignalGraphWindow* window = graphicsManager_.currentFigureWindow();
      if (!window) {
        window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
      }
      const auto axesId = window->addAxes(axesPos);
      window->selectAxes(axesId);
      graphicsManager_.markFocused(window);
      focusWindow(window);
      return finalizeHandleOutput({axesId}, graphicsHandleText(axesId));
    }

    std::uint64_t axesOrFigureId = 0;
    if (!resolveHandleId(axesArg, axesOrFigureId)) {
      return finalizeOutput(QString("Error: invalid axes argument: %1").arg(axesArg));
    }

    if (auto* owner = graphicsManager_.findAxesOwner(axesOrFigureId)) {
      owner->selectAxes(axesOrFigureId);
      graphicsManager_.markFocused(owner);
      focusWindow(owner);
      return finalizeHandleOutput({axesOrFigureId}, graphicsHandleText(axesOrFigureId));
    }

    if (auto* figure = graphicsManager_.findFigureById(axesOrFigureId)) {
      const auto axesId = figure->addAxes({0.08, 0.18, 0.86, 0.72});
      figure->selectAxes(axesId);
      graphicsManager_.markFocused(figure);
      focusWindow(figure);
      return finalizeHandleOutput({axesId}, graphicsHandleText(axesId));
    }

    return finalizeOutput(QString("Error: figure/axes handle not found: %1").arg(axesArg));
  }

  const QRegularExpressionMatch plotMatch = kPlotCall.match(normalized);
  if (plotMatch.hasMatch()) {
    const QStringList args = splitTopLevelArgs(plotMatch.captured(1).trimmed());
    if (args.isEmpty() || args.size() > 3) {
      return finalizeOutput(QStringLiteral("Error: currently supported forms are plot(x), plot(x,\"style\"), plot(h,x), and plot(h,x,\"style\")."));
    }

    QString expr;
    SignalGraphWindow* targetWindow = nullptr;
    QString styleArg;
    if (args.size() == 1) {
      expr = args.front();
    } else if (args.size() == 2) {
      if (isQuotedStringLiteral(args[1])) {
        expr = args[0];
        styleArg = unquoteStringLiteral(args[1]);
      } else {
        expr = args[1];
        std::uint64_t maybeId = 0;
        if (!resolveHandleId(args.front(), maybeId)) {
          return finalizeOutput(QStringLiteral("Error: currently supported forms are plot(x), plot(x,\"style\"), plot(h,x), and plot(h,x,\"style\")."));
        }
        targetWindow = graphicsManager_.findFigureById(maybeId);
        if (!targetWindow) {
          return finalizeOutput(QString("Error: figure handle not found: %1").arg(args.front()));
        }
      }
    } else if (args.size() == 3) {
      std::uint64_t maybeId = 0;
      if (!resolveHandleId(args.front(), maybeId)) {
        return finalizeOutput(QStringLiteral("Error: currently supported forms are plot(x), plot(x,\"style\"), plot(h,x), and plot(h,x,\"style\")."));
      }
      targetWindow = graphicsManager_.findFigureById(maybeId);
      if (!targetWindow) {
        return finalizeOutput(QString("Error: figure handle not found: %1").arg(args.front()));
      }
      expr = args[1];
      if (!isQuotedStringLiteral(args[2])) {
        return finalizeOutput(QStringLiteral("Error: plot style must be a quoted string."));
      }
      styleArg = unquoteStringLiteral(args[2]);
    }

    PlotStyleSpec styleSpec;
    if (!styleArg.isEmpty()) {
      styleSpec = parsePlotStyleString(styleArg);
      if (!styleSpec.valid) {
        return finalizeOutput(QString("Error: invalid plot style: \"%1\"").arg(styleArg));
      }
    }

    const QString tmpVar = nextGraphicsTempName();
    const QString assignExpr = QString("%1=%2").arg(tmpVar, expr);
    const EvalResult evalResult = engine_.eval(assignExpr.toStdString());
    if (evalResult.status != static_cast<int>(auxEvalStatus::AUX_EVAL_OK)) {
      engine_.deleteVar(tmpVar.toStdString());
      return finalizeOutput(QString::fromStdString(evalResult.output).trimmed().isEmpty()
                                ? QString("Error: failed to evaluate plot expression: %1").arg(expr)
                                : QString::fromStdString(evalResult.output));
    }

    auto sig = engine_.getSignalData(tmpVar.toStdString());
    engine_.deleteVar(tmpVar.toStdString());
    if (!sig) {
      return finalizeOutput(QString("Error: plot expression is not plottable: %1").arg(expr));
    }

    if (!targetWindow) {
      targetWindow = createSignalFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), *sig, false, QString(), false);
    } else {
      targetWindow->updateData(*sig);
      focusWindow(targetWindow);
      graphicsManager_.markFocused(targetWindow);
    }
    if (!sig->isAudio && !sig->channels.empty()) {
      const int expectedLength = static_cast<int>(sig->channels.front().samples.size());
      if (const auto xdata = extractRangeXData(expr, expectedLength); xdata.has_value()) {
        targetWindow->applyXDataToAllLines(*xdata);
      }
    }
    if (!styleArg.isEmpty()) {
      targetWindow->applyStyleToAllLines(styleSpec.hasColor ? std::optional<QColor>(styleSpec.color) : std::nullopt,
                                         styleSpec.marker,
                                         styleSpec.lineStyle);
    }
    const std::uint64_t id = targetWindow ? targetWindow->graphicsModel().figure().common.id : 0;
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  const QRegularExpressionMatch deleteMatch = kDeleteCall.match(normalized);
  if (deleteMatch.hasMatch()) {
    const QString arg = deleteMatch.captured(1).trimmed();
    if (arg.isEmpty()) {
      return finalizeOutput(QStringLiteral("Error: delete() requires a graphics handle."));
    }

    std::uint64_t objectId = 0;
    QString sourceVar;
    if (!resolveHandleId(arg, objectId, &sourceVar)) {
      return finalizeOutput(QString("Error: invalid graphics handle: %1").arg(arg));
    }

    if (auto* figure = graphicsManager_.findFigureById(objectId)) {
      const bool deletingCurrent = (graphicsManager_.currentFigureWindow() == figure);
      if (deletingCurrent) {
        graphicsManager_.clearCurrentWindow(figure);
      }
      figure->close();
      if (!sourceVar.isEmpty()) {
        engine_.eval(QString("%1=[]").arg(sourceVar).toStdString());
      }
      return finalizeOutput(QStringLiteral("[]"));
    }

    if (auto* owner = graphicsManager_.findAxesOwner(objectId)) {
      const bool deletingCurrentAxes = (graphicsManager_.currentFigureWindow() == owner &&
                                        graphicsManager_.currentAxesId() == objectId);
      if (!owner->removeAxes(objectId)) {
        return finalizeOutput(QString("Error: graphics handle not found: %1").arg(arg));
      }
      if (deletingCurrentAxes) {
        graphicsManager_.markFocused(owner);
      }
      if (!sourceVar.isEmpty()) {
        engine_.eval(QString("%1=[]").arg(sourceVar).toStdString());
      }
      return finalizeOutput(QStringLiteral("[]"));
    }

    if (auto* owner = graphWindowForHandle(objectId)) {
      if (owner->graphicsModel().containsLine(objectId)) {
        if (!owner->removeLine(objectId)) {
          return finalizeOutput(QString("Error: graphics handle not found: %1").arg(arg));
        }
      } else if (owner->graphicsModel().containsText(objectId)) {
        if (!owner->removeText(objectId)) {
          return finalizeOutput(QString("Error: graphics handle not found: %1").arg(arg));
        }
      } else {
        return finalizeOutput(QString("Error: graphics handle not found: %1").arg(arg));
      }
      if (!sourceVar.isEmpty()) {
        engine_.eval(QString("%1=[]").arg(sourceVar).toStdString());
      }
      return finalizeOutput(QStringLiteral("[]"));
    }

    return finalizeOutput(QString("Error: graphics handle not found: %1").arg(arg));
  }

  const QRegularExpressionMatch lineMatch = kLineCall.match(normalized);
  if (lineMatch.hasMatch()) {
    const QStringList args = splitTopLevelArgs(lineMatch.captured(1).trimmed());
    if (args.isEmpty() || args.size() > 3) {
      return finalizeOutput(QStringLiteral("Error: supported forms are line(x), line(x,y), line(h,x), and line(h,x,y)."));
    }

    SignalGraphWindow* targetWindow = nullptr;
    std::uint64_t targetAxesId = 0;
    int argIndex = 0;
    if (args.size() >= 2) {
      std::uint64_t maybeId = 0;
      if (resolveHandleId(args.front(), maybeId)) {
        if (auto* owner = graphicsManager_.findAxesOwner(maybeId)) {
          targetWindow = owner;
          targetAxesId = maybeId;
          argIndex = 1;
        } else if (auto* figure = graphicsManager_.findFigureById(maybeId)) {
          targetWindow = figure;
          if (const auto* currentAxes = figure->graphicsModel().currentAxes()) {
            targetAxesId = currentAxes->common.id;
          } else {
            targetAxesId = figure->addAxes({0.08, 0.18, 0.86, 0.72});
          }
          argIndex = 1;
        }
      }
    }

    QString xExpr;
    QString yExpr;
    if (args.size() - argIndex == 1) {
      yExpr = args[argIndex];
    } else if (args.size() - argIndex == 2) {
      xExpr = args[argIndex];
      yExpr = args[argIndex + 1];
    } else {
      return finalizeOutput(QStringLiteral("Error: supported forms are line(x), line(x,y), line(h,x), and line(h,x,y)."));
    }

    auto yVals = evaluateVectorExpr(yExpr);
    if (!yVals.has_value()) {
      return finalizeOutput(QString("Error: line data is not a numeric vector: %1").arg(yExpr));
    }

    QVector<double> xVals;
    if (xExpr.isEmpty()) {
      xVals.reserve(yVals->size());
      for (int i = 0; i < yVals->size(); ++i) {
        xVals.push_back(i + 1);
      }
    } else {
      auto maybeX = evaluateVectorExpr(xExpr);
      if (!maybeX.has_value()) {
        return finalizeOutput(QString("Error: line x data is not a numeric vector: %1").arg(xExpr));
      }
      if (maybeX->size() != yVals->size()) {
        return finalizeOutput(QStringLiteral("Error: x and y for line() must have the same length."));
      }
      xVals = *maybeX;
    }

    if (!targetWindow) {
      SignalData emptyData;
      targetWindow = createSignalFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), emptyData, false, QString(), false);
      if (const auto* currentAxes = targetWindow->graphicsModel().currentAxes()) {
        targetAxesId = currentAxes->common.id;
      } else {
        targetAxesId = targetWindow->addAxes({0.08, 0.18, 0.86, 0.72});
      }
    }

    const auto lineId = targetWindow->addLine(targetAxesId, xVals, *yVals);
    if (lineId == 0) {
      return finalizeOutput(QStringLiteral("Error: failed to create line object."));
    }
    focusWindow(targetWindow);
    graphicsManager_.markFocused(targetWindow);
    return finalizeHandleOutput({lineId}, graphicsHandleText(lineId));
  }

  const QRegularExpressionMatch textMatch = kTextCall.match(normalized);
  if (textMatch.hasMatch()) {
    const QStringList args = splitTopLevelArgs(textMatch.captured(1).trimmed());
    if (args.size() != 3 && args.size() != 4) {
      return finalizeOutput(QStringLiteral("Error: supported forms are text(x,y,\"text\") and text(h,x,y,\"text\")."));
    }

    SignalGraphWindow* targetWindow = nullptr;
    std::uint64_t parentId = 0;
    int argIndex = 0;
    if (args.size() == 4) {
      std::uint64_t maybeId = 0;
      if (!resolveHandleId(args[0], maybeId)) {
        return finalizeOutput(QString("Error: invalid text parent handle: %1").arg(args[0]));
      }
      if (auto* owner = graphicsManager_.findAxesOwner(maybeId)) {
        targetWindow = owner;
        parentId = maybeId;
      } else if (auto* figure = graphicsManager_.findFigureById(maybeId)) {
        targetWindow = figure;
        parentId = maybeId;
      } else {
        return finalizeOutput(QString("Error: figure/axes handle not found: %1").arg(args[0]));
      }
      argIndex = 1;
    }

    bool okX = false;
    bool okY = false;
    const double posX = args[argIndex].trimmed().toDouble(&okX);
    const double posY = args[argIndex + 1].trimmed().toDouble(&okY);
    if (!okX || !okY) {
      return finalizeOutput(QStringLiteral("Error: text position must be numeric."));
    }
    if (!isQuotedStringLiteral(args[argIndex + 2])) {
      return finalizeOutput(QStringLiteral("Error: text string must be quoted."));
    }
    const QString textValue = unquoteStringLiteral(args[argIndex + 2]);

    if (!targetWindow) {
      targetWindow = graphicsManager_.currentFigureWindow();
      if (!targetWindow) {
        targetWindow = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
      }
      parentId = targetWindow->graphicsModel().figure().common.id;
    }

    const auto textId = targetWindow->addText(parentId, posX, posY, textValue);
    if (textId == 0) {
      return finalizeOutput(QStringLiteral("Error: failed to create text object."));
    }
    focusWindow(targetWindow);
    graphicsManager_.markFocused(targetWindow);
    return finalizeHandleOutput({textId}, graphicsHandleText(textId));
  }

  const QRegularExpressionMatch figureMatch = kFigureCall.match(normalized);
  if (!figureMatch.hasMatch()) {
    return false;
  }

  const QString arg = figureMatch.captured(1).trimmed();
  if (arg.isEmpty()) {
    auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle());
    const std::uint64_t id = window ? window->graphicsModel().figure().common.id : 0;
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  if (arg.startsWith('"') && arg.endsWith('"') && arg.size() >= 2) {
    const QString path = arg.mid(1, arg.size() - 2);
    if (path.isEmpty() || !variableSupportsSignalDisplay(path)) {
      return finalizeOutput(QString("Error: variable not plottable: %1").arg(path));
    }
    if (auto* existing = graphicsManager_.findNamedFigure(path)) {
      focusWindow(existing);
      graphicsManager_.markFocused(existing);
      return finalizeHandleOutput({existing->graphicsModel().figure().common.id},
                                  graphicsHandleText(existing->graphicsModel().figure().common.id));
    }
    openSignalGraphForPath(path);
    if (auto* current = graphicsManager_.currentFigureWindow()) {
      return finalizeHandleOutput({current->graphicsModel().figure().common.id},
                                  graphicsHandleText(current->graphicsModel().figure().common.id));
    }
    return finalizeOutput(QStringLiteral("[]"));
  }

  std::array<double, 4> pos{};
  if (parseMatlabPosVector(arg, pos)) {
    auto* window = createEmptyFigureWindow(graphicsManager_.nextUnnamedFigureTitle(), matlabFigureRectToQt(pos));
    const std::uint64_t id = window ? window->graphicsModel().figure().common.id : 0;
    return finalizeHandleOutput(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id}, graphicsHandleText(id));
  }

  std::uint64_t id = 0;
  if (resolveHandleId(arg, id)) {
    if (auto* existing = graphicsManager_.findFigureById(id)) {
      focusWindow(existing);
      graphicsManager_.markFocused(existing);
      return finalizeHandleOutput({existing->graphicsModel().figure().common.id},
                                  graphicsHandleText(existing->graphicsModel().figure().common.id));
    }
    return finalizeOutput(QString("Error: figure handle not found: %1").arg(arg));
  }

  return finalizeOutput(QString("Error: invalid figure argument: %1").arg(arg));
}

void MainWindow::onAsyncPollTick() {
  refreshPlaybackHandles();
  const int changed = engine_.pollAsync();
  if (changed <= 0) {
    return;
  }
  refreshVariables();
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

void MainWindow::showVariableContextMenu(QTreeWidget* box, const QPoint& pos) {
  if (!box) {
    return;
  }

  if (auto* item = box->itemAt(pos); item && !item->isSelected()) {
    box->setCurrentItem(item);
    item->setSelected(true);
  }

  QMenu menu(this);
  const QString actionText = QString("Delete Variable(s)\t%1").arg(variableDeleteShortcutText());
  auto* deleteAction = menu.addAction(actionText);
  deleteAction->setEnabled(!selectedVarNames(box).isEmpty());

  const QAction* chosen = menu.exec(box->viewport()->mapToGlobal(pos));
  if (chosen == deleteAction) {
    deleteVariablesFromBox(box);
  }
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
      const QString pauseFile = QString::fromStdString(infoOpt->filename);
      const QString pauseUdf = QFileInfo(pauseFile).completeBaseName();
      debugWindow_->setPauseLocation(pauseFile, infoOpt->line);
      if (!pauseUdf.isEmpty()) {
        const auto bps = engine_.getBreakpoints(pauseUdf.toStdString());
        QSet<int> qbps;
        for (int line : bps) {
          qbps.insert(line);
        }
        debugWindow_->setBreakpointsForFile(pauseFile, qbps);
      }
    }
  }
}

QString MainWindow::activeDebugFilePath() const {
  if (!debugWindow_) {
    return {};
  }
  return debugWindow_->currentFilePath();
}

QString MainWindow::activeDebugUdfName() const {
  const QString filePath = activeDebugFilePath();
  if (!filePath.isEmpty()) {
    const QString base = QFileInfo(filePath).completeBaseName();
    if (!base.isEmpty()) {
      return base;
    }
  }
  return currentUdfName_;
}

void MainWindow::addHistory(const QString& cmd) {
  if (cmd.trimmed().isEmpty()) {
    return;
  }

  for (int i = historyBox_->count() - 1; i >= 0; --i) {
    auto* latestItem = historyBox_->item(i);
    if (!latestItem || isHistoryCommentItem(latestItem)) {
      continue;
    }
    if (historyItemCommand(latestItem) == cmd) {
      int count = latestItem->data(kHistoryCountRole).toInt();
      if (count <= 0) {
        count = 1;
      }
      latestItem->setData(kHistoryCountRole, count + 1);
      updateHistoryItemDisplay(latestItem);
      historyBox_->setCurrentItem(latestItem);
      historyBox_->scrollToItem(latestItem);
      return;
    }
    break;
  }

  auto* item = new QListWidgetItem(historyBox_);
  item->setData(kHistoryCommandRole, cmd);
  item->setData(kHistoryCountRole, 1);
  item->setData(kHistoryIsCommentRole, false);
  updateHistoryItemDisplay(item);
  historyBox_->setCurrentItem(item);
  historyBox_->scrollToBottom();
}

void MainWindow::addHistoryComment(const QString& text) {
  if (text.trimmed().isEmpty()) {
    return;
  }
  auto* item = new QListWidgetItem(historyBox_);
  item->setData(kHistoryCommandRole, QString());
  item->setData(kHistoryCountRole, 0);
  item->setData(kHistoryIsCommentRole, true);
  item->setText(text);
}

void MainWindow::updateHistoryItemDisplay(QListWidgetItem* item) const {
  if (!item) {
    return;
  }
  if (isHistoryCommentItem(item)) {
    return;
  }

  const QString cmd = historyItemCommand(item);
  int count = item->data(kHistoryCountRole).toInt();
  if (count <= 0) {
    count = 1;
    item->setData(kHistoryCountRole, count);
  }

  QString display = cmd;
  if (count > 1) {
    display += QString("   (%1x)").arg(count);
  }
  item->setText(display);
  item->setToolTip(display);
}

bool MainWindow::isHistoryCommentItem(const QListWidgetItem* item) const {
  if (!item) {
    return false;
  }
  if (item->data(kHistoryIsCommentRole).isValid()) {
    return item->data(kHistoryIsCommentRole).toBool();
  }
  return item->text().trimmed().startsWith("//");
}

QString MainWindow::historyItemCommand(const QListWidgetItem* item) const {
  if (!item || isHistoryCommentItem(item)) {
    return {};
  }
  if (item->data(kHistoryCommandRole).isValid()) {
    return item->data(kHistoryCommandRole).toString();
  }
  return item->text();
}

QVector<int> MainWindow::historyCommandRows() const {
  QVector<int> rows;
  rows.reserve(historyBox_->count());
  for (int i = 0; i < historyBox_->count(); ++i) {
    const auto* item = historyBox_->item(i);
    if (!item || isHistoryCommentItem(item)) {
      continue;
    }
    if (historyItemCommand(item).trimmed().isEmpty()) {
      continue;
    }
    rows.push_back(i);
  }
  return rows;
}

void MainWindow::addHistorySessionBanner() {
  addHistoryComment(makeSessionBannerText());
  historyBox_->scrollToBottom();
}

void MainWindow::loadHistory() {
  QFile f(historyFilePath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    addHistorySessionBanner();
    return;
  }
  QTextStream in(&f);
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (line.startsWith("#H\t")) {
      const QString payload = line.mid(3);
      const int tabPos = payload.indexOf('\t');
      if (tabPos <= 0) {
        continue;
      }
      bool ok = false;
      const int count = payload.left(tabPos).toInt(&ok);
      const QString cmd = payload.mid(tabPos + 1);
      if (!ok || cmd.trimmed().isEmpty()) {
        continue;
      }
      auto* item = new QListWidgetItem(historyBox_);
      item->setData(kHistoryCommandRole, cmd);
      item->setData(kHistoryCountRole, std::max(1, count));
      item->setData(kHistoryIsCommentRole, false);
      updateHistoryItemDisplay(item);
      continue;
    }
    if (line.startsWith("#C\t")) {
      addHistoryComment(line.mid(3));
      continue;
    }
    if (line.trimmed().isEmpty()) {
      continue;
    }
    if (line.trimmed().startsWith("//")) {
      addHistoryComment(line);
      continue;
    }
    auto* item = new QListWidgetItem(historyBox_);
    item->setData(kHistoryCommandRole, line);
    item->setData(kHistoryCountRole, 1);
    item->setData(kHistoryIsCommentRole, false);
    updateHistoryItemDisplay(item);
  }
  addHistorySessionBanner();
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
      if (isHistoryCommentItem(item)) {
        out << "#C\t" << item->text() << '\n';
      } else {
        const QString cmd = historyItemCommand(item);
        if (cmd.trimmed().isEmpty()) {
          continue;
        }
        int count = item->data(kHistoryCountRole).toInt();
        if (count <= 0) {
          count = 1;
        }
        out << "#H\t" << count << '\t' << cmd << '\n';
      }
    }
  }
}

void MainWindow::exportHistoryAsPlainText() {
  const QString defaultName =
      QString("auxlab2-history-%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (baseDir.isEmpty()) {
    baseDir = QDir::homePath();
  }
  const QString suggestedPath = QDir(baseDir).filePath(defaultName);
  const QString targetPath = QFileDialog::getSaveFileName(
      this,
      "Export History as Plain Text",
      suggestedPath,
      "Text Files (*.txt);;All Files (*)");
  if (targetPath.isEmpty()) {
    return;
  }

  QFile outFile(targetPath);
  if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::warning(this, "Export History", QString("Failed to write file:\n%1").arg(targetPath));
    return;
  }

  QTextStream out(&outFile);
  for (int i = 0; i < historyBox_->count(); ++i) {
    const QListWidgetItem* item = historyBox_->item(i);
    if (!item) {
      continue;
    }

    if (isHistoryCommentItem(item)) {
      out << item->text() << '\n';
      continue;
    }

    const QString cmd = historyItemCommand(item);
    if (cmd.trimmed().isEmpty()) {
      continue;
    }

    int count = item->data(kHistoryCountRole).toInt();
    if (count <= 0) {
      count = 1;
    }
    for (int repeat = 0; repeat < count; ++repeat) {
      out << cmd << '\n';
    }
  }

  outFile.close();
  statusBar()->showMessage(QString("History exported to %1").arg(targetPath), 4000);
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
  const QVector<int> commandRows = historyCommandRows();
  const int n = commandRows.size();
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
  const int row = commandRows[historyNavIndex_];
  auto* item = historyBox_->item(row);
  if (!item) {
    return;
  }
  historyBox_->setCurrentRow(row);
  commandBox_->setCurrentCommand(historyItemCommand(item));
}

void MainWindow::reverseSearchFromCommand() {
  const QVector<int> commandRows = historyCommandRows();
  const int n = commandRows.size();
  if (n <= 0) {
    return;
  }

  const QString currentInput = commandBox_->currentCommand().trimmed();
  if (reverseSearchActive_ && reverseSearchIndex_ >= 0 && reverseSearchIndex_ < n) {
    auto* curItem = historyBox_->item(commandRows[reverseSearchIndex_]);
    if (curItem && historyItemCommand(curItem) != commandBox_->currentCommand()) {
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
    auto* item = historyBox_->item(commandRows[i]);
    if (!item) {
      continue;
    }
    const QString cmd = historyItemCommand(item);
    if (reverseSearchTerm_.isEmpty() || cmd.contains(reverseSearchTerm_, Qt::CaseInsensitive)) {
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
  const int row = commandRows[found];
  historyBox_->setCurrentRow(row);
  const QString match = historyItemCommand(historyBox_->item(row));
  commandBox_->setCurrentCommand(match);
  statusBar()->showMessage(QString("reverse-i-search \"%1\": %2").arg(reverseSearchTerm_, match), 2500);
}

void MainWindow::openSignalGraphForSelected() {
  const QString var = selectedVarName();
  openSignalGraphForPath(var);
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
  openPathDetail(var);
}

void MainWindow::playSelectedAudioFromVarBox() {
  const QString var = selectedVarName();
  playAudioForPath(var);
}

void MainWindow::openSignalGraphForPath(const QString& path) {
  if (path.isEmpty() || !variableSupportsSignalDisplay(path)) {
    return;
  }
  auto sig = engine_.getSignalData(path.toStdString());
  if (!sig) {
    return;
  }

  const auto currentScope = engine_.activeContext();
  if (auto* existing = findSignalGraphWindow(path, currentScope)) {
    existing->updateData(*sig);
    focusWindow(existing);
    return;
  }

  SignalGraphWindow::CreationOptions options;
  options.title = path;
  options.namedPlot = true;
  options.sourcePath = path;

  auto* w = new SignalGraphWindow(
      path, *sig, options, nullptr,
      [this, path](int viewStart, int viewLen) { return engine_.getSignalFftPowerDb(path.toStdString(), viewStart, viewLen); });
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(path, w, WindowKind::Graph);
  focusWindow(w);
}

SignalGraphWindow* MainWindow::createEmptyFigureWindow(const QString& title, const QRect& geometry) {
  SignalData emptyData;
  SignalGraphWindow::CreationOptions options;
  options.title = title;
  options.namedPlot = false;

  auto* w = new SignalGraphWindow(title, emptyData, options);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  if (geometry.isValid()) {
    w->setGeometry(geometry);
  }
  trackWindow(title, w, WindowKind::Graph, false);
  focusWindow(w);
  graphicsManager_.markFocused(w);
  return w;
}

SignalGraphWindow* MainWindow::createSignalFigureWindow(const QString& title,
                                                       const SignalData& data,
                                                       bool namedPlot,
                                                       const QString& sourcePath,
                                                       bool variableBacked) {
  SignalGraphWindow::CreationOptions options;
  options.title = title;
  options.namedPlot = namedPlot;
  options.sourcePath = sourcePath;

  const QString trackName = variableBacked && !sourcePath.isEmpty() ? sourcePath : title;
  auto* w = new SignalGraphWindow(
      trackName,
      data,
      options,
      nullptr,
      [this, sourcePath, variableBacked, trackName](int viewStart, int viewLen) {
        const QString fftSource = variableBacked && !sourcePath.isEmpty() ? sourcePath : trackName;
        return engine_.getSignalFftPowerDb(fftSource.toStdString(), viewStart, viewLen);
      });
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(trackName, w, WindowKind::Graph, variableBacked);
  focusWindow(w);
  graphicsManager_.markFocused(w);
  return w;
}

void MainWindow::openPathDetail(const QString& path) {
  if (path.isEmpty()) {
    return;
  }

  if (openGraphicsPathDetail(path)) {
    return;
  }

  if (variableIsStruct(path)) {
    openStructMembersForPath(path);
    return;
  }
  if (variableIsCell(path)) {
    openCellMembersForPath(path);
    return;
  }

  if (variableIsString(path)) {
    auto text = engine_.getStringValue(path.toStdString());
    if (!text) {
      return;
    }
    auto* w = new TextObjectWindow(path, QString::fromStdString(*text));
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(path, w, WindowKind::Text);
    w->show();
    return;
  }

  if (variableIsBinary(path)) {
    auto binary = engine_.getBinaryData(path.toStdString());
    if (!binary) {
      return;
    }

    QByteArray data;
    data.resize(static_cast<int>(binary->bytes.size()));
    if (!binary->bytes.empty()) {
      std::memcpy(data.data(), binary->bytes.data(), binary->bytes.size());
    }

    auto* w = new BinaryObjectWindow(path, data);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(path, w, WindowKind::Text);
    w->show();
    return;
  }

  if (!variableSupportsSignalDisplay(path)) {
    return;
  }

  auto sig = engine_.getSignalData(path.toStdString());
  if (!sig) {
    return;
  }

  auto* w = new SignalTableWindow(path, *sig);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(path, w, WindowKind::Table);
  w->show();
}

bool MainWindow::openGraphicsPathDetail(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }

  if (const auto ids = graphicsHandlePathIds(path); ids.has_value()) {
    if (ids->empty()) {
      auto* w = new TextObjectWindow(path, QStringLiteral("[]"));
      w->setAttribute(Qt::WA_DeleteOnClose, true);
      trackWindow(path, w, WindowKind::Text);
      w->show();
      return true;
    }

    if (ids->size() == 1) {
      auto members = graphicsHandleMembersForId(ids->front());
      auto* w = new StructMembersWindow(path, members);
      w->setAttribute(Qt::WA_DeleteOnClose, true);
      connect(w, &StructMembersWindow::requestOpenGraph, this, &MainWindow::openSignalGraphForPath);
      connect(w, &StructMembersWindow::requestPlayAudio, this, &MainWindow::playAudioForPath);
      connect(w, &StructMembersWindow::requestOpenDetail, this, &MainWindow::openPathDetail);
      trackWindow(path, w, WindowKind::Text);
      w->show();
      return true;
    }

    auto* w = new CellMembersWindow(path, graphicsHandleArrayMembers(*ids));
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(w, &CellMembersWindow::requestOpenGraph, this, &MainWindow::openSignalGraphForPath);
    connect(w, &CellMembersWindow::requestPlayAudio, this, &MainWindow::playAudioForPath);
    connect(w, &CellMembersWindow::requestOpenDetail, this, &MainWindow::openPathDetail);
    trackWindow(path, w, WindowKind::Text);
    w->show();
    return true;
  }

  const int dot = path.lastIndexOf('.');
  if (dot <= 0) {
    return false;
  }

  const QString root = path.left(dot);
  const QString prop = path.mid(dot + 1);
  const auto rootIds = graphicsHandlePathIds(root);
  if (!rootIds.has_value() || rootIds->size() != 1) {
    return false;
  }
  const QString value = graphicsHandleProperty(rootIds->front(), prop);

  if (const auto numeric = signalDataFromNumericDisplay(value); numeric.has_value()) {
    auto* w = new SignalTableWindow(path, *numeric);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(path, w, WindowKind::Table);
    w->show();
    return true;
  }

  auto* w = new TextObjectWindow(path, value);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  trackWindow(path, w, WindowKind::Text);
  w->show();
  return true;
}

void MainWindow::playAudioForPath(const QString& path) {
  if (path.isEmpty() || !variableIsAudio(path)) {
    return;
  }

  auto sig = engine_.getSignalData(path.toStdString());
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

  QAudioFormat fmt;
  const int sampleRate = sig->sampleRate > 0 ? sig->sampleRate : 22050;
  int chCount = 0;
  int totalFrames = 0;
  varPcmData_ = buildAudioPcm16(*sig, chCount, totalFrames);
  fmt.setSampleRate(sampleRate);
  fmt.setChannelCount(chCount);
  fmt.setSampleFormat(QAudioFormat::Int16);

  varAudioBuffer_ = new QBuffer(this);
  varAudioBuffer_->setData(varPcmData_);
  varAudioBuffer_->open(QIODevice::ReadOnly);

  varAudioSink_ = new QAudioSink(fmt, this);
  varAudioSink_->start(varAudioBuffer_);
}

bool MainWindow::startPlaybackHandle(std::uint64_t handleId,
                                     AuxObj obj,
                                     int repeatCount,
                                     bool reuseExistingHandle,
                                     std::string& err) {
  if (!obj || repeatCount < 1) {
    err = "play() requires an audio object and a positive repeat count.";
    return false;
  }

  const auto sig = signalDataFromAuxObj(obj, 22050);
  if (!sig || !sig->isAudio || sig->channels.empty()) {
    err = "play() requires an audio object.";
    return false;
  }

  if (varAudioSink_) {
    varAudioSink_->disconnect(this);
    varAudioSink_->reset();
    varAudioSink_->stop();
    delete varAudioSink_;
    varAudioSink_ = nullptr;
  }
  if (varAudioBuffer_) {
    varAudioBuffer_->close();
    delete varAudioBuffer_;
    varAudioBuffer_ = nullptr;
  }
  varPcmData_.clear();

  const int sampleRate = sig->sampleRate > 0 ? sig->sampleRate : 22050;
  const double onePassDurationMs = sig->startTimeSec * 1000.0 +
                                   (sig->channels.empty() ? 0.0
                                                          : 1000.0 * static_cast<double>(sig->channels.front().samples.size()) /
                                                                static_cast<double>(sampleRate));
  int channelCount = 0;
  int onePassFrames = 0;
  QByteArray onePassPcm = buildAudioPcm16(*sig, channelCount, onePassFrames);
  if (onePassFrames <= 0 || channelCount <= 0) {
    err = "play() received empty audio.";
    return false;
  }

  QByteArray repeatedPcm;
  repeatedPcm.reserve(onePassPcm.size() * repeatCount);
  for (int i = 0; i < repeatCount; ++i) {
    repeatedPcm += onePassPcm;
  }
  auto it = playbackSessions_.find(handleId);
  if (reuseExistingHandle) {
    if (it == playbackSessions_.end()) {
      err = "Invalid or inactive playback handle.";
      return false;
    }
    PlaybackSession& session = it->second;
    if ((!session.paused && (!session.sink || !session.buffer ||
         session.sink->state() == QAudio::IdleState || session.sink->state() == QAudio::StoppedState)) ||
        session.sampleRate != sampleRate || session.channelCount != channelCount) {
      err = "Queued playback must match the active playback format.";
      return false;
    }
    session.pcmData += repeatedPcm;
    if (session.buffer) {
      session.buffer->buffer().append(repeatedPcm);
    }
    int cumulativeEnd = session.segmentEndFrames.empty() ? 0 : session.segmentEndFrames.back();
    for (int i = 0; i < repeatCount; ++i) {
      cumulativeEnd += onePassFrames;
      session.segmentEndFrames.push_back(cumulativeEnd);
    }
    session.totalFrames += onePassFrames * repeatCount;
    session.durationMs += onePassDurationMs * repeatCount;
    engine_.updateRuntimeHandleMembers(handleId,
                                       {{"dur", session.durationMs},
                                        {"repeat_left", static_cast<double>(std::max(0, static_cast<int>(session.segmentEndFrames.size()) - 1))}});
    refreshVariables();
    return true;
  }

  if (it != playbackSessions_.end()) {
    QAudioSink* oldSink = it->second.sink;
    QBuffer* oldBuffer = it->second.buffer;
    it->second.sink = nullptr;
    it->second.buffer = nullptr;
    playbackSessions_.erase(it);
    if (oldSink) {
      oldSink->stop();
      oldSink->deleteLater();
    }
    if (oldBuffer) {
      oldBuffer->close();
      oldBuffer->deleteLater();
    }
  }

  PlaybackSession session;
  session.handleId = handleId;
  session.sampleRate = sampleRate;
  session.channelCount = channelCount;
  session.repeatCount = repeatCount;
  session.durationMs = onePassDurationMs * repeatCount;
  session.totalFrames = onePassFrames * repeatCount;
  session.pcmData = std::move(repeatedPcm);
  int cumulativeEnd = 0;
  for (int i = 0; i < repeatCount; ++i) {
    cumulativeEnd += onePassFrames;
    session.segmentEndFrames.push_back(cumulativeEnd);
  }

  QAudioFormat fmt;
  fmt.setSampleRate(session.sampleRate);
  fmt.setChannelCount(session.channelCount);
  fmt.setSampleFormat(QAudioFormat::Int16);

  session.buffer = new QBuffer(this);
  session.buffer->setData(session.pcmData);
  session.buffer->open(QIODevice::ReadOnly);

  session.sink = new QAudioSink(fmt, this);
  connect(session.sink, &QAudioSink::stateChanged, this, [this](QAudio::State) {
    refreshPlaybackHandles();
  });
  session.sink->start(session.buffer);

  playbackSessions_[handleId] = std::move(session);
  engine_.updateRuntimeHandleMembers(handleId,
                                     {{"fs", static_cast<double>(playbackSessions_[handleId].sampleRate)},
                                      {"dur", playbackSessions_[handleId].durationMs},
                                      {"repeat_left", static_cast<double>(std::max(0, static_cast<int>(playbackSessions_[handleId].segmentEndFrames.size()) - 1))},
                                      {"prog", 0.0}});
  refreshVariables();
  return true;
}

bool MainWindow::controlPlaybackHandle(std::uint64_t handleId,
                                       auxPlaybackCommand command,
                                       std::string& err) {
  auto it = playbackSessions_.find(handleId);
  if (it == playbackSessions_.end()) {
    err = "Invalid or inactive playback handle.";
    return false;
  }

  if (command != auxPlaybackCommand::AUX_PLAYBACK_STOP) {
    PlaybackSession& session = it->second;
    switch (command) {
      case auxPlaybackCommand::AUX_PLAYBACK_PAUSE: {
        if (session.paused) {
          return true;
        }
        if (varAudioSink_) {
          varAudioSink_->disconnect(this);
          varAudioSink_->reset();
          varAudioSink_->stop();
          delete varAudioSink_;
          varAudioSink_ = nullptr;
        }
        if (varAudioBuffer_) {
          varAudioBuffer_->close();
          delete varAudioBuffer_;
          varAudioBuffer_ = nullptr;
        }
        varPcmData_.clear();

        for (auto& entry : playbackSessions_) {
          PlaybackSession& other = entry.second;
          if (other.sink && other.buffer) {
            const qint64 bytesConsumed =
                other.pcmData.size() - std::max<qint64>(0, other.buffer->bytesAvailable());
            other.pausedBytes = std::clamp<qint64>(bytesConsumed, 0, other.pcmData.size());
          }
          if (other.buffer) {
            other.buffer->close();
          }
          if (other.sink) {
            other.sink->disconnect(this);
            other.sink->reset();
            other.sink->stop();
            delete other.sink;
            other.sink = nullptr;
          }
          if (other.buffer) {
            delete other.buffer;
            other.buffer = nullptr;
          }
          other.paused = true;
        }
        refreshVariables();
        return true;
      }
      case auxPlaybackCommand::AUX_PLAYBACK_RESUME: {
        if (!session.paused) {
          return true;
        }
        if (session.pausedBytes >= session.pcmData.size()) {
          err = "Invalid or inactive playback handle.";
          return false;
        }
        QAudioFormat fmt;
        fmt.setSampleRate(session.sampleRate);
        fmt.setChannelCount(session.channelCount);
        fmt.setSampleFormat(QAudioFormat::Int16);
        session.buffer = new QBuffer(this);
        session.buffer->setData(session.pcmData);
        session.buffer->open(QIODevice::ReadOnly);
        session.buffer->seek(session.pausedBytes);
        session.sink = new QAudioSink(fmt, this);
        connect(session.sink, &QAudioSink::stateChanged, this, [this](QAudio::State) {
          refreshPlaybackHandles();
        });
        session.sink->start(session.buffer);
        session.paused = false;
        refreshVariables();
        return true;
      }
      default:
        err = "Playback command not available yet.";
        return false;
    }
  }

  if (varAudioSink_) {
    varAudioSink_->disconnect(this);
    varAudioSink_->reset();
    varAudioSink_->stop();
    delete varAudioSink_;
    varAudioSink_ = nullptr;
  }
  if (varAudioBuffer_) {
    varAudioBuffer_->close();
    delete varAudioBuffer_;
    varAudioBuffer_ = nullptr;
  }
  varPcmData_.clear();

  for (auto& entry : playbackSessions_) {
    PlaybackSession& session = entry.second;
    engine_.updateRuntimeHandleMembers(session.handleId, {{"repeat_left", 0.0}, {"prog", 100.0}});
    if (session.buffer) {
      session.buffer->close();
    }
    if (session.sink) {
      session.sink->disconnect(this);
      session.sink->reset();
      session.sink->stop();
      delete session.sink;
      session.sink = nullptr;
    }
    if (session.buffer) {
      delete session.buffer;
      session.buffer = nullptr;
    }
    session.pcmData.clear();
  }
  playbackSessions_.clear();
  refreshVariables();
  return true;
}

void MainWindow::refreshPlaybackHandles() {
  bool anyFinished = false;
  for (auto it = playbackSessions_.begin(); it != playbackSessions_.end();) {
    PlaybackSession& session = it->second;
    if (!session.sink) {
      if (session.paused) {
        ++it;
        continue;
      }
      anyFinished = true;
      it = playbackSessions_.erase(it);
      continue;
    }

    const qsizetype bytesPerFrame = session.channelCount * static_cast<int>(sizeof(qint16));
    const qint64 bytesConsumed = session.buffer
        ? (session.pcmData.size() - std::max<qint64>(0, session.buffer->bytesAvailable()))
        : 0;
    const int framesConsumed = bytesPerFrame > 0
        ? static_cast<int>(std::clamp<qint64>(bytesConsumed / bytesPerFrame, 0, session.totalFrames))
        : 0;
    int completedSegments = 0;
    while (completedSegments < static_cast<int>(session.segmentEndFrames.size()) &&
           framesConsumed >= session.segmentEndFrames[static_cast<size_t>(completedSegments)]) {
      ++completedSegments;
    }
    const int repeatLeft = std::max(0, static_cast<int>(session.segmentEndFrames.size()) - completedSegments - 1);
    const double prog = session.totalFrames > 0
        ? 100.0 * static_cast<double>(framesConsumed) / static_cast<double>(session.totalFrames)
        : 100.0;

    engine_.updateRuntimeHandleMembers(session.handleId,
                                       {{"repeat_left", static_cast<double>(repeatLeft)},
                                        {"prog", std::clamp(prog, 0.0, 100.0)}});

    if (session.sink->state() == QAudio::IdleState || session.sink->state() == QAudio::StoppedState) {
      QAudioSink* finishedSink = session.sink;
      QBuffer* finishedBuffer = session.buffer;
      engine_.updateRuntimeHandleMembers(session.handleId, {{"repeat_left", 0.0}, {"prog", 100.0}});
      session.sink = nullptr;
      session.buffer = nullptr;
      anyFinished = true;
      it = playbackSessions_.erase(it);
      if (finishedSink) {
        finishedSink->stop();
        finishedSink->deleteLater();
      }
      if (finishedBuffer) {
        finishedBuffer->close();
        finishedBuffer->deleteLater();
      }
      continue;
    }
    ++it;
  }
  if (anyFinished) {
    refreshVariables();
  }
}

void MainWindow::openStructMembersForPath(const QString& path) {
  if (path.isEmpty()) {
    return;
  }

  const auto members = engine_.listStructMembers(path.toStdString());
  if (members.empty()) {
    const auto handleId = graphicsHandleIdForVariable(path);
    if (!handleId.has_value()) {
      return;
    }

    auto* w = new TextObjectWindow(path, graphicsHandleDump(*handleId));
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    trackWindow(path, w, WindowKind::Text);
    w->show();
    return;
  }

  auto* w = new StructMembersWindow(path, members);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  connect(w, &StructMembersWindow::requestOpenGraph, this, &MainWindow::openSignalGraphForPath);
  connect(w, &StructMembersWindow::requestPlayAudio, this, &MainWindow::playAudioForPath);
  connect(w, &StructMembersWindow::requestOpenDetail, this, &MainWindow::openPathDetail);
  trackWindow(path, w, WindowKind::Text);
  w->show();
}

void MainWindow::openCellMembersForPath(const QString& path) {
  if (path.isEmpty()) {
    return;
  }

  const auto members = engine_.listCellMembers(path.toStdString());
  if (members.empty()) {
    return;
  }

  auto* w = new CellMembersWindow(path, members);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  connect(w, &CellMembersWindow::requestOpenGraph, this, &MainWindow::openSignalGraphForPath);
  connect(w, &CellMembersWindow::requestPlayAudio, this, &MainWindow::playAudioForPath);
  connect(w, &CellMembersWindow::requestOpenDetail, this, &MainWindow::openPathDetail);
  trackWindow(path, w, WindowKind::Text);
  w->show();
}

void MainWindow::trackWindow(const QString& varName, QWidget* window, WindowKind kind, bool variableBacked) {
  ScopedWindow s;
  s.varName = varName;
  s.variableBacked = variableBacked;
  s.scope = engine_.activeContext();
  s.kind = kind;
  s.window = window;
  scopedWindows_.push_back(s);
  window->installEventFilter(this);
  auto* graphWindow = qobject_cast<SignalGraphWindow*>(window);
  if (graphWindow) {
    graphicsManager_.registerWindow(graphWindow);
  }
  connect(window, &QObject::destroyed, this, [this, window, graphWindow]() {
    if (graphWindow) {
      graphicsManager_.unregisterWindow(graphWindow);
    }
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

SignalGraphWindow* MainWindow::graphWindowForHandle(std::uint64_t handleId) const {
  if (auto* owner = graphicsManager_.findFigureById(handleId)) {
    return owner;
  }
  if (auto* owner = graphicsManager_.findAxesOwner(handleId)) {
    return owner;
  }
  for (auto it = scopedWindows_.rbegin(); it != scopedWindows_.rend(); ++it) {
    if (it->kind != WindowKind::Graph || !it->window) {
      continue;
    }
    if (auto* g = qobject_cast<SignalGraphWindow*>(it->window.data())) {
      if (g->graphicsModel().containsLine(handleId) || g->graphicsModel().containsText(handleId)) {
        return g;
      }
    }
  }
  return nullptr;
}

std::optional<std::uint64_t> MainWindow::graphicsHandleIdForVariable(const QString& varName) const {
  if (varName.isEmpty()) {
    return std::nullopt;
  }
  const auto scalar = engine_.getScalarValue(varName.toStdString());
  if (!scalar.has_value()) {
    return std::nullopt;
  }
  const auto rounded = static_cast<long long>(std::llround(*scalar));
  if (rounded <= 0 || std::fabs(*scalar - static_cast<double>(rounded)) > 1e-9) {
    return std::nullopt;
  }
  const auto handleId = static_cast<std::uint64_t>(rounded);
  return graphWindowForHandle(handleId) ? std::optional<std::uint64_t>(handleId) : std::nullopt;
}

std::optional<std::vector<std::uint64_t>> MainWindow::graphicsHandleReferenceIds(std::uint64_t handleId, const QString& prop) const {
  SignalGraphWindow* owner = graphWindowForHandle(handleId);
  if (!owner) {
    return std::nullopt;
  }

  const auto& model = owner->graphicsModel();
  const QString key = prop.trimmed().toLower();

  auto commonRefs = [&](const GraphicsObjectCommon& common) -> std::optional<std::vector<std::uint64_t>> {
    if (key == "parent") {
      return std::vector<std::uint64_t>(common.parentId == 0 ? 0 : 1, common.parentId);
    }
    if (key == "children") {
      return common.children;
    }
    return std::nullopt;
  };

  if (handleId == model.figure().common.id) {
    return commonRefs(model.figure().common);
  }
  if (const auto axes = std::find_if(model.axes().begin(), model.axes().end(), [handleId](const GraphicsAxesHandle& ax) {
        return ax.common.id == handleId;
      }); axes != model.axes().end()) {
    return commonRefs(axes->common);
  }
  if (const auto* line = model.lineById(handleId)) {
    return commonRefs(line->common);
  }
  for (const auto& text : model.texts()) {
    if (text.common.id == handleId) {
      return commonRefs(text.common);
    }
  }
  return std::nullopt;
}

std::optional<std::vector<std::uint64_t>> MainWindow::graphicsHandlePathIds(const QString& path) const {
  if (path.isEmpty()) {
    return std::nullopt;
  }

  const int brace = path.lastIndexOf('{');
  if (brace > 0 && path.endsWith('}')) {
    bool ok = false;
    const int oneBased = path.mid(brace + 1, path.size() - brace - 2).toInt(&ok);
    if (!ok || oneBased <= 0) {
      return std::nullopt;
    }
    const auto baseIds = graphicsHandlePathIds(path.left(brace));
    if (!baseIds.has_value()) {
      return std::nullopt;
    }
    const size_t index = static_cast<size_t>(oneBased - 1);
    if (index >= baseIds->size()) {
      return std::vector<std::uint64_t>{};
    }
    return std::vector<std::uint64_t>{(*baseIds)[index]};
  }

  if (const auto handleId = graphicsHandleIdForVariable(path); handleId.has_value()) {
    return std::vector<std::uint64_t>{*handleId};
  }

  const int dot = path.lastIndexOf('.');
  if (dot <= 0) {
    return std::nullopt;
  }
  const QString root = path.left(dot);
  const QString prop = path.mid(dot + 1);
  const auto rootIds = graphicsHandlePathIds(root);
  if (!rootIds.has_value() || rootIds->size() != 1) {
    return std::nullopt;
  }
  return graphicsHandleReferenceIds(rootIds->front(), prop);
}

std::vector<VarSnapshot> MainWindow::graphicsHandleMembersForId(std::uint64_t handleId) const {
  std::vector<VarSnapshot> out;

  auto makeSnapshot = [&](const QString& name, const QString& typeTag, const QString& size, const QString& preview) {
    VarSnapshot snap;
    snap.name = name.toStdString();
    snap.typeTag = typeTag.toStdString();
    snap.size = size.toStdString();
    snap.preview = preview.toStdString();
    return snap;
  };

  const QString typeValue = graphicsHandleProperty(handleId, QStringLiteral("type"));
  if (typeValue.startsWith(QStringLiteral("Error:"))) {
    return out;
  }

  const QString typeName = unquoteStringLiteral(typeValue);
  out.push_back(makeSnapshot(QStringLiteral("type"), QStringLiteral("TEXT"), QStringLiteral("1"), typeValue));
  out.push_back(makeSnapshot(QStringLiteral("pos"), QStringLiteral("VECT"), QStringLiteral("4"), graphicsHandleProperty(handleId, QStringLiteral("pos"))));
  out.push_back(makeSnapshot(QStringLiteral("color"), QStringLiteral("VECT"), QStringLiteral("3"), graphicsHandleProperty(handleId, QStringLiteral("color"))));
  out.push_back(makeSnapshot(QStringLiteral("visible"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("visible"))));

  if (const auto parentIds = graphicsHandleReferenceIds(handleId, QStringLiteral("parent")); parentIds.has_value()) {
    const QString size = QString::number(parentIds->size());
    const QString preview = parentIds->empty() ? QStringLiteral("[]") : graphicsHandleProperty(handleId, QStringLiteral("parent"));
    out.push_back(makeSnapshot(QStringLiteral("parent"), QStringLiteral("HNDL"), size, preview));
  }
  if (const auto childIds = graphicsHandleReferenceIds(handleId, QStringLiteral("children")); childIds.has_value()) {
    out.push_back(makeSnapshot(QStringLiteral("children"), QStringLiteral("HNDL"),
                               QString::number(childIds->size()),
                               graphicsHandleProperty(handleId, QStringLiteral("children"))));
  }

  if (typeName == "axes") {
    out.push_back(makeSnapshot(QStringLiteral("box"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("box"))));
    out.push_back(makeSnapshot(QStringLiteral("linewidth"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("linewidth"))));
    out.push_back(makeSnapshot(QStringLiteral("xlim"), QStringLiteral("VECT"), QStringLiteral("2"), graphicsHandleProperty(handleId, QStringLiteral("xlim"))));
    out.push_back(makeSnapshot(QStringLiteral("ylim"), QStringLiteral("VECT"), QStringLiteral("2"), graphicsHandleProperty(handleId, QStringLiteral("ylim"))));
    out.push_back(makeSnapshot(QStringLiteral("fontname"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("fontname"))));
    out.push_back(makeSnapshot(QStringLiteral("fontsize"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("fontsize"))));
    out.push_back(makeSnapshot(QStringLiteral("xscale"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("xscale"))));
    out.push_back(makeSnapshot(QStringLiteral("yscale"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("yscale"))));
    out.push_back(makeSnapshot(QStringLiteral("xgrid"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("xgrid"))));
    out.push_back(makeSnapshot(QStringLiteral("ygrid"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("ygrid"))));
  } else if (typeName == "line") {
    out.push_back(makeSnapshot(QStringLiteral("xdata"), QStringLiteral("VECT"), QStringLiteral("?"), graphicsHandleProperty(handleId, QStringLiteral("xdata"))));
    out.push_back(makeSnapshot(QStringLiteral("ydata"), QStringLiteral("VECT"), QStringLiteral("?"), graphicsHandleProperty(handleId, QStringLiteral("ydata"))));
    out.push_back(makeSnapshot(QStringLiteral("linewidth"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("linewidth"))));
    out.push_back(makeSnapshot(QStringLiteral("linestyle"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("linestyle"))));
    out.push_back(makeSnapshot(QStringLiteral("marker"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("marker"))));
    out.push_back(makeSnapshot(QStringLiteral("markersize"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("markersize"))));
  } else if (typeName == "text") {
    out.push_back(makeSnapshot(QStringLiteral("fontname"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("fontname"))));
    out.push_back(makeSnapshot(QStringLiteral("fontsize"), QStringLiteral("SCLR"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("fontsize"))));
    out.push_back(makeSnapshot(QStringLiteral("string"), QStringLiteral("TEXT"), QStringLiteral("1"), graphicsHandleProperty(handleId, QStringLiteral("string"))));
  }

  return out;
}

std::vector<VarSnapshot> MainWindow::graphicsHandleArrayMembers(const std::vector<std::uint64_t>& ids) const {
  std::vector<VarSnapshot> out;
  out.reserve(ids.size());
  for (size_t i = 0; i < ids.size(); ++i) {
    VarSnapshot snap;
    snap.name = std::to_string(i + 1);
    snap.typeTag = "HNDL";
    snap.size = "1";
    snap.preview = QString::number(ids[i]).toStdString();
    out.push_back(std::move(snap));
  }
  return out;
}

QString MainWindow::graphicsHandleProperty(std::uint64_t handleId, const QString& prop) const {
  SignalGraphWindow* owner = graphWindowForHandle(handleId);
  if (!owner) {
    return QString("Error: graphics handle not found: %1").arg(handleId);
  }

  const auto& model = owner->graphicsModel();
  const QString key = prop.trimmed().toLower();
  const auto figureId = model.figure().common.id;

  auto commonGetter = [&](const GraphicsObjectCommon& common) -> QString {
    if (key == "pos") return formatDoubleArray4(common.pos);
    if (key == "color") return formatColor(common.color);
    if (key == "visible") return common.visible ? QStringLiteral("1") : QStringLiteral("0");
    if (key == "parent") return common.parentId == 0 ? QStringLiteral("[]") : QString::number(common.parentId);
    if (key == "children") return formatChildren(common.children);
    return QString();
  };

  if (handleId == figureId) {
    const auto& fig = model.figure();
    if (key == "type") return QStringLiteral("\"figure\"");
    if (key == "pos") return formatDoubleArray4(owner->currentFigurePos());
    if (const QString value = commonGetter(fig.common); !value.isEmpty()) return value;
    return QString("Error: unsupported figure property: %1").arg(prop);
  }
  if (const auto axes = std::find_if(model.axes().begin(), model.axes().end(), [handleId](const GraphicsAxesHandle& ax) {
        return ax.common.id == handleId;
      }); axes != model.axes().end()) {
    if (key == "type") return QStringLiteral("\"axes\"");
    if (const QString value = commonGetter(axes->common); !value.isEmpty()) return value;
    if (key == "box") return axes->box ? QStringLiteral("1") : QStringLiteral("0");
    if (key == "linewidth") return QString::number(axes->lineWidth);
    if (key == "xlim") return formatDoubleArray2(axes->xlim);
    if (key == "ylim") return formatDoubleArray2(axes->ylim);
    if (key == "fontname") return QString("\"%1\"").arg(axes->fontName);
    if (key == "fontsize") return QString::number(axes->fontSize);
    if (key == "xscale") return QString("\"%1\"").arg(axes->xscale);
    if (key == "yscale") return QString("\"%1\"").arg(axes->yscale);
    if (key == "xgrid") return axes->xgrid ? QStringLiteral("1") : QStringLiteral("0");
    if (key == "ygrid") return axes->ygrid ? QStringLiteral("1") : QStringLiteral("0");
    return QString("Error: unsupported axes property: %1").arg(prop);
  }
  if (const auto* line = model.lineById(handleId)) {
    if (key == "type") return QStringLiteral("\"line\"");
    if (const QString value = commonGetter(line->common); !value.isEmpty()) return value;
    if (key == "xdata") return formatDoubleVector(line->xdata);
    if (key == "ydata") return formatDoubleVector(line->ydata);
    if (key == "linewidth") return QString::number(line->lineWidth);
    if (key == "linestyle") return QString("\"%1\"").arg(line->lineStyle);
    if (key == "marker") return QString("\"%1\"").arg(line->marker);
    if (key == "markersize") return QString::number(line->markerSize);
    return QString("Error: unsupported line property: %1").arg(prop);
  }
  if (const auto& texts = model.texts(); std::any_of(texts.begin(), texts.end(), [handleId](const GraphicsTextHandle& text) {
        return text.common.id == handleId;
      })) {
    const auto* text = [&]() -> const GraphicsTextHandle* {
      for (const auto& item : texts) {
        if (item.common.id == handleId) return &item;
      }
      return nullptr;
    }();
    if (!text) {
      return QString("Error: graphics handle not found: %1").arg(handleId);
    }
    if (key == "type") return QStringLiteral("\"text\"");
    if (const QString value = commonGetter(text->common); !value.isEmpty()) return value;
    if (key == "fontname") return QString("\"%1\"").arg(text->fontName);
    if (key == "fontsize") return QString::number(text->fontSize);
    if (key == "string") return QString("\"%1\"").arg(text->stringValue);
    return QString("Error: unsupported text property: %1").arg(prop);
  }
  return QString("Error: graphics handle not found: %1").arg(handleId);
}

QString MainWindow::graphicsHandleDump(std::uint64_t handleId) const {
  const QString typeValue = graphicsHandleProperty(handleId, QStringLiteral("type"));
  if (typeValue.startsWith(QStringLiteral("Error:"))) {
    return typeValue;
  }

  const QString typeName = unquoteStringLiteral(typeValue);
  QStringList props = {
      QStringLiteral("type"),
      QStringLiteral("pos"),
      QStringLiteral("color"),
      QStringLiteral("visible"),
      QStringLiteral("parent"),
      QStringLiteral("children"),
  };
  if (typeName == "axes") {
    props << QStringLiteral("box") << QStringLiteral("linewidth") << QStringLiteral("xlim")
          << QStringLiteral("ylim") << QStringLiteral("fontname") << QStringLiteral("fontsize")
          << QStringLiteral("xscale") << QStringLiteral("yscale") << QStringLiteral("xgrid")
          << QStringLiteral("ygrid");
  } else if (typeName == "line") {
    props << QStringLiteral("xdata") << QStringLiteral("ydata") << QStringLiteral("linewidth")
          << QStringLiteral("linestyle") << QStringLiteral("marker") << QStringLiteral("markersize");
  } else if (typeName == "text") {
    props << QStringLiteral("fontname") << QStringLiteral("fontsize") << QStringLiteral("string");
  }

  QString out = QString("type = 0x%1, [Handle] %2\n")
                    .arg(QString::number(kDisplayTypebitHandle, 16).rightJustified(4, QLatin1Char('0')))
                    .arg(handleId);
  for (const QString& property : props) {
    out += QString(".%1 = %2").arg(property, graphicsHandleProperty(handleId, property));
    if (property != props.back()) {
      out += QLatin1Char('\n');
    }
  }
  return out;
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
  graphicsManager_.reconcile();
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
    const std::string fullName = it->varName.toStdString();
    size_t rootPos = fullName.find('.');
    const size_t cellPos = fullName.find('{');
    if (cellPos != std::string::npos && (rootPos == std::string::npos || cellPos < rootPos)) {
      rootPos = cellPos;
    }
    const std::string rootName = rootPos == std::string::npos ? fullName : fullName.substr(0, rootPos);
    if (it->variableBacked && it->scope == currentScope && activeNames.find(rootName) == activeNames.end()) {
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
  if (auto* graphWindow = qobject_cast<SignalGraphWindow*>(window)) {
    graphicsManager_.markFocused(graphWindow);
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

bool MainWindow::variableIsStruct(const QString& varName) const {
  return engine_.isStructVar(varName.toStdString());
}

bool MainWindow::variableIsCell(const QString& varName) const {
  return engine_.isCellVar(varName.toStdString());
}

void MainWindow::handleDebugAction(auxDebugAction action) {
  reloadCurrentUdfIfStale("Reloaded after external edit");
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

void MainWindow::showAboutDialog() {
  QString auxeVersion = QString::fromStdString(engine_.engineVersion());
  if (auxeVersion.trimmed().isEmpty()) {
    auxeVersion = AUXE_VERSION;
  }
  const QString aboutText =
      QString("%1\nVersion: %2\nCommit: %3\nBuild time: %4\n\nauxe\nVersion: %5\nCommit: %6")
          .arg(AUXLAB2_APP_NAME)
          .arg(AUXLAB2_VERSION)
          .arg(AUXLAB2_GIT_HASH)
          .arg(AUXLAB2_BUILD_DATETIME)
          .arg(auxeVersion)
          .arg(AUXE_GIT_HASH);
  QMessageBox::about(this, "About auxlab2", aboutText);
}
