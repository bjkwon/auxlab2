#include "SignalGraphWindow.h"

#include <QAudioFormat>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <utility>

namespace {
constexpr double kRmsDbOffset = 3.0103;

Qt::PenStyle penStyleForLine(const QString& lineStyle) {
  if (lineStyle == "--") {
    return Qt::DashLine;
  }
  if (lineStyle == ":") {
    return Qt::DotLine;
  }
  if (lineStyle == "-.") {
    return Qt::DashDotLine;
  }
  if (lineStyle == "none") {
    return Qt::NoPen;
  }
  return Qt::SolidLine;
}

bool hasMarker(const QString& marker) {
  const QString trimmed = marker.trimmed();
  return !trimmed.isEmpty();
}

void drawMarker(QPainter& p, const QPointF& pt, const QString& marker, int markerSize) {
  const QString m = marker.trimmed();
  if (m.isEmpty()) {
    return;
  }

  const qreal r = std::max(2, markerSize);
  if (m == "o") {
    p.drawEllipse(pt, r, r);
  } else if (m == ".") {
    p.drawEllipse(pt, 1.5, 1.5);
  } else if (m == "+") {
    p.drawLine(QPointF(pt.x() - r, pt.y()), QPointF(pt.x() + r, pt.y()));
    p.drawLine(QPointF(pt.x(), pt.y() - r), QPointF(pt.x(), pt.y() + r));
  } else if (m == "x") {
    p.drawLine(QPointF(pt.x() - r, pt.y() - r), QPointF(pt.x() + r, pt.y() + r));
    p.drawLine(QPointF(pt.x() - r, pt.y() + r), QPointF(pt.x() + r, pt.y() - r));
  } else if (m == "*") {
    p.drawLine(QPointF(pt.x() - r, pt.y()), QPointF(pt.x() + r, pt.y()));
    p.drawLine(QPointF(pt.x(), pt.y() - r), QPointF(pt.x(), pt.y() + r));
    p.drawLine(QPointF(pt.x() - r * 0.7, pt.y() - r * 0.7), QPointF(pt.x() + r * 0.7, pt.y() + r * 0.7));
    p.drawLine(QPointF(pt.x() - r * 0.7, pt.y() + r * 0.7), QPointF(pt.x() + r * 0.7, pt.y() - r * 0.7));
  } else if (m == "s") {
    p.drawRect(QRectF(pt.x() - r, pt.y() - r, 2 * r, 2 * r));
  } else if (m == "d") {
    QPolygonF poly;
    poly << QPointF(pt.x(), pt.y() - r) << QPointF(pt.x() + r, pt.y())
         << QPointF(pt.x(), pt.y() + r) << QPointF(pt.x() - r, pt.y());
    p.drawPolygon(poly);
  } else if (m == "^" || m == "v" || m == ">" || m == "<") {
    QPolygonF poly;
    if (m == "^") {
      poly << QPointF(pt.x(), pt.y() - r) << QPointF(pt.x() + r, pt.y() + r) << QPointF(pt.x() - r, pt.y() + r);
    } else if (m == "v") {
      poly << QPointF(pt.x() - r, pt.y() - r) << QPointF(pt.x() + r, pt.y() - r) << QPointF(pt.x(), pt.y() + r);
    } else if (m == ">") {
      poly << QPointF(pt.x() - r, pt.y() - r) << QPointF(pt.x() + r, pt.y()) << QPointF(pt.x() - r, pt.y() + r);
    } else {
      poly << QPointF(pt.x() + r, pt.y() - r) << QPointF(pt.x() - r, pt.y()) << QPointF(pt.x() + r, pt.y() + r);
    }
    p.drawPolygon(poly);
  } else {
    p.drawEllipse(pt, r, r);
  }
}

int timelineOffsetSamples(const SignalData& data) {
  if (!data.isAudio || data.sampleRate <= 0) {
    return 0;
  }
  return std::max(0, static_cast<int>(std::llround(data.startTimeSec * data.sampleRate)));
}

int totalTimelineSamples(const SignalData& data) {
  if (data.channels.empty()) {
    return 0;
  }
  return timelineOffsetSamples(data) + static_cast<int>(data.channels.front().samples.size());
}

std::array<double, 4> qtRectToMatlabFigurePos(const QRect& rect) {
  const QRect screen = QGuiApplication::primaryScreen()
                           ? QGuiApplication::primaryScreen()->availableGeometry()
                           : QRect(0, 0, 1440, 900);
  const int x = rect.x();
  const int width = rect.width();
  const int height = rect.height();
  const int yBottom = screen.y() + screen.height() - rect.y() - height;
  return {static_cast<double>(x),
          static_cast<double>(yBottom),
          static_cast<double>(width),
          static_cast<double>(height)};
}

QRect matlabFigurePosToQtRect(const std::array<double, 4>& pos) {
  const QRect screen = QGuiApplication::primaryScreen()
                           ? QGuiApplication::primaryScreen()->availableGeometry()
                           : QRect(0, 0, 1440, 900);
  const int x = static_cast<int>(std::llround(pos[0]));
  const int width = static_cast<int>(std::llround(pos[2]));
  const int height = static_cast<int>(std::llround(pos[3]));
  const int yBottom = static_cast<int>(std::llround(pos[1]));
  const int yTop = screen.y() + screen.height() - yBottom - height;
  return QRect(x, yTop, width, height);
}

double niceNumber(double x, bool roundValue) {
  if (x <= 0.0) {
    return 1.0;
  }
  const double exponent = std::floor(std::log10(x));
  const double fraction = x / std::pow(10.0, exponent);
  double niceFraction = 1.0;

  if (roundValue) {
    if (fraction < 1.5) {
      niceFraction = 1.0;
    } else if (fraction < 3.0) {
      niceFraction = 2.0;
    } else if (fraction < 7.0) {
      niceFraction = 5.0;
    } else {
      niceFraction = 10.0;
    }
  } else {
    if (fraction <= 1.0) {
      niceFraction = 1.0;
    } else if (fraction <= 2.0) {
      niceFraction = 2.0;
    } else if (fraction <= 5.0) {
      niceFraction = 5.0;
    } else {
      niceFraction = 10.0;
    }
  }
  return niceFraction * std::pow(10.0, exponent);
}

QString trimTrailingZeros(QString s) {
  while (s.contains('.') && s.endsWith('0')) {
    s.chop(1);
  }
  if (s.endsWith('.')) {
    s.chop(1);
  }
  return s;
}

QString formatSecondsCompact(double sec) {
  const double clamped = std::max(0.0, sec);
  if (clamped >= 60.0) {
    const int mins = static_cast<int>(std::floor(clamped / 60.0));
    const double rem = clamped - mins * 60.0;
    const double remRounded = std::round(rem);
    if (std::fabs(rem - remRounded) < 1e-6) {
      const int remInt = static_cast<int>(remRounded);
      if (remInt == 0) {
        return QString("%1m").arg(mins);
      }
      return QString("%1m%2s").arg(mins).arg(remInt);
    }
    return QString("%1m%2s").arg(mins).arg(trimTrailingZeros(QString::number(rem, 'f', 1)));
  }

  const double secRounded = std::round(clamped);
  if (std::fabs(clamped - secRounded) < 1e-6) {
    return QString::number(static_cast<int>(secRounded));
  }
  if (clamped >= 10.0) {
    return trimTrailingZeros(QString::number(clamped, 'f', 1));
  }
  if (clamped >= 1.0) {
    return trimTrailingZeros(QString::number(clamped, 'f', 2));
  }
  return trimTrailingZeros(QString::number(clamped, 'f', 3));
}

QString formatFrequencyTick(double hz) {
  if (std::fabs(hz) >= 1000.0) {
    return trimTrailingZeros(QString::number(hz / 1000.0, 'f', 2)) + "k";
  }
  return QString::number(static_cast<int>(std::llround(hz)));
}

// In-place iterative radix-2 FFT; data.size() must be a power of two.
void fftRadix2(std::vector<std::complex<double>>& data) {
  const size_t n = data.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    constexpr double kPi = 3.14159265358979323846;
    const double angle = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> wlen(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (size_t k = 0; k < len / 2; ++k) {
        const std::complex<double> u = data[i + k];
        const std::complex<double> v = data[i + k + len / 2] * w;
        data[i + k] = u + v;
        data[i + k + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

// Viridis-like colormap, t in [0, 1].
QRgb spectrogramColor(double t) {
  static const std::array<QColor, 5> stops = {QColor(68, 1, 84), QColor(59, 82, 139), QColor(33, 145, 140),
                                              QColor(94, 201, 98), QColor(253, 231, 37)};
  static const std::array<QRgb, 256> lut = [] {
    std::array<QRgb, 256> out{};
    for (int i = 0; i < 256; ++i) {
      const double pos = (i / 255.0) * (stops.size() - 1);
      const int k = std::min(static_cast<int>(pos), static_cast<int>(stops.size()) - 2);
      const double f = pos - k;
      const QColor& a = stops[static_cast<size_t>(k)];
      const QColor& b = stops[static_cast<size_t>(k + 1)];
      out[static_cast<size_t>(i)] = qRgb(static_cast<int>(a.red() + f * (b.red() - a.red())),
                                         static_cast<int>(a.green() + f * (b.green() - a.green())),
                                         static_cast<int>(a.blue() + f * (b.blue() - a.blue())));
    }
    return out;
  }();
  return lut[static_cast<size_t>(std::clamp(static_cast<int>(std::lround(t * 255.0)), 0, 255))];
}

QString formatSecondsWithSuffix(double sec) {
  const QString body = formatSecondsCompact(sec);
  return body.contains('m') || body.endsWith('s') ? body : body + "s";
}

QString formatStatusSeconds(double sec, double viewSpanSec) {
  const double clamped = std::isfinite(sec) ? std::max(0.0, sec) : 0.0;
  const double span = std::isfinite(viewSpanSec) ? std::max(0.0, viewSpanSec) : 0.0;
  const qint64 totalMs = std::max<qint64>(0, static_cast<qint64>(std::llround(clamped * 1000.0)));

  if (span < 60.0) {
    const qint64 totalSeconds = totalMs / 1000;
    const int minutes = static_cast<int>(totalSeconds / 60);
    const int seconds = static_cast<int>(totalSeconds % 60);
    const int ms = static_cast<int>(totalMs % 1000);
    return QString("%1:%2:%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
  }

  const qint64 totalSeconds = (totalMs + 500) / 1000;
  const qint64 minutesTotal = totalSeconds / 60;
  const int seconds = static_cast<int>(totalSeconds % 60);
  if (span < 3600.0) {
    return QString("%1:%2").arg(static_cast<int>(minutesTotal), 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
  }

  const qint64 hours = minutesTotal / 60;
  const int minutes = static_cast<int>(minutesTotal % 60);
  return QString("%1:%2:%3").arg(static_cast<int>(hours), 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

QString formatPlaybackSeconds(double sec) {
  const double clamped = std::isfinite(sec) ? std::max(0.0, sec) : 0.0;
  const qint64 totalSeconds = std::max<qint64>(0, static_cast<qint64>(std::floor(clamped)));
  const qint64 minutesTotal = totalSeconds / 60;
  const int seconds = static_cast<int>(totalSeconds % 60);
  if (minutesTotal < 60) {
    return QString("%1:%2")
        .arg(static_cast<int>(minutesTotal), 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }

  const qint64 hours = minutesTotal / 60;
  const int minutes = static_cast<int>(minutesTotal % 60);
  return QString("%1:%2:%3")
      .arg(static_cast<int>(hours), 2, 10, QChar('0'))
      .arg(minutes, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}

QString formatYTickLabel(double value, bool audioScale) {
  const double normalized = std::fabs(value) < 5e-10 ? 0.0 : value;
  if (audioScale) {
    if (normalized == 0.0) {
      return QStringLiteral("0");
    }
    return QString::number(normalized, 'f', 1);
  }

  QString label = QString::number(normalized, 'f', 2);
  if (label.contains('.')) {
    while (label.endsWith('0')) {
      label.chop(1);
    }
    if (label.endsWith('.')) {
      label.chop(1);
    }
  }
  return label;
}

int shiftSideFromEvent(const QKeyEvent* event) {
  if (!event || event->key() != Qt::Key_Shift) {
    return 0;
  }

  const quint32 nativeVirtualKey = event->nativeVirtualKey();
  const quint32 nativeScanCode = event->nativeScanCode();
#ifdef Q_OS_MAC
  if (nativeVirtualKey == 0x38 || nativeScanCode == 0x38) {
    return -1;
  }
  if (nativeVirtualKey == 0x3c || nativeScanCode == 0x3c) {
    return 1;
  }
#elif defined(Q_OS_WIN)
  if (nativeVirtualKey == 0xa0 || nativeScanCode == 0x2a) {
    return -1;
  }
  if (nativeVirtualKey == 0xa1 || nativeScanCode == 0x36) {
    return 1;
  }
#else
  if (nativeScanCode == 50 || nativeScanCode == 0x2a) {
    return -1;
  }
  if (nativeScanCode == 62 || nativeScanCode == 0x36) {
    return 1;
  }
#endif
  return 0;
}
}  // namespace

SignalGraphWindow::SignalGraphWindow(const QString& varName,
                                     const SignalDataPtr& data,
                                     CreationOptions options,
                                     QWidget* parent,
                                     FftProvider fftProvider)
    : QWidget(parent),
      varName_(varName),
      data_(data),
      options_(options),
      graphics_(GraphicsFigureModel::createSignalFigure(
          options.title.isEmpty() ? QString("Signal Graph - %1").arg(varName_) : options.title,
          *data,
          options.namedPlot,
          options.sourcePath)),
      fftProvider_(std::move(fftProvider)) {
  setWindowTitle(graphics_.figure().title);
  resize(900, 460);
  syncFigurePosFromWidget();
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  auto* dockButton = new QPushButton(QStringLiteral("Dock"), this);
  dockButton->setToolTip(QStringLiteral("Move this figure back into the main window"));
  dockButton->setVisible(false);
  dockButton->setFocusPolicy(Qt::NoFocus);
  dockButton_ = dockButton;
  connect(dockButton, &QPushButton::clicked, this, [this]() { emit dockRequested(); });

  if (!data_->channels.empty()) {
    viewStart_ = 0;
    viewLen_ = std::max(1, totalTimelineSamples(*data_));
    fftPaneOffsets_.assign(data_->channels.size(), QPoint(0, 0));
    rangeHistory_.push_back({viewStart_, viewStart_ + viewLen_});
    rangeHistoryIndex_ = 0;
  }
  syncVisibleXRangeToAxes();
  updateYRange();

  playheadTimer_.setInterval(16);
  connect(&playheadTimer_, &QTimer::timeout, this, &SignalGraphWindow::updatePlayhead);

  const auto bindRangeShortcut = [this](const QKeySequence& sequence, auto handler) {
    auto* shortcut = new QShortcut(sequence, this);
    connect(shortcut, &QShortcut::activated, this, handler);
  };

#ifdef Q_OS_MAC
  const Qt::KeyboardModifier rangeModifier = Qt::ControlModifier;
#else
  const Qt::KeyboardModifier rangeModifier = Qt::AltModifier;
#endif
  bindRangeShortcut(QKeySequence(rangeModifier | Qt::Key_Left), &SignalGraphWindow::anchorRangeStartToZero);
  bindRangeShortcut(QKeySequence(rangeModifier | Qt::Key_Right), &SignalGraphWindow::anchorRangeEndToSignalEnd);
  bindRangeShortcut(QKeySequence(rangeModifier | Qt::Key_Slash), &SignalGraphWindow::resetRangeToFull);
  bindRangeShortcut(QKeySequence(rangeModifier | Qt::Key_Comma), [this]() { stepRangeHistory(-1); });
  bindRangeShortcut(QKeySequence(rangeModifier | Qt::Key_Period), [this]() { stepRangeHistory(+1); });

  fftMoveHoldTimer_.setSingleShot(true);
  fftMoveHoldTimer_.setInterval(2000);
  connect(&fftMoveHoldTimer_, &QTimer::timeout, this, [this]() {
    if (fftMovePending_) {
      fftMoveReady_ = true;
      update();
    }
  });
}

SignalGraphWindow::~SignalGraphWindow() {
  stopPlayback();
}

QString SignalGraphWindow::varName() const {
  return varName_;
}

QString SignalGraphWindow::dockTitle() const {
  QString title = varName_.isEmpty() ? graphics_.figure().title : varName_;
  if (audioSink_ && audioSink_->state() != QAudio::StoppedState && data_->isAudio) {
    const QString timestamp = playbackTimestampText();
    if (!timestamp.isEmpty()) {
      title += QString(" - %1").arg(timestamp);
    }
  }
  return title;
}

void SignalGraphWindow::setWorkspaceActive(bool active) {
  workspaceActive_ = active;
  setEnabled(active);
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::updateData(const SignalDataPtr& data) {
  if (audioSink_ && data_ != data) {
    stopPlayback();
  }

  const int oldTotalLen = totalTimelineSamples(*data_);
  const int oldViewEnd = viewStart_ + std::max(0, viewLen_);
  const bool wasNearFullView =
      (oldTotalLen > 0 && viewStart_ <= 1 && oldViewEnd >= oldTotalLen - 1);
  const bool hadNoUsablePriorView = (oldTotalLen <= 0 || viewLen_ <= 0);

  data_ = data;
  graphics_.updateSignalData(*data_);
  updatePlaybackTitle();
  ++dataSerial_;
  fftComputed_ = false;
  fftDb_.clear();
  fftViewStart_ = -1;
  fftViewLen_ = -1;
  fftDataSerial_ = -1;
  if (!data_->channels.empty()) {
    const int totalLen = std::max(1, totalTimelineSamples(*data_));
    if (wasNearFullView || hadNoUsablePriorView) {
      // Keep showing the whole signal when user was viewing full extent.
      viewStart_ = 0;
      viewLen_ = totalLen;
    } else {
      viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
      viewLen_ = std::clamp(viewLen_, 1, std::max(1, totalLen));
    }
    rangeHistory_.clear();
    rangeHistory_.push_back({viewStart_, viewStart_ + viewLen_});
    rangeHistoryIndex_ = 0;
  }
  if (fftPaneOffsets_.size() < data_->channels.size()) {
    fftPaneOffsets_.resize(data_->channels.size(), QPoint(0, 0));
  } else if (fftPaneOffsets_.size() > data_->channels.size()) {
    fftPaneOffsets_.resize(data_->channels.size());
  }
  if (showFftOverlay_) {
    ensureFftData();
  }
  syncVisibleXRangeToAxes();
  updateYRange();
  invalidateStaticLayer();
  update();
}

std::uint64_t SignalGraphWindow::addAxes(const std::array<double, 4>& pos) {
  const auto axesId = graphics_.addAxes(pos);
  invalidateStaticLayer();
  update();
  return axesId;
}

std::uint64_t SignalGraphWindow::addLine(std::uint64_t axesId, const QVector<double>& xdata, const QVector<double>& ydata) {
  const auto lineId = graphics_.addLine(axesId, xdata, ydata);
  if (lineId == 0) {
    return 0;
  }
  applyRange({0, std::max(viewLen_, static_cast<int>(xdata.size()))});
  return lineId;
}

std::uint64_t SignalGraphWindow::addText(std::uint64_t parentId, double x, double y, const QString& text) {
  const auto textId = graphics_.addText(parentId, x, y, text);
  if (textId == 0) {
    return 0;
  }
  invalidateStaticLayer();
  update();
  return textId;
}

bool SignalGraphWindow::selectAxes(std::uint64_t axesId) {
  if (!graphics_.setCurrentAxes(axesId)) {
    return false;
  }
  update();
  return true;
}

bool SignalGraphWindow::removeAxes(std::uint64_t axesId) {
  if (!graphics_.removeAxes(axesId)) {
    return false;
  }
  invalidateStaticLayer();
  update();
  return true;
}

bool SignalGraphWindow::removeLine(std::uint64_t lineId) {
  if (!graphics_.removeLine(lineId)) {
    return false;
  }
  updateYRange();
  invalidateStaticLayer();
  update();
  return true;
}

bool SignalGraphWindow::removeText(std::uint64_t textId) {
  if (!graphics_.removeText(textId)) {
    return false;
  }
  invalidateStaticLayer();
  update();
  return true;
}

void SignalGraphWindow::applyStyleToAllLines(const std::optional<QColor>& color,
                                             const QString& marker,
                                             const QString& lineStyle) {
  graphics_.applyStyleToAllLines(color, marker, lineStyle);
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::applyXDataToAllLines(const QVector<double>& xdata) {
  graphics_.applyXDataToAllLines(xdata);
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::refreshGraphics() {
  syncFigurePosFromWidget();
  updateYRange();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::setAxesXLim(std::uint64_t axesId, const std::array<double, 2>& xlim) {
  auto* axes = graphics_.axesByIdMutable(axesId);
  if (!axes) {
    return;
  }
  axes->xlim = xlim;
  axes->autoXLim = false;
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::setAxesYLim(std::uint64_t axesId, const std::array<double, 2>& ylim) {
  auto* axes = graphics_.axesByIdMutable(axesId);
  if (!axes) {
    return;
  }
  axes->ylim = ylim;
  axes->autoYLim = false;
  invalidateStaticLayer();
  update();
}

std::optional<SignalGraphWindow::SelectedRange> SignalGraphWindow::selectedRangeCapture() const {
  const auto* axes = graphics_.leftChannelAxes();
  if (!axes) {
    return std::nullopt;
  }
  return selectedRangeCapture(axes->common.id);
}

std::optional<SignalGraphWindow::SelectedRange> SignalGraphWindow::selectedRangeCapture(std::uint64_t axesId) const {
  const Range sel = selectionForAxes(axesId);
  if (sel.end <= sel.start) {
    return std::nullopt;
  }

  const auto xRange = xRangeForSampleRange(axesId, sel);
  if (!xRange.has_value()) {
    return std::nullopt;
  }

  SelectedRange out;
  out.start = sel.start;
  out.end = sel.end;
  out.isAudio = data_->isAudio;
  out.sampleRate = data_->sampleRate;
  out.xStart = (*xRange)[0];
  out.xEnd = (*xRange)[1];
  out.endsAtSignalEnd = sel.end >= std::max(0, totalTimelineSamples(*data_) - 1);
  return out;
}

bool SignalGraphWindow::setSelectedRange(std::uint64_t axesId, double xStart, double xEnd) {
  const auto range = sampleRangeForXRange(axesId, xStart, xEnd);
  if (!range.has_value()) {
    return false;
  }

  const Range clamped = clampRange(*range);
  setSelectionForAxes(axesId, clamped);
  update();
  return true;
}

void SignalGraphWindow::clearSelectedRange(std::optional<std::uint64_t> axesId) {
  if (axesId.has_value() && !graphics_.isNamedPlot()) {
    axesSelectionRanges_.erase(*axesId);
  } else {
    axesSelectionRanges_.clear();
  }
  selStart_ = -1;
  selEnd_ = -1;
  update();
}

bool SignalGraphWindow::spectrumAvailable() const {
  return data_->isAudio && data_->sampleRate > 0 && !data_->channels.empty() && static_cast<bool>(fftProvider_);
}

bool SignalGraphWindow::spectrumVisible() const {
  return graphics_.spectrumVisible();
}

void SignalGraphWindow::setSpectrumVisible(bool visible) {
  if (visible && !spectrumAvailable()) {
    return;
  }
  graphics_.setSpectrumVisible(visible);
  spectrumDataSerial_ = -1;
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::ensureSpectrumData() {
  if (!graphics_.spectrumVisible() || !spectrumAvailable()) {
    return;
  }
  // Keep the last spectrum while a selection drag is in progress; recompute
  // once the drag settles.
  if (selecting_ && spectrumDataSerial_ == dataSerial_) {
    return;
  }

  Range range = clampRange({viewStart_, viewStart_ + std::max(1, viewLen_)});
  const Range sel = normalizedSelection();
  if (sel.end > sel.start) {
    range = clampRange(sel);
  }
  const int len = std::max(1, range.end - range.start);
  if (spectrumDataSerial_ == dataSerial_ && spectrumStart_ == range.start && spectrumLen_ == len) {
    return;
  }
  spectrumDataSerial_ = dataSerial_;
  spectrumStart_ = range.start;
  spectrumLen_ = len;

  // Normalize so the loudest bin across all channels sits at 0 dB; a shared
  // reference keeps the left/right level difference visible.
  constexpr double kSpectrumRangeDb = 80.0;
  constexpr double kRawFloorDb = -240.0;
  auto powerDb = fftProvider_(range.start, len, kRawFloorDb);
  double peakDb = kRawFloorDb;
  for (const auto& bins : powerDb) {
    for (const double db : bins) {
      peakDb = std::max(peakDb, db);
    }
  }
  for (auto& bins : powerDb) {
    for (double& db : bins) {
      db = std::max(-kSpectrumRangeDb, db - peakDb);
    }
  }

  const double nyquistHz = data_->sampleRate * 0.5;
  constexpr int kMaxSpectrumPoints = 2048;
  for (int ch = 0; ch < static_cast<int>(powerDb.size()); ++ch) {
    const auto& bins = powerDb[static_cast<size_t>(ch)];
    const int n = static_cast<int>(bins.size());
    QVector<double> freqHz;
    QVector<double> db;
    if (n > 0) {
      const double binHz = n > 1 ? nyquistHz / (n - 1) : 0.0;
      const int groups = std::min(n, kMaxSpectrumPoints);
      freqHz.reserve(groups);
      db.reserve(groups);
      for (int g = 0; g < groups; ++g) {
        const int b0 = static_cast<int>(static_cast<long long>(g) * n / groups);
        const int b1 = std::max(b0 + 1, static_cast<int>(static_cast<long long>(g + 1) * n / groups));
        // Keep each group's peak so narrow tones survive the decimation.
        int peak = b0;
        for (int b = b0 + 1; b < b1; ++b) {
          if (bins[static_cast<size_t>(b)] > bins[static_cast<size_t>(peak)]) {
            peak = b;
          }
        }
        freqHz.push_back(peak * binHz);
        db.push_back(bins[static_cast<size_t>(peak)]);
      }
    }
    graphics_.setSpectrumData(ch, freqHz, db, nyquistHz);
  }
  invalidateStaticLayer();
}

bool SignalGraphWindow::spectrogramAvailable() const {
  return data_->isAudio && data_->sampleRate > 0 && !data_->channels.empty();
}

bool SignalGraphWindow::spectrogramVisible() const {
  return graphics_.spectrogramVisible();
}

void SignalGraphWindow::setSpectrogramVisible(bool visible) {
  if (visible && !spectrogramAvailable()) {
    return;
  }
  graphics_.setSpectrogramVisible(visible);
  spectrogramImages_.clear();
  spectrogramDataSerial_ = -1;
  syncVisibleXRangeToAxes();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::ensureSpectrogramImages(int columns) {
  columns = std::max(1, columns);
  if (spectrogramDataSerial_ == dataSerial_ && spectrogramViewStart_ == viewStart_ &&
      spectrogramViewLen_ == viewLen_ && spectrogramColumns_ == columns) {
    return;
  }
  spectrogramDataSerial_ = dataSerial_;
  spectrogramViewStart_ = viewStart_;
  spectrogramViewLen_ = viewLen_;
  spectrogramColumns_ = columns;
  spectrogramImages_.clear();
  if (!spectrogramAvailable() || viewLen_ <= 0) {
    return;
  }

  // ~30 ms Hann window; one frame per pixel column, centered on the column's
  // time, so cost scales with the axes width rather than the view length.
  const int fs = data_->sampleRate;
  int nfft = 256;
  while (nfft * 2 <= fs * 0.032 && nfft < 4096) {
    nfft *= 2;
  }
  const int bins = nfft / 2 + 1;
  std::vector<double> window(static_cast<size_t>(nfft));
  for (int i = 0; i < nfft; ++i) {
    window[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (nfft - 1));
  }

  const int offset = timelineOffsetSamples(*data_);
  const int nChannels = static_cast<int>(data_->channels.size());
  std::vector<std::vector<float>> powerDb(static_cast<size_t>(nChannels));
  double peakDb = -300.0;
  std::vector<std::complex<double>> frame(static_cast<size_t>(nfft));
  for (int ch = 0; ch < nChannels; ++ch) {
    const auto& samples = data_->channels[static_cast<size_t>(ch)].samples;
    const int dataLen = static_cast<int>(samples.size());
    auto& out = powerDb[static_cast<size_t>(ch)];
    out.resize(static_cast<size_t>(columns) * bins);
    for (int c = 0; c < columns; ++c) {
      const double center = viewStart_ + (c + 0.5) * static_cast<double>(viewLen_) / columns;
      const int start = static_cast<int>(std::floor(center)) - nfft / 2 - offset;
      for (int i = 0; i < nfft; ++i) {
        const int di = start + i;
        const double v = (di >= 0 && di < dataLen) ? samples[static_cast<size_t>(di)] : 0.0;
        frame[static_cast<size_t>(i)] = std::complex<double>(std::isfinite(v) ? v * window[static_cast<size_t>(i)] : 0.0, 0.0);
      }
      fftRadix2(frame);
      for (int k = 0; k < bins; ++k) {
        const double db = 10.0 * std::log10(std::max(1e-30, std::norm(frame[static_cast<size_t>(k)])));
        out[static_cast<size_t>(c) * bins + k] = static_cast<float>(db);
        peakDb = std::max(peakDb, db);
      }
    }
  }

  // Same normalization as the spectrum: 0 dB = loudest bin across channels,
  // 80 dB of range below it.
  constexpr double kRangeDb = 80.0;
  for (int ch = 0; ch < nChannels; ++ch) {
    QImage image(columns, bins, QImage::Format_RGB32);
    const auto& db = powerDb[static_cast<size_t>(ch)];
    for (int k = 0; k < bins; ++k) {
      auto* row = reinterpret_cast<QRgb*>(image.scanLine(bins - 1 - k));
      for (int c = 0; c < columns; ++c) {
        const double t = (db[static_cast<size_t>(c) * bins + k] - peakDb + kRangeDb) / kRangeDb;
        row[c] = spectrogramColor(t);
      }
    }
    spectrogramImages_[ch] = std::move(image);
  }
}

void SignalGraphWindow::drawSpectrogram(QPainter& p, const QRect& area, const GraphicsAxesHandle& axes) {
  ensureSpectrogramImages(area.width());
  // In stereo overlay only the left spectrogram axes is visible; images cannot
  // be overlaid, so it shows whichever channel F2 brought to the foreground.
  int channel = axes.sourceChannel;
  if (channel == 0 && graphics_.stereoDisplayMode() == StereoDisplayMode::OverlayRightForeground) {
    channel = 1;
  }
  const auto it = spectrogramImages_.find(channel);
  if (it == spectrogramImages_.end() || it->second.isNull()) {
    return;
  }
  const QImage& image = it->second;
  // Rows span 0..Nyquist; crop to the axes' frequency limits.
  const double nyquistHz = data_->sampleRate * 0.5;
  const double lo = std::clamp(axes.ylim[0], 0.0, nyquistHz);
  const double hi = std::clamp(axes.ylim[1], 0.0, nyquistHz);
  const double ySpan = axes.ylim[1] - axes.ylim[0];
  if (hi <= lo || nyquistHz <= 0.0 || ySpan <= 0.0) {
    return;
  }
  const auto freqToY = [&](double hz) { return area.bottom() - (hz - axes.ylim[0]) / ySpan * area.height(); };
  const double rows = image.height();
  const QRectF source(0.0, (1.0 - hi / nyquistHz) * rows, image.width(), (hi - lo) / nyquistHz * rows);
  const QRectF target(area.left(), freqToY(hi), area.width(), freqToY(lo) - freqToY(hi));
  p.drawImage(target, image, source);
}

std::array<double, 4> SignalGraphWindow::currentFigurePos() const {
  return qtRectToMatlabFigurePos(geometry());
}

void SignalGraphWindow::applyFigurePos(const std::array<double, 4>& pos) {
  const QRect rect = matlabFigurePosToQtRect(pos);
  if (rect.isValid()) {
    setGeometry(rect);
  }
  syncFigurePosFromWidget();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::setDockButtonVisible(bool visible) {
  if (!dockButton_) {
    return;
  }
  dockButton_->setVisible(visible);
  updateDockButtonGeometry();
}

void SignalGraphWindow::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), graphics_.figure().common.color);

  const QRect plot = plotRect();
  ensureSpectrumData();
  ensureStaticLayer(plot);
  if (!staticLayer_.isNull()) {
    p.drawImage(QPoint(0, 0), staticLayer_);
  }

  for (const auto& axes : graphics_.axes()) {
    if (!axes.common.visible || axes.role == AxesRole::Spectrum) {
      continue;
    }
    // A spectrogram shares its waveform's time axis, so it mirrors that selection.
    const Range sel = selectionForAxes(axes.role == AxesRole::Spectrogram ? axes.sourceAxesId : axes.common.id);
    if (sel.end <= sel.start) {
      continue;
    }
    const QRect selectionRect = axesRectForPlot(axes, plot);
    const int x1 = sampleToX(selectionRect, sel.start);
    const int x2 = sampleToX(selectionRect, sel.end);
    p.fillRect(QRect(std::min(x1, x2), selectionRect.top(), std::abs(x2 - x1), selectionRect.height()),
               QColor(72, 120, 72, 110));
  }

  if (audioSink_ && audioSink_->state() != QAudio::StoppedState && workspaceActive_) {
    const int span = std::max(1, playingRange_.end - playingRange_.start);
    const qint64 processedUs = audioSink_->processedUSecs();
    double frac = (processedUs * 1e-6) * data_->sampleRate / static_cast<double>(span);
    frac = std::clamp(frac, 0.0, 1.0);
    int sample = playingRange_.start + static_cast<int>(span * frac);
    sample = std::clamp(sample, viewStart_, std::max(viewStart_, viewStart_ + viewLen_ - 1));
    p.setPen(QPen(QColor(255, 230, 120), 1));
    for (const auto& axes : graphics_.axes()) {
      if (!axes.common.visible || axes.role == AxesRole::Spectrum) {
        continue;
      }
      const QRect axesRect = axesRectForPlot(axes, plot);
      const int x = sampleToX(axesRect, sample);
      p.drawLine(x, axesRect.top(), x, axesRect.bottom());
    }
  }

  drawFftOverlays(p, plot);
  drawStatusBar(p);
}

SignalGraphWindow::Range SignalGraphWindow::clampRange(const Range& range) const {
  const int totalLen = std::max(1, totalTimelineSamples(*data_));
  const int start = std::clamp(range.start, 0, std::max(0, totalLen - 1));
  const int end = std::clamp(range.end, start + 1, totalLen);
  return {start, end};
}

SignalGraphWindow::Range SignalGraphWindow::fullRange() const {
  const int totalLen = std::max(1, totalTimelineSamples(*data_));
  return {0, totalLen};
}

bool SignalGraphWindow::isZoomedIn() const {
  const Range full = fullRange();
  return viewStart_ > full.start || viewStart_ + std::max(1, viewLen_) < full.end;
}

void SignalGraphWindow::recordRangeHistory(const Range& range) {
  const Range clamped = clampRange(range);
  if (!rangeHistory_.empty() && rangeHistoryIndex_ >= 0 &&
      rangeHistoryIndex_ < static_cast<int>(rangeHistory_.size())) {
    const Range& current = rangeHistory_[static_cast<size_t>(rangeHistoryIndex_)];
    if (current.start == clamped.start && current.end == clamped.end) {
      return;
    }
  }
  if (rangeHistoryIndex_ + 1 < static_cast<int>(rangeHistory_.size())) {
    rangeHistory_.erase(rangeHistory_.begin() + rangeHistoryIndex_ + 1, rangeHistory_.end());
  }
  rangeHistory_.push_back(clamped);
  rangeHistoryIndex_ = static_cast<int>(rangeHistory_.size()) - 1;
}

void SignalGraphWindow::applyRange(const Range& range, bool recordHistory) {
  if (data_->channels.empty()) {
    return;
  }
  const Range clamped = clampRange(range);
  viewStart_ = clamped.start;
  viewLen_ = std::max(1, clamped.end - clamped.start);
  if (recordHistory) {
    recordRangeHistory(clamped);
  }
  handlePlaybackAfterRangeChange();
  syncVisibleXRangeToAxes();
  updateYRange();
  invalidateStaticLayer();
  update();
}

bool SignalGraphWindow::canStepRangeHistory(int delta) const {
  const int next = rangeHistoryIndex_ + delta;
  return next >= 0 && next < static_cast<int>(rangeHistory_.size());
}

void SignalGraphWindow::stepRangeHistory(int delta) {
  if (!canStepRangeHistory(delta)) {
    return;
  }
  rangeHistoryIndex_ += delta;
  applyRange(rangeHistory_[static_cast<size_t>(rangeHistoryIndex_)], false);
}

void SignalGraphWindow::moveRangeStartToZero() {
  applyRange({0, std::max(1, viewLen_)});
}

void SignalGraphWindow::anchorRangeStartToZero() {
  applyRange({0, viewStart_ + std::max(1, viewLen_)});
}

void SignalGraphWindow::anchorRangeEndToSignalEnd() {
  const int totalLen = std::max(1, totalTimelineSamples(*data_));
  const int currentLen = std::max(1, viewLen_);
  applyRange({totalLen - currentLen, totalLen});
}

void SignalGraphWindow::resetRangeToFull() {
  applyRange(fullRange());
}

void SignalGraphWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Shift) {
    const int side = shiftSideFromEvent(event);
    if (side < 0) {
      activeShiftSide_ = ShiftSide::Left;
    } else if (side > 0) {
      activeShiftSide_ = ShiftSide::Right;
    }
  }

  const bool closeShortcut =
#ifdef Q_OS_MAC
      ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_W);
#else
      ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_W);
#endif
  if (closeShortcut) {
    close();
    event->accept();
    return;
  }

  if (!workspaceActive_) {
    return;
  }

#ifdef Q_OS_MAC
  const bool rangeShortcut = (event->modifiers() & Qt::ControlModifier);
#else
  const bool rangeShortcut = (event->modifiers() & Qt::AltModifier);
#endif

  if (rangeShortcut) {
    switch (event->key()) {
      case Qt::Key_Left:
        anchorRangeStartToZero();
        event->accept();
        return;
      case Qt::Key_Right:
        anchorRangeEndToSignalEnd();
        event->accept();
        return;
      case Qt::Key_Slash:
        resetRangeToFull();
        event->accept();
        return;
      case Qt::Key_Comma:
        stepRangeHistory(-1);
        event->accept();
        return;
      case Qt::Key_Period:
        stepRangeHistory(+1);
        event->accept();
        return;
      default:
        break;
    }
  }

  const Qt::KeyboardModifiers modifiers = event->modifiers();
  if ((modifiers & Qt::ShiftModifier) &&
      (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) &&
      nudgeSelectionWithShiftArrow(event->key() == Qt::Key_Right ? 1 : -1)) {
    event->accept();
    return;
  }

  const bool unmodifiedOrShiftOnly = modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier;
  if (unmodifiedOrShiftOnly && isZoomedIn()) {
    switch (event->key()) {
      case Qt::Key_Home:
      case Qt::Key_Comma:
      case Qt::Key_Less:
        moveRangeStartToZero();
        event->accept();
        return;
      case Qt::Key_End:
      case Qt::Key_Period:
      case Qt::Key_Greater:
        anchorRangeEndToSignalEnd();
        event->accept();
        return;
      case Qt::Key_Slash:
      case Qt::Key_Question:
        resetRangeToFull();
        event->accept();
        return;
      default:
        break;
    }
  }

  switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Up:
      zoomIn();
      break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
    case Qt::Key_Down:
      zoomOut();
      break;
    case Qt::Key_Left:
      panView(-1);
      break;
    case Qt::Key_Right:
      panView(+1);
      break;
    case Qt::Key_F2:
      cycleStereoMode();
      break;
    case Qt::Key_F4:
      if (event->modifiers() & Qt::ShiftModifier) {
        fftPaneOffsets_.assign(data_->channels.size(), QPoint(0, 0));
        update();
      } else {
        toggleFftOverlay();
      }
      break;
    case Qt::Key_Space:
      if (data_->isAudio) {
        togglePlayPause();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      const auto sel = normalizedSelection();
      if (sel.end > sel.start) {
        applyRange({std::max(0, sel.start), std::max(0, sel.end + 1)});
      }
      break;
    }
    case Qt::Key_Escape:
      stopPlayback();
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  event->accept();
}

void SignalGraphWindow::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Shift) {
    const int side = shiftSideFromEvent(event);
    if (side < 0 && activeShiftSide_ == ShiftSide::Left) {
      activeShiftSide_ = ShiftSide::Unknown;
    } else if (side > 0 && activeShiftSide_ == ShiftSide::Right) {
      activeShiftSide_ = ShiftSide::Unknown;
    } else if (!(event->modifiers() & Qt::ShiftModifier)) {
      activeShiftSide_ = ShiftSide::Unknown;
    }
  }
  QWidget::keyReleaseEvent(event);
}

void SignalGraphWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !workspaceActive_) {
    return QWidget::mousePressEvent(event);
  }

  if (showFftOverlay_) {
    const QRect plot = plotRect();
    const int nChannels = static_cast<int>(data_->channels.size());
    const auto panes = buildFftPaneLayouts(plot, nChannels);
    for (const auto& pane : panes) {
      if (pane.leftMargin.contains(event->pos())) {
        fftMovePending_ = true;
        fftMoveReady_ = false;
        fftMoveChannel_ = pane.channel;
        fftMovePressPos_ = event->pos();
        if (fftMoveChannel_ >= 0 && fftMoveChannel_ < static_cast<int>(fftPaneOffsets_.size()))
          fftMoveStartOffset_ = fftPaneOffsets_[static_cast<size_t>(fftMoveChannel_)];
        else
          fftMoveStartOffset_ = QPoint(0, 0);
        fftMoveHoldTimer_.start();
        event->accept();
        return;
      }
    }
  }

  updateHoverFromPoint(event->pos());
  std::uint64_t hitAxesId = axesIdAtPoint(event->pos());
  if (graphics_.axesRole(hitAxesId) == AxesRole::Spectrogram) {
    // Selecting on a spectrogram selects on the waveform above it.
    if (const auto* axes = graphics_.axesByIdMutable(hitAxesId)) {
      hitAxesId = axes->sourceAxesId;
    }
  }
  if (hitAxesId != 0 && graphics_.isSpectrumAxes(hitAxesId)) {
    // Spectrum axes have a frequency x-axis; time selection does not apply.
    update();
    event->accept();
    return;
  }
  selecting_ = true;
  selectingAxesId_ = hitAxesId;
  if (selectingAxesId_ == 0) {
    if (const auto* axes = graphics_.leftChannelAxes()) {
      selectingAxesId_ = axes->common.id;
    }
  }
  if ((event->modifiers() & Qt::ShiftModifier) && selectingAxesId_ != 0 &&
      extendSelectionAtPoint(selectingAxesId_, xToSample(event->pos()))) {
    selecting_ = false;
    selectingAxesId_ = 0;
    update();
    event->accept();
    return;
  }
  selStart_ = xToSample(event->pos());
  selEnd_ = selStart_;
  if (selectingAxesId_ != 0) {
    axesSelectionRanges_[selectingAxesId_] = {selStart_, selEnd_};
  }
  update();
}

void SignalGraphWindow::mouseMoveEvent(QMouseEvent* event) {
  if (fftMovePending_) {
    if (fftMoveReady_ && (event->buttons() & Qt::LeftButton)) {
      const QPoint delta = event->pos() - fftMovePressPos_;
      const QPoint desired = fftMoveStartOffset_ + delta;
      const QRect plot = plotRect();
      if (fftMoveChannel_ >= 0 && fftMoveChannel_ < static_cast<int>(fftPaneOffsets_.size())) {
        fftPaneOffsets_[static_cast<size_t>(fftMoveChannel_)] = clampFftPaneOffset(plot, desired, fftMoveChannel_);
      }
      update();
    }
    event->accept();
    return;
  }

  updateHoverFromPoint(event->pos());
  if (!selecting_) {
    update();
    return QWidget::mouseMoveEvent(event);
  }
  selEnd_ = xToSample(event->pos());
  if (selectingAxesId_ != 0) {
    const Range range{std::min(selStart_, selEnd_), std::max(selStart_, selEnd_)};
    if (graphics_.isNamedPlot()) {
      const Range clamped = clampRange(range);
      for (const auto& axes : graphics_.axes()) {
        axesSelectionRanges_[axes.common.id] = clamped;
      }
    } else {
      axesSelectionRanges_[selectingAxesId_] = range;
    }
  }
  update();
}

void SignalGraphWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (fftMovePending_) {
      fftMoveHoldTimer_.stop();
      fftMovePending_ = false;
      fftMoveReady_ = false;
      fftMoveChannel_ = -1;
      update();
      event->accept();
      return;
    }
    updateHoverFromPoint(event->pos());
    if (!selecting_) {
      // Press did not start a selection (spectrum axes or Shift-extend).
      update();
      return;
    }
    selecting_ = false;
    selEnd_ = xToSample(event->pos());
    if (selectingAxesId_ != 0) {
      const Range range{std::min(selStart_, selEnd_), std::max(selStart_, selEnd_)};
      if (range.end > range.start) {
        if (graphics_.isNamedPlot()) {
          setSelectionForAxes(selectingAxesId_, range);
        } else {
          axesSelectionRanges_[selectingAxesId_] = range;
        }
      } else if (graphics_.isNamedPlot()) {
        axesSelectionRanges_.clear();
      } else {
        axesSelectionRanges_.erase(selectingAxesId_);
      }
    }
    selectingAxesId_ = 0;
    update();
  }
}

void SignalGraphWindow::leaveEvent(QEvent* event) {
  if (!fftMovePending_) {
    hoverActive_ = false;
    hoverSample_ = -1;
  }
  update();
  QWidget::leaveEvent(event);
}

QRect SignalGraphWindow::axesRectForPlot(const GraphicsAxesHandle& axes, const QRect& plot) const {
  const auto& pos = axes.common.pos;
  const int left = plot.left() + static_cast<int>(std::llround(pos[0] * plot.width()));
  const int width = static_cast<int>(std::llround(pos[2] * plot.width()));
  const int height = static_cast<int>(std::llround(pos[3] * plot.height()));
  const int bottom = plot.bottom() - static_cast<int>(std::llround(pos[1] * plot.height()));
  const int top = bottom - height;
  return QRect(left, top, width, height);
}

void SignalGraphWindow::drawLine(QPainter& p, const QRect& area, const GraphicsAxesHandle& axes, const GraphicsLineHandle& line) {
  const QVector<double>& xdata = line.xdata;
  const QVector<double>& ydata = line.ydata;
  const bool deriveAudioX = data_->isAudio && data_->sampleRate > 0 && xdata.isEmpty();
  const bool manualAudioX = deriveAudioX && !axes.autoXLim;
  if (ydata.isEmpty() || viewLen_ <= 0) {
    return;
  }
  if (!deriveAudioX && (xdata.isEmpty() || xdata.size() != ydata.size())) {
    return;
  }

  const double xmin = axes.xlim[0];
  const double xmax = axes.xlim[1];
  const double yminAxis = axes.ylim[0];
  const double ymaxAxis = axes.ylim[1];
  const double xspan = std::max(1e-12, xmax - xmin);
  const double yspan = std::max(1e-12, ymaxAxis - yminAxis);

  int from = -1;
  int to = -1;
  if (deriveAudioX) {
    const int totalLen = ydata.size();
    if (manualAudioX) {
      const double sampleRate = static_cast<double>(data_->sampleRate);
      const double t0 = data_->startTimeSec;
      from = std::clamp(static_cast<int>(std::floor((xmin - t0) * sampleRate)), 0, std::max(0, totalLen - 1));
      to = std::clamp(static_cast<int>(std::ceil((xmax - t0) * sampleRate)) + 1, from + 1, totalLen);
    } else {
      from = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
      to = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);
    }
  } else {
    for (int i = 0; i < xdata.size(); ++i) {
      if (xdata[i] >= xmin && xdata[i] <= xmax) {
        if (from < 0) {
          from = i;
        }
        to = i + 1;
      }
    }
  }
  if (from < 0 || to <= from) {
    return;
  }

  p.setRenderHint(QPainter::Antialiasing, false);
  QPen pen(line.common.color, std::max(1, line.lineWidth));
  pen.setStyle(penStyleForLine(line.lineStyle));
  p.setPen(pen);

  const int width = std::max(1, area.width());
  const double samplesPerPixel = static_cast<double>(to - from) / width;
  if (samplesPerPixel <= 1.0 && pen.style() != Qt::NoPen) {
    QPainterPath path;
    bool segmentOpen = false;
    QVector<QPointF> markerPoints;
    for (int i = from; i < to; ++i) {
      const double y = ydata[i];
      if (!std::isfinite(y)) {
        segmentOpen = false;
        continue;
      }
      const double yNorm = (y - yminAxis) / yspan;
      double px = 0.0;
      if (deriveAudioX) {
        if (manualAudioX) {
          const double t = data_->startTimeSec + static_cast<double>(i) / static_cast<double>(data_->sampleRate);
          const double xNorm = (t - xmin) / xspan;
          px = area.left() + xNorm * area.width();
        } else {
          px = sampleToX(area, i);
        }
      } else {
        const double xNorm = (xdata[i] - xmin) / xspan;
        px = area.left() + xNorm * area.width();
      }
      const double py = area.bottom() - yNorm * area.height();
      if (!segmentOpen) {
        path.moveTo(px, py);
        segmentOpen = true;
      } else {
        path.lineTo(px, py);
      }
      if (hasMarker(line.marker)) {
        markerPoints.push_back(QPointF(px, py));
      }
    }
    p.drawPath(path);
    if (hasMarker(line.marker)) {
      const int markerCount = static_cast<int>(markerPoints.size());
      const int step = std::max(1, markerCount / 40);
      for (int i = 0; i < markerCount; i += step) {
        drawMarker(p, markerPoints[i], line.marker, line.markerSize);
      }
    }
    return;
  }

  QVector<QPointF> markerPoints;
  for (int x = 0; x < width; ++x) {
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();
    bool any = false;
    if (deriveAudioX) {
      int s0 = from;
      int s1 = to;
      if (manualAudioX) {
        const double sampleRate = static_cast<double>(data_->sampleRate);
        const double binStart = xmin + (xspan * x) / width;
        const double binEnd = xmin + (xspan * (x + 1)) / width;
        const double sampleStart = (binStart - data_->startTimeSec) * sampleRate;
        const double sampleEnd = (binEnd - data_->startTimeSec) * sampleRate;
        if (sampleEnd <= from || sampleStart >= to) {
          continue;
        }
        const int binStartSample = static_cast<int>(std::floor(sampleStart));
        const int binEndSample = static_cast<int>(std::ceil(sampleEnd));
        s0 = std::clamp(binStartSample, from, to - 1);
        s1 = std::clamp(binEndSample, s0 + 1, to);
      } else {
        const int total = std::max(1, viewLen_ - 1);
        const int binStartSample = viewStart_ + static_cast<int>(std::floor((static_cast<double>(x) * total) / width));
        const int binEndSample = viewStart_ + static_cast<int>(std::ceil((static_cast<double>(x + 1) * total) / width));
        s0 = std::clamp(binStartSample, from, to - 1);
        s1 = std::clamp(std::max(s0 + 1, binEndSample), s0 + 1, to);
      }
      for (int i = s0; i < s1; ++i) {
        const double v = ydata[i];
        if (!std::isfinite(v)) {
          continue;
        }
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
        any = true;
      }
    } else {
      const double binStart = xmin + (xspan * x) / width;
      const double binEnd = xmin + (xspan * (x + 1)) / width;
      for (int i = from; i < to; ++i) {
        if (xdata[i] < binStart || xdata[i] > binEnd) {
          continue;
        }
        const double v = ydata[i];
        if (!std::isfinite(v)) {
          continue;
        }
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
        any = true;
      }
    }
    if (!any) {
      continue;
    }
    const double y0Norm = (vmin - yminAxis) / yspan;
    const double y1Norm = (vmax - yminAxis) / yspan;
    const int px = area.left() + x;
    const int py0 = area.bottom() - static_cast<int>(y0Norm * area.height());
    const int py1 = area.bottom() - static_cast<int>(y1Norm * area.height());
    if (pen.style() != Qt::NoPen) {
      p.drawLine(px, py0, px, py1);
    }
    if (hasMarker(line.marker) && (x % std::max(1, width / 30) == 0)) {
      const int midY = (py0 + py1) / 2;
      markerPoints.push_back(QPointF(px, midY));
    }
  }
  if (hasMarker(line.marker)) {
    for (const auto& point : markerPoints) {
      drawMarker(p, point, line.marker, line.markerSize);
    }
  }
}

void SignalGraphWindow::cycleStereoMode() {
  if (data_->channels.size() < 2) {
    return;
  }
  switch (graphics_.stereoDisplayMode()) {
    case StereoDisplayMode::SplitAxes:
      graphics_.setStereoDisplayMode(StereoDisplayMode::OverlayLeftForeground);
      break;
    case StereoDisplayMode::OverlayLeftForeground:
      graphics_.setStereoDisplayMode(StereoDisplayMode::OverlayRightForeground);
      break;
    case StereoDisplayMode::OverlayRightForeground:
      graphics_.setStereoDisplayMode(StereoDisplayMode::SplitAxes);
      break;
  }
  updateYRange();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::zoomIn() {
  if (data_->channels.empty()) {
    return;
  }
  const int totalLen = totalTimelineSamples(*data_);
  if (totalLen <= 1) {
    return;
  }

  const int currentLen = std::clamp(viewLen_, 1, totalLen);
  const int center = viewStart_ + currentLen / 2;
  const int minLen = 32;
  const int newLen = std::max(minLen, currentLen / 2);
  const int nextLen = std::clamp(newLen, 1, totalLen);
  const int nextStart = std::clamp(center - nextLen / 2, 0, std::max(0, totalLen - nextLen));
  applyRange({nextStart, nextStart + nextLen});
}

void SignalGraphWindow::zoomOut() {
  if (data_->channels.empty()) {
    return;
  }
  const int totalLen = totalTimelineSamples(*data_);
  const int nextLen = std::min(totalLen, static_cast<int>(viewLen_ * 1.8));
  const int nextStart = std::clamp(viewStart_, 0, std::max(0, totalLen - nextLen));
  applyRange({nextStart, nextStart + nextLen});
}

void SignalGraphWindow::panView(int direction) {
  if (data_->channels.empty() || direction == 0) {
    return;
  }

  const int totalLen = totalTimelineSamples(*data_);
  const int currentLen = std::clamp(viewLen_, 1, std::max(1, totalLen));
  if (totalLen <= currentLen) {
    return;  // Full view: no panning room.
  }

  const int step = std::max(1, static_cast<int>(std::llround(currentLen * 0.25)));
  const int nextStart = std::clamp(viewStart_ + (direction > 0 ? step : -step), 0, std::max(0, totalLen - currentLen));
  applyRange({nextStart, nextStart + currentLen});
}

bool SignalGraphWindow::nudgeSelectionWithShiftArrow(int direction) {
  if (data_->channels.empty() || direction == 0 || activeShiftSide_ == ShiftSide::Unknown) {
    return false;
  }

  std::uint64_t axesId = 0;
  if (const auto* axes = graphics_.leftChannelAxes()) {
    const Range range = selectionForAxes(axes->common.id);
    if (range.end > range.start) {
      axesId = axes->common.id;
    }
  }
  if (axesId == 0) {
    for (const auto& [candidateAxesId, range] : axesSelectionRanges_) {
      if (range.end > range.start) {
        axesId = candidateAxesId;
        break;
      }
    }
  }
  if (axesId == 0) {
    return false;
  }

  const Range selected = selectionForAxes(axesId);
  const int totalLen = std::max(1, totalTimelineSamples(*data_));
  const int step = std::max(1, static_cast<int>(std::llround(std::max(1, viewLen_) / 100.0)));
  Range nudged = selected;
  if (activeShiftSide_ == ShiftSide::Left) {
    nudged.start = std::clamp(selected.start + direction * step, 0, selected.end - 1);
  } else {
    nudged.end = std::clamp(selected.end + direction * step, selected.start + 1, totalLen);
  }

  if (nudged.start == selected.start && nudged.end == selected.end) {
    return true;
  }
  setSelectionForAxes(axesId, nudged);
  update();
  return true;
}

void SignalGraphWindow::togglePlayPause() {
  if (!audioSink_) {
    startPlaybackForRange(activePlaybackRange());
    return;
  }

  if (audioSink_->state() == QAudio::ActiveState) {
    audioSink_->suspend();
  } else if (audioSink_->state() == QAudio::SuspendedState) {
    audioSink_->resume();
  } else {
    startPlaybackForRange(activePlaybackRange());
  }
}

void SignalGraphWindow::stopPlayback() {
  playheadTimer_.stop();
  if (audioSink_) {
    audioSink_->disconnect(this);
    audioSink_->stop();
    audioSink_->deleteLater();
    audioSink_ = nullptr;
  }
  if (audioBuffer_) {
    audioBuffer_->close();
    audioBuffer_->deleteLater();
    audioBuffer_ = nullptr;
  }
  pcmData_.clear();
  updatePlaybackTitle();
  update();
}

void SignalGraphWindow::startPlaybackForRange(const Range& range) {
  startPlaybackFromSample(range, range.start, false);
}

void SignalGraphWindow::startPlaybackFromSample(const Range& range, int startSample, bool startPaused) {
  if (!data_->isAudio || data_->channels.empty() || data_->sampleRate <= 0) {
    return;
  }

  stopPlayback();

  const int offset = timelineOffsetSamples(*data_);
  const int dataLen = static_cast<int>(data_->channels.front().samples.size());
  const int totalTimeline = totalTimelineSamples(*data_);
  if (dataLen <= 0 || totalTimeline <= 0) {
    return;
  }

  const int startTimeline = std::clamp(startSample, 0, totalTimeline - 1);
  const int endTimeline = std::clamp(range.end, startTimeline + 1, totalTimeline);
  if (endTimeline <= startTimeline) {
    return;
  }
  playingRange_ = {startTimeline, endTimeline};

  QAudioFormat fmt;
  fmt.setSampleRate(data_->sampleRate);
  fmt.setChannelCount(static_cast<int>(std::min<size_t>(2, data_->channels.size())));
  fmt.setSampleFormat(QAudioFormat::Int16);

  const int chCount = fmt.channelCount();
  const int frames = endTimeline - startTimeline;
  pcmData_.resize(frames * chCount * static_cast<int>(sizeof(qint16)));

  auto* out = reinterpret_cast<qint16*>(pcmData_.data());
  for (int ti = startTimeline; ti < endTimeline; ++ti) {
    const int di = ti - offset;
    for (int c = 0; c < chCount; ++c) {
      const auto& src = data_->channels[static_cast<size_t>(c)].samples;
      double v = 0.0;
      if (di >= 0 && di < static_cast<int>(src.size())) {
        v = std::clamp(src[static_cast<size_t>(di)], -1.0, 1.0);
      }
      *out++ = static_cast<qint16>(std::lrint(v * 32767.0));
    }
  }

  audioBuffer_ = new QBuffer(this);
  audioBuffer_->setData(pcmData_);
  audioBuffer_->open(QIODevice::ReadOnly);

  audioSink_ = new QAudioSink(fmt, this);
  connect(audioSink_, &QAudioSink::stateChanged, this, [this](QAudio::State st) {
    if (!audioSink_) {
      return;
    }
    if (st == QAudio::IdleState) {
      stopPlayback();
      return;
    }
    if (st == QAudio::StoppedState && audioSink_->error() != QAudio::NoError) {
      stopPlayback();
    }
  });

  playheadTimer_.start();
  audioSink_->start(audioBuffer_);
  if (startPaused && audioSink_) {
    audioSink_->suspend();
  }
  updatePlaybackTitle();
}

SignalGraphWindow::Range SignalGraphWindow::activePlaybackRange() const {
  auto sel = normalizedSelection();
  if (sel.end > sel.start) {
    return sel;
  }
  return {viewStart_, viewStart_ + viewLen_};
}

SignalGraphWindow::Range SignalGraphWindow::normalizedSelection() const {
  if (const auto* axes = graphics_.leftChannelAxes()) {
    const Range range = selectionForAxes(axes->common.id);
    if (range.end > range.start) {
      return range;
    }
  }
  if (selStart_ < 0 || selEnd_ < 0 || selStart_ == selEnd_) {
    return {};
  }
  return {std::min(selStart_, selEnd_), std::max(selStart_, selEnd_)};
}

SignalGraphWindow::Range SignalGraphWindow::selectionForAxes(std::uint64_t axesId) const {
  const auto it = axesSelectionRanges_.find(axesId);
  if (it == axesSelectionRanges_.end()) {
    return {};
  }
  return it->second;
}

void SignalGraphWindow::setSelectionForAxes(std::uint64_t axesId, const Range& range) {
  const Range clamped = clampRange(range);
  if (graphics_.isNamedPlot()) {
    for (const auto& axes : graphics_.axes()) {
      axesSelectionRanges_[axes.common.id] = clamped;
    }
  } else {
    axesSelectionRanges_[axesId] = clamped;
  }
  selStart_ = clamped.start;
  selEnd_ = clamped.end;
}

bool SignalGraphWindow::extendSelectionAtPoint(std::uint64_t axesId, int targetSample) {
  const Range selected = selectionForAxes(axesId);
  if (selected.end <= selected.start) {
    return false;
  }

  Range extended = selected;
  if (targetSample < selected.start) {
    extended.start = targetSample;
  } else if (targetSample > selected.end) {
    extended.end = targetSample;
  } else {
    const int distanceToStart = std::abs(targetSample - selected.start);
    const int distanceToEnd = std::abs(selected.end - targetSample);
    if (distanceToStart <= distanceToEnd) {
      extended.start = targetSample;
    } else {
      extended.end = targetSample;
    }
  }

  extended = {std::min(extended.start, extended.end), std::max(extended.start, extended.end)};
  if (extended.end <= extended.start) {
    return false;
  }
  setSelectionForAxes(axesId, extended);
  return true;
}

std::uint64_t SignalGraphWindow::axesIdAtPoint(const QPoint& pt) const {
  const QRect plot = plotRect();
  for (const auto& axes : graphics_.axes()) {
    if (!axes.common.visible) {
      continue;
    }
    if (axesRectForPlot(axes, plot).contains(pt)) {
      return axes.common.id;
    }
  }
  return 0;
}

std::optional<SignalGraphWindow::Range> SignalGraphWindow::sampleRangeForXRange(std::uint64_t axesId,
                                                                                double xStart,
                                                                                double xEnd) const {
  if (!std::isfinite(xStart) || !std::isfinite(xEnd) || xStart == xEnd) {
    return std::nullopt;
  }
  const auto axes = std::find_if(graphics_.axes().begin(), graphics_.axes().end(), [axesId](const GraphicsAxesHandle& ax) {
    return ax.common.id == axesId;
  });
  if (axes == graphics_.axes().end()) {
    return std::nullopt;
  }
  const auto lines = graphics_.linesForAxes(axesId);
  if (lines.empty() || !lines.front()) {
    return std::nullopt;
  }
  const auto* line = lines.front();
  const int totalLen = line->ydata.size();
  if (totalLen <= 0) {
    return std::nullopt;
  }

  const double lo = std::min(xStart, xEnd);
  const double hi = std::max(xStart, xEnd);
  if (data_->isAudio && data_->sampleRate > 0) {
    const double fs = static_cast<double>(data_->sampleRate);
    const int start = static_cast<int>(std::llround((lo - data_->startTimeSec) * fs));
    const int end = static_cast<int>(std::llround((hi - data_->startTimeSec) * fs));
    return Range{start, end};
  }

  const QVector<double>& xdata = line->xdata;
  if (xdata.isEmpty() || xdata.size() != totalLen) {
    return std::nullopt;
  }

  int first = -1;
  int last = -1;
  for (int i = 0; i < xdata.size(); ++i) {
    const double x = xdata[i];
    if (x >= lo && x <= hi) {
      if (first < 0) {
        first = i;
      }
      last = i;
    }
  }
  if (first < 0 || last < first) {
    return std::nullopt;
  }
  return Range{first, last + 1};
}

std::optional<std::array<double, 2>> SignalGraphWindow::xRangeForSampleRange(std::uint64_t axesId,
                                                                             const Range& range) const {
  const auto axes = std::find_if(graphics_.axes().begin(), graphics_.axes().end(), [axesId](const GraphicsAxesHandle& ax) {
    return ax.common.id == axesId;
  });
  if (axes == graphics_.axes().end()) {
    return std::nullopt;
  }
  const auto lines = graphics_.linesForAxes(axesId);
  if (lines.empty() || !lines.front()) {
    return std::nullopt;
  }
  const auto* line = lines.front();
  const int totalLen = line->ydata.size();
  if (totalLen <= 0) {
    return std::nullopt;
  }

  const Range clamped = clampRange(range);
  if (data_->isAudio && data_->sampleRate > 0) {
    const double fs = static_cast<double>(data_->sampleRate);
    return std::array<double, 2>{data_->startTimeSec + static_cast<double>(clamped.start) / fs,
                                 data_->startTimeSec + static_cast<double>(clamped.end) / fs};
  }

  const QVector<double>& xdata = line->xdata;
  if (xdata.isEmpty() || xdata.size() != totalLen) {
    return std::nullopt;
  }

  const int lastIndex = std::max(0, static_cast<int>(xdata.size()) - 1);
  const int start = std::clamp(clamped.start, 0, lastIndex);
  const int end = std::clamp(clamped.end - 1, start, lastIndex);
  return std::array<double, 2>{xdata[start], xdata[end]};
}

int SignalGraphWindow::currentPlaybackSample() const {
  if (!audioSink_) {
    return -1;
  }
  const int span = std::max(1, playingRange_.end - playingRange_.start);
  const qint64 processedUs = audioSink_->processedUSecs();
  double frac = (processedUs * 1e-6) * data_->sampleRate / static_cast<double>(span);
  frac = std::clamp(frac, 0.0, 1.0);
  int sample = playingRange_.start + static_cast<int>(span * frac);
  sample = std::clamp(sample, playingRange_.start, std::max(playingRange_.start, playingRange_.end - 1));
  return sample;
}

void SignalGraphWindow::handlePlaybackAfterRangeChange() {
  if (!audioSink_) {
    return;
  }
  const int sample = currentPlaybackSample();
  if (sample < 0) {
    return;
  }
  const int viewEnd = viewStart_ + std::max(1, viewLen_) - 1;
  if (sample < viewStart_ || sample > viewEnd) {
    stopPlayback();
    return;
  }

  const bool wasPaused = audioSink_->state() == QAudio::SuspendedState;
  const Range newRange = activePlaybackRange();
  if (sample < newRange.start || sample >= newRange.end) {
    stopPlayback();
    return;
  }

  if (sample != playingRange_.start || newRange.end != playingRange_.end) {
    startPlaybackFromSample(newRange, sample, wasPaused);
  }
}

QString SignalGraphWindow::playbackTimestampText() const {
  const int sample = currentPlaybackSample();
  if (!data_->isAudio || data_->sampleRate <= 0 || sample < 0) {
    return {};
  }
  const double sec = data_->startTimeSec + static_cast<double>(sample) / static_cast<double>(data_->sampleRate);
  return formatPlaybackSeconds(sec);
}

void SignalGraphWindow::updatePlaybackTitle() {
  QString title = graphics_.figure().title;
  if (audioSink_ && audioSink_->state() != QAudio::StoppedState && data_->isAudio) {
    const QString timestamp = playbackTimestampText();
    if (!timestamp.isEmpty()) {
      title += QString(" - %1").arg(timestamp);
    }
  }
  if (windowTitle() != title) {
    setWindowTitle(title);
  }
}

int SignalGraphWindow::xToSample(const QPoint& pt) const {
  const QRect ref = selectionReferenceRect();
  const int width = std::max(1, ref.width());
  const double t = (pt.x() - ref.left()) / static_cast<double>(width);
  const double clamped = std::clamp(t, 0.0, 1.0);
  const int span = std::max(0, viewLen_ - 1);
  return viewStart_ + static_cast<int>(std::llround(clamped * span));
}

QRect SignalGraphWindow::selectionReferenceRect() const {
  const QRect plot = plotRect();
  const auto* axes = graphics_.leftChannelAxes();
  if (!axes || !axes->common.visible) {
    return plot;
  }
  const QRect axesRect = axesRectForPlot(*axes, plot);
  return axesRect.isValid() ? axesRect : plot;
}

void SignalGraphWindow::updatePlayhead() {
  updatePlaybackTitle();
  update(plotRect());
}

void SignalGraphWindow::moveEvent(QMoveEvent* event) {
  QWidget::moveEvent(event);
  syncFigurePosFromWidget();
}

void SignalGraphWindow::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  syncFigurePosFromWidget();
  invalidateStaticLayer();
  updateDockButtonGeometry();
}

void SignalGraphWindow::updateDockButtonGeometry() {
  if (!dockButton_) {
    return;
  }
  const QSize hint = dockButton_->sizeHint();
  const int margin = 10;
  dockButton_->setGeometry(width() - hint.width() - margin, margin, hint.width(), hint.height());
  dockButton_->raise();
}

void SignalGraphWindow::syncFigurePosFromWidget() {
  graphics_.figureMutable().common.pos = currentFigurePos();
}

void SignalGraphWindow::updateYRange() {
  const auto applyAutoYLimToAllAxes = [this](const std::array<double, 2>& ylim) {
    for (const auto& axesConst : graphics_.axes()) {
      if (axesConst.isDerived()) {
        continue;
      }
      if (auto* axes = graphics_.axesByIdMutable(axesConst.common.id); axes && axes->autoYLim) {
        axes->ylim = ylim;
      }
    }
  };

  if (data_->isAudio) {
    if (data_->channels.empty() || data_->channels.front().samples.empty()) {
      yMin_ = -1.0;
      yMax_ = 1.0;
      applyAutoYLimToAllAxes({-1.0, 1.0});
      return;
    }
    yMin_ = -1.0;
    yMax_ = 1.0;
    applyAutoYLimToAllAxes({-1.0, 1.0});
    return;
  }

  yMin_ = std::numeric_limits<double>::max();
  yMax_ = std::numeric_limits<double>::lowest();
  bool any = false;
  for (const auto& line : graphics_.lines()) {
    if (line.ydata.isEmpty()) {
      continue;
    }
    const int totalLen = line.ydata.size();
    const int from = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
    const int end = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);
    for (int i = from; i < end; ++i) {
      const double v = line.ydata[i];
      yMin_ = std::min(yMin_, v);
      yMax_ = std::max(yMax_, v);
      any = true;
    }
  }
  if (!any) {
    yMin_ = -1.0;
    yMax_ = 1.0;
  } else if (std::fabs(yMax_ - yMin_) < 1e-12) {
    yMin_ -= 1.0;
    yMax_ += 1.0;
  }

  for (const auto& axesConst : graphics_.axes()) {
    auto* axes = graphics_.axesByIdMutable(axesConst.common.id);
    if (!axes || !axes->autoYLim || axes->isDerived()) {
      continue;
    }

    double axesYMin = std::numeric_limits<double>::max();
    double axesYMax = std::numeric_limits<double>::lowest();
    bool axesAny = false;
    const auto lines = graphics_.linesForAxes(axes->common.id);
    for (const auto* line : lines) {
      if (!line || line->ydata.isEmpty()) {
        continue;
      }
      const int totalLen = line->ydata.size();
      const int from = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
      const int end = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);
      for (int i = from; i < end; ++i) {
        const double v = line->ydata[i];
        if (!std::isfinite(v)) {
          continue;
        }
        axesYMin = std::min(axesYMin, v);
        axesYMax = std::max(axesYMax, v);
        axesAny = true;
      }
    }

    if (!axesAny) {
      axes->ylim = {-1.0, 1.0};
      continue;
    }
    if (std::fabs(axesYMax - axesYMin) < 1e-12) {
      axesYMin -= 1.0;
      axesYMax += 1.0;
    }
    axes->ylim = {axesYMin, axesYMax};
  }
}

void SignalGraphWindow::syncVisibleXRangeToAxes() {
  if (graphics_.axes().empty() || viewLen_ <= 0) {
    return;
  }

  for (const auto& axesConst : graphics_.axes()) {
    auto* axes = graphics_.axesByIdMutable(axesConst.common.id);
    if (!axes || axes->role == AxesRole::Spectrum) {
      continue;
    }
    if (axes->role == AxesRole::Spectrogram) {
      // No lines: time follows the view, frequency spans 0..Nyquist.
      if (data_->isAudio && data_->sampleRate > 0) {
        const double fs = static_cast<double>(data_->sampleRate);
        if (axes->autoXLim) {
          axes->xlim = {data_->startTimeSec + viewStart_ / fs,
                        data_->startTimeSec + (viewStart_ + std::max(1, viewLen_) - 1) / fs};
        }
        if (axes->autoYLim) {
          axes->ylim = {0.0, fs * 0.5};
        }
      }
      continue;
    }
    if (!axes->autoXLim) {
      continue;
    }
    const auto lines = graphics_.linesForAxes(axes->common.id);
    if (lines.empty()) {
      continue;
    }
    const auto* line = lines.front();
    if (!line) {
      continue;
    }

    if (data_->isAudio && data_->sampleRate > 0) {
      const double x0 = data_->startTimeSec + static_cast<double>(viewStart_) / static_cast<double>(data_->sampleRate);
      const double x1 = data_->startTimeSec +
                        static_cast<double>(viewStart_ + std::max(1, viewLen_) - 1) /
                            static_cast<double>(data_->sampleRate);
      axes->xlim = {x0, x1};
      continue;
    }

    if (line->xdata.isEmpty()) {
      continue;
    }
    const int totalLen = line->xdata.size();
    const int from = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
    const int to = std::clamp(viewStart_ + std::max(1, viewLen_) - 1, from, totalLen - 1);
    axes->xlim = {line->xdata[from], line->xdata[to]};
  }
}

void SignalGraphWindow::invalidateStaticLayer() {
  staticLayerValid_ = false;
}

int SignalGraphWindow::sampleToX(const QRect& plot, int sample) const {
  const int total = std::max(1, viewLen_ - 1);
  const double frac = std::clamp((sample - viewStart_) / static_cast<double>(total), 0.0, 1.0);
  return plot.left() + static_cast<int>(std::llround(frac * plot.width()));
}

void SignalGraphWindow::ensureStaticLayer(const QRect& plot) {
  const bool needsRebuild = !staticLayerValid_ ||
                            staticLayer_.size() != size() ||
                            cachedDataSerial_ != dataSerial_ ||
                            cachedViewStart_ != viewStart_ ||
                            cachedViewLen_ != viewLen_ ||
                            std::fabs(cachedYMin_ - yMin_) > 1e-12 ||
                            std::fabs(cachedYMax_ - yMax_) > 1e-12 ||
                            cachedStereoDisplayMode_ != graphics_.stereoDisplayMode() ||
                            cachedWorkspaceActive_ != workspaceActive_ ||
                            staticPlotRect_ != plot;
  if (!needsRebuild) {
    return;
  }

  staticLayer_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
  staticLayer_.fill(graphics_.figure().common.color);
  QPainter p(&staticLayer_);

  if (!workspaceActive_) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawRect(plot);
    p.setPen(Qt::white);
    p.drawText(plot, Qt::AlignCenter, "Inactive (different workspace scope)");
  } else if (graphics_.lines().empty()) {
    p.setPen(Qt::white);
    p.drawText(plot, Qt::AlignCenter, "No signal data");
  } else {
    for (const auto& axes : graphics_.axes()) {
      if (!axes.common.visible) {
        continue;
      }
      const QRect axesRect = axesRectForPlot(axes, plot);
      const int xTickCount = 7;
      const int yTickCount = 5;
      const bool isSpectrum = axes.role == AxesRole::Spectrum;
      const bool isSpectrogram = axes.role == AxesRole::Spectrogram;
      const bool xIsTime = !isSpectrum && data_->isAudio && data_->sampleRate > 0;
      const double xStartVal = axes.xlim[0];
      const double xEndVal = axes.xlim[1];
      const double xSpan = std::max(1e-12, xEndVal - xStartVal);
      const double yStartVal = axes.ylim[0];
      const double yEndVal = axes.ylim[1];
      std::vector<double> xTicks;
      xTicks.reserve(10);
      if (xIsTime) {
        const bool longRange = xEndVal >= 60.0 || xSpan >= 60.0;
        const double rawStep = xSpan / 6.0;
        double step = niceNumber(rawStep, true);
        if (longRange) {
          step = std::max(1.0, std::round(step));
        }
        const double eps = step * 1e-6;
        double t = std::ceil((xStartVal - eps) / step) * step;
        while (t <= xEndVal + eps) {
          if (t >= xStartVal - eps) {
            xTicks.push_back(t);
          }
          t += step;
        }
        if (xTicks.empty()) {
          xTicks.push_back(xEndVal);
        } else {
          const double diff = std::fabs(xTicks.back() - xEndVal);
          const bool farInValue = diff > std::max(1e-6, step * 0.1);
          const double pxDist = (diff / std::max(1e-12, xSpan)) * axesRect.width();
          constexpr double kMinEndpointLabelSpacingPx = 56.0;
          if (farInValue && pxDist >= kMinEndpointLabelSpacingPx) {
            xTicks.push_back(xEndVal);
          }
        }
      } else if (isSpectrum) {
        const double step = niceNumber(xSpan / 5.0, true);
        const double eps = step * 1e-6;
        for (double f = std::ceil((xStartVal - eps) / step) * step; f <= xEndVal + eps; f += step) {
          xTicks.push_back(f);
        }
      } else {
        for (int i = 0; i < xTickCount; ++i) {
          const double v = xStartVal + (xSpan * i) / (xTickCount - 1);
          xTicks.push_back(v);
        }
      }

      if (xTicks.size() > 8) {
        std::vector<double> thinned;
        thinned.reserve(8);
        const size_t stride = static_cast<size_t>(std::ceil(xTicks.size() / 8.0));
        for (size_t i = 0; i < xTicks.size(); i += std::max<size_t>(1, stride)) {
          thinned.push_back(xTicks[i]);
        }
        if (!xTicks.empty() && (thinned.empty() || std::fabs(thinned.back() - xTicks.back()) > 1e-9)) {
          thinned.push_back(xTicks.back());
        }
        xTicks.swap(thinned);
      }

      if (axes.xgrid) {
        p.setPen(QColor(112, 120, 112));
        for (double tick : xTicks) {
          const double frac = std::clamp((tick - xStartVal) / std::max(1e-12, xSpan), 0.0, 1.0);
          const int x = axesRect.left() + static_cast<int>(std::llround(frac * axesRect.width()));
          p.drawLine(x, axesRect.top(), x, axesRect.bottom());
        }
      }
      if (axes.ygrid) {
        p.setPen(QColor(112, 120, 112));
        for (int i = 0; i < yTickCount; ++i) {
          const int y = axesRect.bottom() - (i * axesRect.height()) / (yTickCount - 1);
          p.drawLine(axesRect.left(), y, axesRect.right(), y);
        }
      }

      p.fillRect(axesRect, axes.common.color);
      if (isSpectrogram) {
        drawSpectrogram(p, axesRect, axes);
      }
      if (axes.box) {
        p.setPen(QPen(QColor(40, 40, 40), std::max(1, axes.lineWidth)));
        p.drawRect(axesRect);
      }
      for (const auto* line : graphics_.linesForAxes(axes.common.id)) {
        drawLine(p, axesRect, axes, *line);
      }
      if (axes.showXTickLabels) {
        p.setPen(QColor(36, 36, 36));
        for (double tick : xTicks) {
          const double frac = std::clamp((tick - xStartVal) / std::max(1e-12, xSpan), 0.0, 1.0);
          const int x = axesRect.left() + static_cast<int>(std::llround(frac * axesRect.width()));
          p.drawLine(x, axesRect.bottom(), x, axesRect.bottom() + 4);
          QString label;
          if (xIsTime) {
            label = formatSecondsCompact(tick);
          } else if (isSpectrum) {
            label = formatFrequencyTick(tick);
          } else {
            label = QString::number(static_cast<int>(std::llround(tick)));
          }
          p.drawText(QRect(x - 42, axesRect.bottom() + 7, 84, 16), Qt::AlignHCenter | Qt::AlignTop, label);
        }
      }

      for (int i = 0; i < yTickCount; ++i) {
        const int y = axesRect.bottom() - (i * axesRect.height()) / (yTickCount - 1);
        const double v = yStartVal + ((yEndVal - yStartVal) * i) / (yTickCount - 1);
        p.drawLine(axesRect.left() - 4, y, axesRect.left(), y);
        if (isSpectrogram && i == yTickCount - 1) {
          continue;  // Top edge is shared with the waveform's bottom label.
        }
        const QString label = isSpectrogram ? formatFrequencyTick(v)
                                            : formatYTickLabel(v, data_->isAudio && !isSpectrum);
        const QRect labelRect(plot.left(), y - 8, std::max(0, axesRect.left() - plot.left() - 8), 16);
        p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
      }
    }

    p.setPen(QColor(24, 24, 24));
    for (const auto& text : graphics_.texts()) {
      if (!text.common.visible || text.stringValue.isEmpty()) {
        continue;
      }
      QRect parentRect = plot;
      if (text.common.parentId != graphics_.figure().common.id) {
        auto axIt = std::find_if(graphics_.axes().begin(), graphics_.axes().end(), [&text](const GraphicsAxesHandle& axes) {
          return axes.common.id == text.common.parentId;
        });
        if (axIt == graphics_.axes().end() || !axIt->common.visible) {
          continue;
        }
        parentRect = axesRectForPlot(*axIt, plot);
      }
      const int px = parentRect.left() + static_cast<int>(std::llround(text.common.pos[0] * parentRect.width()));
      const int py = parentRect.bottom() - static_cast<int>(std::llround(text.common.pos[1] * parentRect.height()));
      p.drawText(QPoint(px, py), text.stringValue);
    }
  }

  staticLayerValid_ = true;
  staticPlotRect_ = plot;
  cachedDataSerial_ = dataSerial_;
  cachedViewStart_ = viewStart_;
  cachedViewLen_ = viewLen_;
  cachedYMin_ = yMin_;
  cachedYMax_ = yMax_;
  cachedStereoDisplayMode_ = graphics_.stereoDisplayMode();
  cachedWorkspaceActive_ = workspaceActive_;
}

QRect SignalGraphWindow::plotRect() const {
  // Reserve bottom area for the quick-read status bar; axes margins (see
  // kDefaultMonoAxesPos et al.) reserve their own tick-label space.
  return rect().adjusted(6, 6, -6, -30);
}

void SignalGraphWindow::updateHoverFromPoint(const QPoint& pt) {
  hoverInFft_ = false;
  hoverFftValue_ = 0.0;
  hoverFftFreqHz_ = 0.0;
  hoverSpectrogramHz_ = -1.0;
  hoverXCoord_ = 0.0;

  if (showFftOverlay_ && data_->isAudio && data_->sampleRate > 0) {
    ensureFftData();
    const int nChannels = std::min(static_cast<int>(data_->channels.size()), static_cast<int>(fftDb_.size()));
    const auto panes = buildFftPaneLayouts(plotRect(), nChannels);
    for (const auto& pane : panes) {
      if (!pane.inner.contains(pt)) {
        continue;
      }
      const double x01 = std::clamp((pt.x() - pane.inner.left()) / static_cast<double>(std::max(1, pane.inner.width())), 0.0, 1.0);
      const double y01 = std::clamp((pt.y() - pane.inner.top()) / static_cast<double>(std::max(1, pane.inner.height())), 0.0, 1.0);
      hoverInFft_ = true;
      hoverFftValue_ = -80.0 * y01; // top=0 dB, bottom=-80 dB
      hoverFftFreqHz_ = x01 * (data_->sampleRate * 0.5);
      hoverActive_ = true;
      hoverSample_ = -1;
      return;
    }
  }

  const QRect plot = plotRect();
  if (!plot.contains(pt)) {
    hoverActive_ = false;
    hoverSample_ = -1;
    return;
  }

  const GraphicsAxesHandle* axes = nullptr;
  QRect axesRect;
  for (const auto& candidate : graphics_.axes()) {
    if (!candidate.common.visible) {
      continue;
    }
    const QRect candidateRect = axesRectForPlot(candidate, plot);
    if (candidateRect.contains(pt)) {
      axes = &candidate;
      axesRect = candidateRect;
      break;
    }
  }
  if (!axes) {
    hoverActive_ = false;
    hoverSample_ = -1;
    hoverValue_ = 0.0;
    return;
  }

  hoverActive_ = true;
  const double x01 = std::clamp((pt.x() - axesRect.left()) / static_cast<double>(std::max(1, axesRect.width())), 0.0, 1.0);
  hoverXCoord_ = axes->xlim[0] + x01 * (axes->xlim[1] - axes->xlim[0]);
  const double y01 = std::clamp((axesRect.bottom() - pt.y()) / static_cast<double>(std::max(1, axesRect.height())), 0.0, 1.0);
  if (axes->role == AxesRole::Spectrogram) {
    // Time comes from the shared x-axis below; add the frequency under the cursor.
    hoverSpectrogramHz_ = axes->ylim[0] + y01 * (axes->ylim[1] - axes->ylim[0]);
  }
  if (axes->role == AxesRole::Spectrum) {
    // Reuse the FFT-overlay readout: "(dB, Hz)".
    hoverInFft_ = true;
    hoverFftFreqHz_ = hoverXCoord_;
    hoverFftValue_ = axes->ylim[0] + y01 * (axes->ylim[1] - axes->ylim[0]);
    hoverSample_ = -1;
    return;
  }
  if (data_->isAudio && data_->sampleRate > 0) {
    const double samplePos = (hoverXCoord_ - data_->startTimeSec) * static_cast<double>(data_->sampleRate);
    hoverSample_ = std::clamp(static_cast<int>(std::llround(samplePos)), 0, std::max(0, totalTimelineSamples(*data_) - 1));
    hoverValue_ = std::numeric_limits<double>::quiet_NaN();
    return;
  } else {
    hoverSample_ = xToSample(pt);
  }
  const auto lines = graphics_.linesForAxes(axes->common.id);
  if (lines.empty()) {
    hoverValue_ = 0.0;
    return;
  }
  const auto* line = lines.front();
  if (line && hoverSample_ >= 0 && hoverSample_ < line->ydata.size()) {
    hoverValue_ = line->ydata[hoverSample_];
  } else {
    hoverValue_ = std::numeric_limits<double>::quiet_NaN();
  }
}

QString SignalGraphWindow::formatStatusTimeValue(int sample, double viewSpanSec) const {
  if (data_->isAudio && data_->sampleRate > 0) {
    const double sec = static_cast<double>(sample) / static_cast<double>(data_->sampleRate);
    return formatStatusSeconds(sec, viewSpanSec);
  }

  const auto* axes = graphics_.leftChannelAxes();
  if (axes) {
    const auto lines = graphics_.linesForAxes(axes->common.id);
    if (!lines.empty()) {
      const auto* line = lines.front();
      if (line && sample >= 0 && sample < line->xdata.size()) {
        const double x = line->xdata[sample];
        const double rounded = std::round(x);
        if (std::fabs(x - rounded) < 1e-9) {
          return QString::number(static_cast<long long>(rounded));
        }
        return QString::number(x, 'g', 8);
      }
    }
  }

  return QString::number(sample + 1);
}

QString SignalGraphWindow::formatRmsInfo(const Range& range) const {
  if (!data_->isAudio) {
    return {};
  }
  if (data_->channels.empty()) {
    return "[dBRMS] -";
  }
  if (cachedRmsDataSerial_ == dataSerial_ && cachedRmsRange_.start == range.start &&
      cachedRmsRange_.end == range.end) {
    return cachedRmsText_;
  }

  const int totalTimeline = std::max(1, totalTimelineSamples(*data_));
  const int start = std::clamp(range.start, 0, totalTimeline - 1);
  const int end = std::clamp(range.end, start + 1, totalTimeline);
  const int offset = timelineOffsetSamples(*data_);

  QString out = "[dBRMS]";
  for (size_t idx = 0; idx < data_->channels.size(); ++idx) {
    const auto& ch = data_->channels[idx];
    const int d0 = std::max(0, start - offset);
    const int d1 = std::min(static_cast<int>(ch.samples.size()), end - offset);
    const bool isFullChannel = d0 == 0 && d1 == static_cast<int>(ch.samples.size());
    if (isFullChannel && idx < data_->fullRmsDb.size()) {
      const double rmsDb = data_->fullRmsDb[idx];
      if (std::isinf(rmsDb)) {
        out += rmsDb > 0 ? " inf" : " -inf";
      } else {
        out += QString(" %1").arg(rmsDb, 0, 'f', 1);
      }
      continue;
    }
    if (d1 <= d0) {
      out += " -inf";
      continue;
    }

    long double sumSq = 0.0;
    for (int i = d0; i < d1; ++i) {
      const double v = ch.samples[static_cast<size_t>(i)];
      sumSq += static_cast<long double>(v) * static_cast<long double>(v);
    }
    const long double mean = sumSq / static_cast<long double>(d1 - d0);
    if (mean <= 0.0) {
      out += " -inf";
      continue;
    }
    const double rmsDb = 20.0 * std::log10(std::sqrt(static_cast<double>(mean))) + kRmsDbOffset;
    out += QString(" %1").arg(rmsDb, 0, 'f', 1);
  }
  cachedRmsDataSerial_ = dataSerial_;
  cachedRmsRange_ = range;
  cachedRmsText_ = out;
  return out;
}

void SignalGraphWindow::toggleFftOverlay() {
  if (!data_->isAudio || data_->channels.empty() || data_->sampleRate <= 0) {
    showFftOverlay_ = false;
    return;
  }
  showFftOverlay_ = !showFftOverlay_;
  if (showFftOverlay_) {
    ensureFftData();
  }
  update();
}

void SignalGraphWindow::ensureFftData() {
  const bool stale = !fftComputed_ || fftViewStart_ != viewStart_ || fftViewLen_ != viewLen_ || fftDataSerial_ != dataSerial_;
  if (!stale) {
    return;
  }
  fftComputed_ = true;
  fftViewStart_ = viewStart_;
  fftViewLen_ = viewLen_;
  fftDataSerial_ = dataSerial_;
  fftDb_.clear();
  if (!fftProvider_) {
    return;
  }
  fftDb_ = fftProvider_(viewStart_, viewLen_, std::nullopt);
}

std::vector<SignalGraphWindow::FftPaneLayout> SignalGraphWindow::buildFftPaneLayouts(const QRect& plot, int nChannels) const {
  std::vector<FftPaneLayout> out;
  if (nChannels <= 0) {
    return out;
  }
  const int insetW = std::max(140, static_cast<int>(std::llround(plot.width() * 0.20)));
  const int insetH = std::max(90, static_cast<int>(std::llround(insetW * 0.62)));
  const int gap = 8;
  const int rightMargin = 8;
  const int topMargin = 8;

  for (int ch = 0; ch < nChannels; ++ch) {
    const QPoint off = (ch >= 0 && ch < static_cast<int>(fftPaneOffsets_.size())) ? fftPaneOffsets_[static_cast<size_t>(ch)] : QPoint(0, 0);
    const int x = plot.right() - insetW - rightMargin + off.x();
    const int y = plot.top() + topMargin + ch * (insetH + gap) + off.y();
    QRect box(x, y, insetW, insetH);
    QRect inner = box.adjusted(28, 14, -8, -18);
    QRect leftMargin(box.left(), box.top(), std::max(0, inner.left() - box.left()), box.height());
    if (inner.width() < 20 || inner.height() < 20) {
      continue;
    }
    out.push_back({ch, box, inner, leftMargin});
  }
  return out;
}

QPoint SignalGraphWindow::clampFftPaneOffset(const QRect& plot, const QPoint& desired, int channelIndex) const {
  if (channelIndex < 0) {
    return QPoint(0, 0);
  }
  const int insetW = std::max(140, static_cast<int>(std::llround(plot.width() * 0.20)));
  const int insetH = std::max(90, static_cast<int>(std::llround(insetW * 0.62)));
  const int gap = 8;
  const int rightMargin = 8;
  const int topMargin = 8;
  const int baseX = plot.right() - insetW - rightMargin;
  const int baseY = plot.top() + topMargin + channelIndex * (insetH + gap);
  const int minX = plot.left() - baseX;
  const int maxX = plot.right() - insetW - baseX;
  const int minY = plot.top() - baseY;
  const int maxY = plot.bottom() - insetH - baseY;
  return QPoint(std::clamp(desired.x(), minX, maxX), std::clamp(desired.y(), minY, maxY));
}

void SignalGraphWindow::drawFftOverlays(QPainter& p, const QRect& plot) {
  if (!showFftOverlay_ || !workspaceActive_ || !data_->isAudio || data_->sampleRate <= 0) {
    return;
  }
  ensureFftData();
  if (fftDb_.empty()) {
    return;
  }

  const int nChannels = std::min(static_cast<int>(data_->channels.size()), static_cast<int>(fftDb_.size()));
  if (nChannels <= 0) {
    return;
  }

  const QColor chColors[2] = {QColor(28, 62, 178), QColor(255, 86, 86)};
  const auto panes = buildFftPaneLayouts(plot, nChannels);
  for (int ch = 0; ch < static_cast<int>(panes.size()); ++ch) {
    const QRect box = panes[static_cast<size_t>(ch)].box;
    const QRect inner = panes[static_cast<size_t>(ch)].inner;

    const bool activePane = fftMoveReady_ && fftMovePending_ && fftMoveChannel_ == panes[static_cast<size_t>(ch)].channel;
    const QColor paneFill = activePane ? QColor(210, 236, 210, 235) : QColor(238, 238, 228, 230);
    p.fillRect(box, paneFill);
    p.setPen(QColor(80, 80, 80));
    p.drawRect(box);

    p.setPen(QColor(155, 155, 155));
    for (int t = 0; t <= 4; ++t) {
      const int yy = inner.top() + (t * inner.height()) / 4;
      p.drawLine(inner.left(), yy, inner.right(), yy);
    }
    for (int t = 0; t <= 2; ++t) {
      const int xx = inner.left() + (t * inner.width()) / 2;
      p.drawLine(xx, inner.top(), xx, inner.bottom());
    }

    const auto& db = fftDb_[static_cast<size_t>(ch)];
    if (!db.empty()) {
      QPainterPath path;
      bool first = true;
      const int n = static_cast<int>(db.size());
      for (int i = 0; i < n; ++i) {
        const double xf = (n <= 1) ? 0.0 : static_cast<double>(i) / (n - 1);
        const double clampedDb = std::clamp(db[static_cast<size_t>(i)], -80.0, 0.0);
        const double yf = (0.0 - clampedDb) / 80.0;
        const double px = inner.left() + xf * inner.width();
        const double py = inner.top() + yf * inner.height();
        if (first) {
          path.moveTo(px, py);
          first = false;
        } else {
          path.lineTo(px, py);
        }
      }
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(chColors[ch % 2], 1.3));
      p.drawPath(path);
      p.setRenderHint(QPainter::Antialiasing, false);
    }

    p.setPen(QColor(35, 35, 35));
    p.drawText(QRect(inner.left() - 24, inner.top() - 6, 22, 12), Qt::AlignRight | Qt::AlignVCenter, "0");
    p.drawText(QRect(inner.left() - 24, inner.bottom() - 6, 22, 12), Qt::AlignRight | Qt::AlignVCenter, "-80");
    p.drawText(QRect(inner.left(), inner.bottom() + 2, 40, 12), Qt::AlignLeft | Qt::AlignVCenter, "0");
    p.drawText(QRect(inner.right() - 56, inner.bottom() + 2, 56, 12), Qt::AlignRight | Qt::AlignVCenter,
               QString("%1").arg(data_->sampleRate / 2));
    p.drawText(QRect(inner.left() - 24, inner.bottom() + 10, 48, 12), Qt::AlignLeft | Qt::AlignVCenter, "[Hz]");
  }
}

