#include "SignalGraphWindow.h"

#include <QAudioFormat>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>

#include <algorithm>
#include <cmath>
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
}  // namespace

SignalGraphWindow::SignalGraphWindow(const QString& varName,
                                     const SignalData& data,
                                     CreationOptions options,
                                     QWidget* parent,
                                     FftProvider fftProvider)
    : QWidget(parent),
      varName_(varName),
      data_(data),
      options_(options),
      graphics_(GraphicsFigureModel::createSignalFigure(
          options.title.isEmpty() ? QString("Signal Graph - %1").arg(varName_) : options.title,
          data,
          options.namedPlot,
          options.sourcePath)),
      fftProvider_(std::move(fftProvider)) {
  setWindowTitle(graphics_.figure().title);
  resize(900, 460);
  syncFigurePosFromWidget();
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  if (!data_.channels.empty()) {
    viewStart_ = 0;
    viewLen_ = std::max(1, totalTimelineSamples(data_));
    fftPaneOffsets_.assign(data_.channels.size(), QPoint(0, 0));
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

void SignalGraphWindow::setWorkspaceActive(bool active) {
  workspaceActive_ = active;
  setEnabled(active);
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::updateData(const SignalData& data) {
  const int oldTotalLen = totalTimelineSamples(data_);
  const int oldViewEnd = viewStart_ + std::max(0, viewLen_);
  const bool wasNearFullView =
      (oldTotalLen > 0 && viewStart_ <= 1 && oldViewEnd >= oldTotalLen - 1);

  data_ = data;
  graphics_.updateSignalData(data_);
  setWindowTitle(graphics_.figure().title);
  ++dataSerial_;
  fftComputed_ = false;
  fftDb_.clear();
  fftViewStart_ = -1;
  fftViewLen_ = -1;
  fftDataSerial_ = -1;
  if (!data_.channels.empty()) {
    const int totalLen = std::max(1, totalTimelineSamples(data_));
    if (wasNearFullView) {
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
  if (fftPaneOffsets_.size() < data_.channels.size()) {
    fftPaneOffsets_.resize(data_.channels.size(), QPoint(0, 0));
  } else if (fftPaneOffsets_.size() > data_.channels.size()) {
    fftPaneOffsets_.resize(data_.channels.size());
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

void SignalGraphWindow::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), graphics_.figure().common.color);

  const QRect plot = plotRect();
  ensureStaticLayer(plot);
  if (!staticLayer_.isNull()) {
    p.drawImage(QPoint(0, 0), staticLayer_);
  }

  if (selStart_ >= 0 && selEnd_ >= 0 && selStart_ != selEnd_) {
    const int s = std::min(selStart_, selEnd_);
    const int e = std::max(selStart_, selEnd_);
    int x1 = sampleToX(plot, s);
    int x2 = sampleToX(plot, e);
    p.fillRect(QRect(std::min(x1, x2), plot.top(), std::abs(x2 - x1), plot.height()), QColor(72, 120, 72, 110));
  }

  if (audioSink_ && audioSink_->state() != QAudio::StoppedState && workspaceActive_) {
    const int span = std::max(1, playingRange_.end - playingRange_.start);
    const qint64 processedUs = audioSink_->processedUSecs();
    double frac = (processedUs * 1e-6) * data_.sampleRate / static_cast<double>(span);
    frac = std::clamp(frac, 0.0, 1.0);
    int sample = playingRange_.start + static_cast<int>(span * frac);
    sample = std::clamp(sample, viewStart_, std::max(viewStart_, viewStart_ + viewLen_ - 1));
    p.setPen(QPen(QColor(255, 230, 120), 1));
    for (const auto& axes : graphics_.axes()) {
      if (!axes.common.visible) {
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
  const int totalLen = std::max(1, totalTimelineSamples(data_));
  const int start = std::clamp(range.start, 0, std::max(0, totalLen - 1));
  const int end = std::clamp(range.end, start + 1, totalLen);
  return {start, end};
}

SignalGraphWindow::Range SignalGraphWindow::fullRange() const {
  const int totalLen = std::max(1, totalTimelineSamples(data_));
  return {0, totalLen};
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
  if (data_.channels.empty()) {
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

void SignalGraphWindow::anchorRangeStartToZero() {
  applyRange({0, viewStart_ + std::max(1, viewLen_)});
}

void SignalGraphWindow::anchorRangeEndToSignalEnd() {
  const int totalLen = std::max(1, totalTimelineSamples(data_));
  const int currentLen = std::max(1, viewLen_);
  applyRange({totalLen - currentLen, totalLen});
}

void SignalGraphWindow::resetRangeToFull() {
  applyRange(fullRange());
}

void SignalGraphWindow::keyPressEvent(QKeyEvent* event) {
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
        fftPaneOffsets_.assign(data_.channels.size(), QPoint(0, 0));
        update();
      } else {
        toggleFftOverlay();
      }
      break;
    case Qt::Key_Space:
      if (data_.isAudio) {
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

void SignalGraphWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !workspaceActive_) {
    return QWidget::mousePressEvent(event);
  }

  if (showFftOverlay_) {
    const QRect plot = plotRect();
    const int nChannels = static_cast<int>(data_.channels.size());
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
  selecting_ = true;
  selStart_ = xToSample(event->pos());
  selEnd_ = selStart_;
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
    selecting_ = false;
    selEnd_ = xToSample(event->pos());
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
  const bool deriveAudioX = data_.isAudio && data_.sampleRate > 0 && xdata.isEmpty();
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
    from = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
    to = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);
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
    bool first = true;
    QVector<QPointF> markerPoints;
    for (int i = from; i < to; ++i) {
      const double y = ydata[i];
      const double yNorm = (y - yminAxis) / yspan;
      double px = 0.0;
      if (deriveAudioX) {
        px = sampleToX(area, i);
      } else {
        const double xNorm = (xdata[i] - xmin) / xspan;
        px = area.left() + xNorm * area.width();
      }
      const double py = area.bottom() - yNorm * area.height();
      if (first) {
        path.moveTo(px, py);
        first = false;
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
      const int total = std::max(1, viewLen_ - 1);
      const int binStartSample = viewStart_ + static_cast<int>(std::floor((static_cast<double>(x) * total) / width));
      const int binEndSample = viewStart_ + static_cast<int>(std::ceil((static_cast<double>(x + 1) * total) / width));
      const int s0 = std::clamp(binStartSample, from, to - 1);
      const int s1 = std::clamp(std::max(s0 + 1, binEndSample), s0 + 1, to);
      for (int i = s0; i < s1; ++i) {
        const double v = ydata[i];
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
  if (data_.channels.size() < 2) {
    return;
  }
  graphics_.setStereoOverlay(!graphics_.stereoOverlay());
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::zoomIn() {
  if (data_.channels.empty()) {
    return;
  }
  const int totalLen = totalTimelineSamples(data_);
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
  if (data_.channels.empty()) {
    return;
  }
  const int totalLen = totalTimelineSamples(data_);
  const int nextLen = std::min(totalLen, static_cast<int>(viewLen_ * 1.8));
  const int nextStart = std::clamp(viewStart_, 0, std::max(0, totalLen - nextLen));
  applyRange({nextStart, nextStart + nextLen});
}

void SignalGraphWindow::panView(int direction) {
  if (data_.channels.empty() || direction == 0) {
    return;
  }

  const int totalLen = totalTimelineSamples(data_);
  const int currentLen = std::clamp(viewLen_, 1, std::max(1, totalLen));
  if (totalLen <= currentLen) {
    return;  // Full view: no panning room.
  }

  const int step = std::max(1, static_cast<int>(std::llround(currentLen * 0.25)));
  const int nextStart = std::clamp(viewStart_ + (direction > 0 ? step : -step), 0, std::max(0, totalLen - currentLen));
  applyRange({nextStart, nextStart + currentLen});
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
  update();
}

void SignalGraphWindow::startPlaybackForRange(const Range& range) {
  startPlaybackFromSample(range, range.start, false);
}

void SignalGraphWindow::startPlaybackFromSample(const Range& range, int startSample, bool startPaused) {
  if (!data_.isAudio || data_.channels.empty() || data_.sampleRate <= 0) {
    return;
  }

  stopPlayback();

  const int offset = timelineOffsetSamples(data_);
  const int dataLen = static_cast<int>(data_.channels.front().samples.size());
  const int totalTimeline = totalTimelineSamples(data_);
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
  fmt.setSampleRate(data_.sampleRate);
  fmt.setChannelCount(static_cast<int>(std::min<size_t>(2, data_.channels.size())));
  fmt.setSampleFormat(QAudioFormat::Int16);

  const int chCount = fmt.channelCount();
  const int frames = endTimeline - startTimeline;
  pcmData_.resize(frames * chCount * static_cast<int>(sizeof(qint16)));

  auto* out = reinterpret_cast<qint16*>(pcmData_.data());
  for (int ti = startTimeline; ti < endTimeline; ++ti) {
    const int di = ti - offset;
    for (int c = 0; c < chCount; ++c) {
      const auto& src = data_.channels[static_cast<size_t>(c)].samples;
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
}

SignalGraphWindow::Range SignalGraphWindow::activePlaybackRange() const {
  auto sel = normalizedSelection();
  if (sel.end > sel.start) {
    return sel;
  }
  return {viewStart_, viewStart_ + viewLen_};
}

SignalGraphWindow::Range SignalGraphWindow::normalizedSelection() const {
  if (selStart_ < 0 || selEnd_ < 0 || selStart_ == selEnd_) {
    return {};
  }
  return {std::min(selStart_, selEnd_), std::max(selStart_, selEnd_)};
}

int SignalGraphWindow::currentPlaybackSample() const {
  if (!audioSink_) {
    return -1;
  }
  const int span = std::max(1, playingRange_.end - playingRange_.start);
  const qint64 processedUs = audioSink_->processedUSecs();
  double frac = (processedUs * 1e-6) * data_.sampleRate / static_cast<double>(span);
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

int SignalGraphWindow::xToSample(const QPoint& pt) const {
  QRect plot = plotRect();
  const int width = std::max(1, plot.width());
  const double t = (pt.x() - plot.left()) / static_cast<double>(width);
  const double clamped = std::clamp(t, 0.0, 1.0);
  const int span = std::max(0, viewLen_ - 1);
  return viewStart_ + static_cast<int>(std::llround(clamped * span));
}

void SignalGraphWindow::updatePlayhead() {
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
}

void SignalGraphWindow::syncFigurePosFromWidget() {
  graphics_.figureMutable().common.pos = currentFigurePos();
}

void SignalGraphWindow::updateYRange() {
  if (data_.isAudio) {
    if (data_.channels.empty() || data_.channels.front().samples.empty()) {
      yMin_ = -1.0;
      yMax_ = 1.0;
      return;
    }
    yMin_ = -1.0;
    yMax_ = 1.0;
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
    return;
  }
  if (std::fabs(yMax_ - yMin_) < 1e-12) {
    yMin_ -= 1.0;
    yMax_ += 1.0;
  }
}

void SignalGraphWindow::syncVisibleXRangeToAxes() {
  if (graphics_.axes().empty() || viewLen_ <= 0) {
    return;
  }

  for (const auto& axesConst : graphics_.axes()) {
    auto* axes = graphics_.axesByIdMutable(axesConst.common.id);
    if (!axes) {
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

    if (data_.isAudio && data_.sampleRate > 0) {
      const double x0 = data_.startTimeSec + static_cast<double>(viewStart_) / static_cast<double>(data_.sampleRate);
      const double x1 = data_.startTimeSec +
                        static_cast<double>(viewStart_ + std::max(1, viewLen_) - 1) /
                            static_cast<double>(data_.sampleRate);
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
                            cachedStereoOverlay_ != graphics_.stereoOverlay() ||
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
      const bool xIsTime = data_.isAudio && data_.sampleRate > 0;
      const double xStartVal = axes.xlim[0];
      const double xEndVal = axes.xlim[1];
      const double xSpan = std::max(1e-12, xEndVal - xStartVal);
      const double yStartVal = axes.ylim[0];
      const double yEndVal = axes.ylim[1];
      const double ySpan = std::max(1e-12, yEndVal - yStartVal);
      const int yDigits = ySpan < 0.1 ? 4 : (ySpan < 1.0 ? 3 : 2);
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
      if (axes.box) {
        p.setPen(QPen(QColor(40, 40, 40), std::max(1, axes.lineWidth)));
        p.drawRect(axesRect);
      }
      for (const auto* line : graphics_.linesForAxes(axes.common.id)) {
        drawLine(p, axesRect, axes, *line);
      }
      p.setPen(QColor(36, 36, 36));
      for (double tick : xTicks) {
        const double frac = std::clamp((tick - xStartVal) / std::max(1e-12, xSpan), 0.0, 1.0);
        const int x = axesRect.left() + static_cast<int>(std::llround(frac * axesRect.width()));
        p.drawLine(x, axesRect.bottom(), x, axesRect.bottom() + 4);
        QString label;
        if (xIsTime) {
          label = formatSecondsCompact(tick);
        } else {
          label = QString::number(static_cast<int>(std::llround(tick)));
        }
        p.drawText(QRect(x - 42, axesRect.bottom() + 7, 84, 16), Qt::AlignHCenter | Qt::AlignTop, label);
      }

      for (int i = 0; i < yTickCount; ++i) {
        const int y = axesRect.bottom() - (i * axesRect.height()) / (yTickCount - 1);
        const double v = yStartVal + ((yEndVal - yStartVal) * i) / (yTickCount - 1);
        p.drawLine(axesRect.left() - 4, y, axesRect.left(), y);
        const QString label = QString::number(v, 'f', yDigits);
        p.drawText(QRect(2, y - 8, axesRect.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, label);
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
  cachedStereoOverlay_ = graphics_.stereoOverlay();
  cachedWorkspaceActive_ = workspaceActive_;
}

QRect SignalGraphWindow::plotRect() const {
  // Reserve bottom area for quick-read status fields.
  return rect().adjusted(50, 20, -20, -78);
}

void SignalGraphWindow::updateHoverFromPoint(const QPoint& pt) {
  hoverInFft_ = false;
  hoverFftValue_ = 0.0;
  hoverFftFreqHz_ = 0.0;

  if (showFftOverlay_ && data_.isAudio && data_.sampleRate > 0) {
    ensureFftData();
    const int nChannels = std::min(static_cast<int>(data_.channels.size()), static_cast<int>(fftDb_.size()));
    const auto panes = buildFftPaneLayouts(plotRect(), nChannels);
    for (const auto& pane : panes) {
      if (!pane.inner.contains(pt)) {
        continue;
      }
      const double x01 = std::clamp((pt.x() - pane.inner.left()) / static_cast<double>(std::max(1, pane.inner.width())), 0.0, 1.0);
      const double y01 = std::clamp((pt.y() - pane.inner.top()) / static_cast<double>(std::max(1, pane.inner.height())), 0.0, 1.0);
      hoverInFft_ = true;
      hoverFftValue_ = -80.0 * y01; // top=0 dB, bottom=-80 dB
      hoverFftFreqHz_ = x01 * (data_.sampleRate * 0.5);
      hoverActive_ = true;
      hoverSample_ = -1;
      return;
    }
  }

  const QRect plot = plotRect();
  if (!plot.contains(pt) || graphics_.lines().empty()) {
    hoverActive_ = false;
    hoverSample_ = -1;
    return;
  }

  hoverActive_ = true;
  hoverSample_ = xToSample(pt);

  const auto* axes = graphics_.leftChannelAxes();
  if (!axes) {
    hoverValue_ = 0.0;
    return;
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
    hoverValue_ = 0.0;
  }
}

QString SignalGraphWindow::formatTimeValue(int sample, bool withSuffix) const {
  if (data_.isAudio && data_.sampleRate > 0) {
    const double sec = static_cast<double>(sample) / static_cast<double>(data_.sampleRate);
    const QString body = formatSecondsCompact(sec);
    if (sec >= 60.0) {
      return body;
    }
    return withSuffix ? body + "s" : body;
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
  if (!data_.isAudio) {
    return {};
  }
  if (data_.channels.empty()) {
    return "[dBRMS] -";
  }

  const int totalTimeline = std::max(1, totalTimelineSamples(data_));
  const int start = std::clamp(range.start, 0, totalTimeline - 1);
  const int end = std::clamp(range.end, start + 1, totalTimeline);
  const int offset = timelineOffsetSamples(data_);

  QString out = "[dBRMS]";
  for (const auto& ch : data_.channels) {
    const int d0 = std::max(0, start - offset);
    const int d1 = std::min(static_cast<int>(ch.samples.size()), end - offset);
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
  return out;
}

void SignalGraphWindow::toggleFftOverlay() {
  if (!data_.isAudio || data_.channels.empty() || data_.sampleRate <= 0) {
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
  fftDb_ = fftProvider_(viewStart_, viewLen_);
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
  if (!showFftOverlay_ || !workspaceActive_ || !data_.isAudio || data_.sampleRate <= 0) {
    return;
  }
  ensureFftData();
  if (fftDb_.empty()) {
    return;
  }

  const int nChannels = std::min(static_cast<int>(data_.channels.size()), static_cast<int>(fftDb_.size()));
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
               QString("%1").arg(data_.sampleRate / 2));
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
  const int totalTimeline = std::max(1, totalTimelineSamples(data_));
  const Range rmsRange = hasSel ? sel : Range{0, totalTimeline};

  const QString mouseText =
      (hoverActive_ && hoverInFft_)
          ? QString("(%1, %2 Hz)").arg(hoverFftValue_, 0, 'f', 2).arg(hoverFftFreqHz_, 0, 'f', 1)
          : ((hoverActive_ && hoverSample_ >= 0) ? QString("(%1,%2)").arg(formatTimeValue(hoverSample_, false)).arg(hoverValue_, 0, 'f', 3)
                                                 : QString());
  const QString viewStartText = formatTimeValue(viewStart_, true);
  const QString viewEndText = formatTimeValue(viewStart_ + std::max(1, viewLen_) - 1, true);
  const QString selStartText = hasSel ? formatTimeValue(sel.start, true) : QString();
  const QString selEndText = hasSel ? formatTimeValue(sel.end, true) : QString();
  const QString rmsText = formatRmsInfo(rmsRange);

  const QStringList cells = {mouseText, viewStartText, viewEndText, selStartText, selEndText, rmsText};
  const int widths[] = {160, 90, 90, 90, 90, std::max(240, width() - 520)};

  int x = 0;
  for (int i = 0; i < cells.size(); ++i) {
    const QRect c(x, bar.top() + 1, widths[i], bar.height() - 1);
    p.setPen(QColor(140, 140, 140));
    p.drawRect(c.adjusted(0, 0, -1, -1));
    p.setPen(QColor(18, 18, 18));
    p.drawText(c.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, cells[i]);
    x += widths[i];
    if (x >= width()) {
      break;
    }
  }
}