void SignalGraphWindow::drawStatusBar(QPainter& p) const {
  const QRect bar = rect().adjusted(0, rect().height() - 30, 0, 0);
  p.fillRect(bar, QColor(224, 224, 224));
  p.setPen(QColor(88, 88, 88));
  p.drawLine(bar.topLeft(), bar.topRight());

  const Range sel = normalizedSelection();
  const bool hasSel = sel.end > sel.start;
  const int totalTimeline = std::max(1, totalTimelineSamples(*data_));
  const Range rmsRange = hasSel ? sel : Range{0, totalTimeline};
  const auto* axes = graphics_.leftChannelAxes();
  const double audioViewSpanSec =
      (data_->isAudio && data_->sampleRate > 0)
          ? (axes ? std::fabs(axes->xlim[1] - axes->xlim[0])
                  : static_cast<double>(std::max(1, viewLen_) - 1) / static_cast<double>(data_->sampleRate))
          : 0.0;

  const QString mouseText =
      (hoverActive_ && hoverInFft_)
          ? QString("(%1, %2 Hz)").arg(hoverFftValue_, 0, 'f', 2).arg(hoverFftFreqHz_, 0, 'f', 1)
          : ((hoverActive_ && hoverSample_ >= 0)
                 ? (data_->isAudio ? formatStatusSeconds(hoverXCoord_, audioViewSpanSec) +
                                         (hoverSpectrogramHz_ >= 0.0
                                              ? QString(", %1 Hz").arg(hoverSpectrogramHz_, 0, 'f', 0)
                                              : QString())
                                    : QString("(%1,%2)")
                                          .arg(QString::number(hoverXCoord_, 'g', 4))
                                          .arg(std::isfinite(hoverValue_) ? QString::number(hoverValue_, 'f', 3) : QString("null")))
                 : QString());
  const QString viewStartText =
      axes ? (data_->isAudio ? formatStatusSeconds(axes->xlim[0], audioViewSpanSec) : QString::number(axes->xlim[0], 'g', 6))
           : formatStatusTimeValue(viewStart_, audioViewSpanSec);
  const QString viewEndText =
      axes ? (data_->isAudio ? formatStatusSeconds(axes->xlim[1], audioViewSpanSec) : QString::number(axes->xlim[1], 'g', 6))
           : formatStatusTimeValue(viewStart_ + std::max(1, viewLen_) - 1, audioViewSpanSec);
  const QString selStartText = hasSel ? formatStatusTimeValue(sel.start, audioViewSpanSec) : QString();
  const QString selEndText = hasSel ? formatStatusTimeValue(sel.end, audioViewSpanSec) : QString();
  const QString rmsText = formatRmsInfo(rmsRange);

  const QStringList cells = {mouseText, viewStartText, viewEndText, selStartText, selEndText, rmsText};
  const QFontMetrics fm(p.font());
  const int pad = 20;
  const int hoverPrefWidth =
      hoverInFft_ || hoverSpectrogramHz_ >= 0.0 || !data_->isAudio || audioViewSpanSec < 60.0 ? 120 : 72;
  const int minWidths[] = {44, 58, 58, 58, 58, 118};
  const int prefWidths[] = {
      hoverPrefWidth,
      std::clamp(fm.horizontalAdvance(viewStartText) + pad, minWidths[1], 86),
      std::clamp(fm.horizontalAdvance(viewEndText) + pad, minWidths[2], 86),
      std::clamp(fm.horizontalAdvance(selStartText) + pad, minWidths[3], 86),
      std::clamp(fm.horizontalAdvance(selEndText) + pad, minWidths[4], 86),
      std::max(150, fm.horizontalAdvance(rmsText) + pad),
  };

  int widths[6] = {};
  int used = 0;
  for (int i = 0; i < 6; ++i) {
    widths[i] = minWidths[i];
    used += widths[i];
  }

  int extra = std::max(0, bar.width() - used);
  for (int i = 0; i < 5 && extra > 0; ++i) {
    const int grow = std::min(extra, prefWidths[i] - widths[i]);
    widths[i] += grow;
    extra -= grow;
  }
  widths[5] += extra;

  int x = 0;
  for (int i = 0; i < cells.size(); ++i) {
    const QRect c(x, bar.top() + 1, widths[i], bar.height() - 1);
    p.setPen(QColor(140, 140, 140));
    p.drawRect(c.adjusted(0, 0, -1, -1));
    p.setPen(QColor(18, 18, 18));
    p.drawText(c.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(cells[i], Qt::ElideRight, c.width() - 12));
    x += widths[i];
    if (x >= width()) {
      break;
    }
  }
}
