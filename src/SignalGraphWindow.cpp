#include "SignalGraphWindow.h"

#include <QAudioFormat>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kRmsDbOffset = 3.0103;

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
}  // namespace

SignalGraphWindow::SignalGraphWindow(const QString& varName, const SignalData& data, QWidget* parent)
    : QWidget(parent), varName_(varName), data_(data) {
  setWindowTitle(QString("Signal Graph - %1").arg(varName_));
  resize(900, 460);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  if (!data_.channels.empty()) {
    viewStart_ = 0;
    viewLen_ = std::max(1, totalTimelineSamples(data_));
  }
  updateYRange();

  playheadTimer_.setInterval(16);
  connect(&playheadTimer_, &QTimer::timeout, this, &SignalGraphWindow::updatePlayhead);
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
  ++dataSerial_;
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
  }
  updateYRange();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor(212, 212, 196));

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
    const int x = sampleToX(plot, sample);
    p.setPen(QPen(QColor(255, 230, 120), 1));
    p.drawLine(x, plot.top(), x, plot.bottom());
  }

  drawStatusBar(p);
}

void SignalGraphWindow::keyPressEvent(QKeyEvent* event) {
  if (!workspaceActive_) {
    return;
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
    case Qt::Key_Space:
      if (data_.isAudio) {
        togglePlayPause();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      const auto sel = normalizedSelection();
      if (sel.end > sel.start) {
        viewStart_ = std::max(0, sel.start);
        viewLen_ = std::max(2, sel.end - sel.start + 1);
        updateYRange();
        invalidateStaticLayer();
        update();
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
  updateHoverFromPoint(event->pos());
  selecting_ = true;
  selStart_ = xToSample(event->pos());
  selEnd_ = selStart_;
  update();
}

void SignalGraphWindow::mouseMoveEvent(QMouseEvent* event) {
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
    updateHoverFromPoint(event->pos());
    selecting_ = false;
    selEnd_ = xToSample(event->pos());
    update();
  }
}

void SignalGraphWindow::leaveEvent(QEvent* event) {
  hoverActive_ = false;
  hoverSample_ = -1;
  update();
  QWidget::leaveEvent(event);
}

void SignalGraphWindow::drawChannel(QPainter& p, const QRect& area, int channel, const QColor& color, bool) {
  if (channel >= static_cast<int>(data_.channels.size())) {
    return;
  }
  const auto& src = data_.channels[static_cast<size_t>(channel)].samples;
  if (src.empty() || viewLen_ <= 1) {
    return;
  }

  const int offset = timelineOffsetSamples(data_);
  const int totalLen = std::max(1, offset + static_cast<int>(src.size()));
  const int from = std::clamp(viewStart_, 0, totalLen - 1);
  const int to = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);

  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(QPen(color, 1));

  const int width = std::max(1, area.width());
  const double samplesPerPixel = static_cast<double>(to - from) / width;
  if (samplesPerPixel <= 1.0) {
    QPainterPath path;
    bool first = true;
    for (int i = from; i < to; ++i) {
      const int di = i - offset;
      if (di < 0 || di >= static_cast<int>(src.size())) {
        continue;
      }
      const double xNorm = static_cast<double>(i - from) / std::max(1, to - from - 1);
      const double y = src[static_cast<size_t>(di)];
      const double yNorm = (y - yMin_) / std::max(1e-12, (yMax_ - yMin_));
      const double px = area.left() + xNorm * area.width();
      const double py = area.bottom() - yNorm * area.height();
      if (first) {
        path.moveTo(px, py);
        first = false;
      } else {
        path.lineTo(px, py);
      }
    }
    p.drawPath(path);
    return;
  }

  for (int x = 0; x < width; ++x) {
    const int s0 = from + static_cast<int>(x * samplesPerPixel);
    const int s1 = std::min(to, from + static_cast<int>((x + 1) * samplesPerPixel));
    if (s0 >= s1) {
      continue;
    }
    const int d0 = std::max(0, s0 - offset);
    const int d1 = std::min(static_cast<int>(src.size()), s1 - offset);
    if (d0 >= d1) {
      continue;
    }
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();
    for (int i = d0; i < d1; ++i) {
      const double v = src[static_cast<size_t>(i)];
      vmin = std::min(vmin, v);
      vmax = std::max(vmax, v);
    }
    const double y0Norm = (vmin - yMin_) / std::max(1e-12, (yMax_ - yMin_));
    const double y1Norm = (vmax - yMin_) / std::max(1e-12, (yMax_ - yMin_));
    const int px = area.left() + x;
    const int py0 = area.bottom() - static_cast<int>(y0Norm * area.height());
    const int py1 = area.bottom() - static_cast<int>(y1Norm * area.height());
    p.drawLine(px, py0, px, py1);
  }
}

void SignalGraphWindow::cycleStereoMode() {
  if (data_.channels.size() < 2) {
    return;
  }
  if (stereoMode_ == StereoMode::Vertical) {
    stereoMode_ = StereoMode::OverlayBlueRed;
  } else if (stereoMode_ == StereoMode::OverlayBlueRed) {
    stereoMode_ = StereoMode::OverlayRedBlue;
  } else {
    stereoMode_ = StereoMode::Vertical;
  }
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
  viewLen_ = std::clamp(newLen, 1, totalLen);
  viewStart_ = center - viewLen_ / 2;
  viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - viewLen_));
  updateYRange();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::zoomOut() {
  if (data_.channels.empty()) {
    return;
  }
  const int totalLen = totalTimelineSamples(data_);
  viewLen_ = std::min(totalLen, static_cast<int>(viewLen_ * 1.8));
  viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - viewLen_));
  updateYRange();
  invalidateStaticLayer();
  update();
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
  viewStart_ += (direction > 0 ? step : -step);
  viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - currentLen));
  viewLen_ = currentLen;

  updateYRange();
  invalidateStaticLayer();
  update();
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

  const int startTimeline = std::clamp(range.start, 0, totalTimeline - 1);
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

void SignalGraphWindow::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  invalidateStaticLayer();
}

void SignalGraphWindow::updateYRange() {
  if (data_.channels.empty() || data_.channels.front().samples.empty()) {
    yMin_ = -1.0;
    yMax_ = 1.0;
    return;
  }

  if (data_.isAudio) {
    yMin_ = -1.0;
    yMax_ = 1.0;
    return;
  }

  const int offset = timelineOffsetSamples(data_);
  const int totalLen = std::max(1, totalTimelineSamples(data_));
  const int from = std::clamp(viewStart_, 0, totalLen - 1);
  const int end = std::clamp(viewStart_ + viewLen_, from + 1, totalLen);
  yMin_ = std::numeric_limits<double>::max();
  yMax_ = std::numeric_limits<double>::lowest();
  bool any = false;
  for (const auto& ch : data_.channels) {
    for (int i = from; i < end; ++i) {
      const int di = i - offset;
      if (di < 0 || di >= static_cast<int>(ch.samples.size())) {
        continue;
      }
      const double v = ch.samples[static_cast<size_t>(di)];
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
                            cachedStereoMode_ != stereoMode_ ||
                            cachedWorkspaceActive_ != workspaceActive_ ||
                            staticPlotRect_ != plot;
  if (!needsRebuild) {
    return;
  }

  staticLayer_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
  staticLayer_.fill(QColor(212, 212, 196));
  QPainter p(&staticLayer_);

  p.fillRect(plot, QColor(188, 196, 190));
  p.setPen(QColor(40, 40, 40));
  p.drawRect(plot);

  if (!workspaceActive_) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawRect(plot);
    p.setPen(Qt::white);
    p.drawText(plot, Qt::AlignCenter, "Inactive (different workspace scope)");
  } else if (data_.channels.empty() || data_.channels.front().samples.empty()) {
    p.setPen(Qt::white);
    p.drawText(plot, Qt::AlignCenter, "No signal data");
  } else {
    const int xTickCount = 7;
    const int yTickCount = 5;
    const int xFrom = viewStart_;
    const int xTo = viewStart_ + std::max(1, viewLen_) - 1;
    const bool xIsTime = data_.isAudio && data_.sampleRate > 0;
    const double xStartVal = xIsTime ? (static_cast<double>(xFrom) / data_.sampleRate) : static_cast<double>(xFrom);
    const double xEndVal = xIsTime ? (static_cast<double>(xTo) / data_.sampleRate) : static_cast<double>(xTo);
    const double xSpan = std::max(1e-12, xEndVal - xStartVal);
    const int xDigits = xSpan < 0.1 ? 4 : (xSpan < 1.0 ? 3 : 2);
    const double ySpan = std::max(1e-12, yMax_ - yMin_);
    const int yDigits = ySpan < 0.1 ? 4 : (ySpan < 1.0 ? 3 : 2);

    p.setPen(QColor(112, 120, 112));
    for (int i = 0; i < xTickCount; ++i) {
      const int x = plot.left() + (i * plot.width()) / (xTickCount - 1);
      p.drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int i = 0; i < yTickCount; ++i) {
      const int y = plot.bottom() - (i * plot.height()) / (yTickCount - 1);
      p.drawLine(plot.left(), y, plot.right(), y);
    }

    const bool stereo = data_.channels.size() >= 2;
    if (stereo && stereoMode_ == StereoMode::Vertical) {
      QRect top = plot;
      top.setHeight(plot.height() / 2 - 2);
      QRect bottom = plot;
      bottom.setTop(top.bottom() + 4);
      drawChannel(p, top, 0, QColor(28, 62, 178), false);
      drawChannel(p, bottom, 1, QColor(255, 86, 86), false);
    } else {
      bool blueOnTop = stereoMode_ != StereoMode::OverlayRedBlue;
      if (blueOnTop) {
        drawChannel(p, plot, 1, QColor(255, 86, 86, 170), true);
        drawChannel(p, plot, 0, QColor(28, 62, 178), true);
      } else {
        drawChannel(p, plot, 0, QColor(28, 62, 178, 170), true);
        drawChannel(p, plot, 1, QColor(255, 86, 86), true);
      }
    }

    p.setPen(QColor(26, 26, 26));
    const QString xLabel = data_.isAudio ? "Time (s)" : "Index";
    p.drawText(QRect(plot.left(), plot.bottom() + 28, plot.width(), 20), Qt::AlignCenter, xLabel);

    p.setPen(QColor(36, 36, 36));
    for (int i = 0; i < xTickCount; ++i) {
      const int x = plot.left() + (i * plot.width()) / (xTickCount - 1);
      const double v = xStartVal + (xSpan * i) / (xTickCount - 1);
      p.drawLine(x, plot.bottom(), x, plot.bottom() + 4);
      const QString label = xIsTime ? QString::number(v, 'f', xDigits) : QString::number(static_cast<int>(std::llround(v)));
      p.drawText(QRect(x - 28, plot.bottom() + 7, 56, 16), Qt::AlignHCenter | Qt::AlignTop, label);
    }

    for (int i = 0; i < yTickCount; ++i) {
      const int y = plot.bottom() - (i * plot.height()) / (yTickCount - 1);
      const double v = yMin_ + ((yMax_ - yMin_) * i) / (yTickCount - 1);
      p.drawLine(plot.left() - 4, y, plot.left(), y);
      const QString label = QString::number(v, 'f', yDigits);
      p.drawText(QRect(2, y - 8, plot.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }
  }

  staticLayerValid_ = true;
  staticPlotRect_ = plot;
  cachedDataSerial_ = dataSerial_;
  cachedViewStart_ = viewStart_;
  cachedViewLen_ = viewLen_;
  cachedYMin_ = yMin_;
  cachedYMax_ = yMax_;
  cachedStereoMode_ = stereoMode_;
  cachedWorkspaceActive_ = workspaceActive_;
}

QRect SignalGraphWindow::plotRect() const {
  // Reserve bottom area for quick-read status fields.
  return rect().adjusted(50, 20, -20, -78);
}

void SignalGraphWindow::updateHoverFromPoint(const QPoint& pt) {
  const QRect plot = plotRect();
  if (!plot.contains(pt) || data_.channels.empty()) {
    hoverActive_ = false;
    hoverSample_ = -1;
    return;
  }

  hoverActive_ = true;
  hoverSample_ = xToSample(pt);

  int channel = 0;
  const bool stereoVertical = data_.channels.size() >= 2 && stereoMode_ == StereoMode::Vertical;
  if (stereoVertical) {
    QRect top = plot;
    top.setHeight(plot.height() / 2 - 2);
    channel = top.contains(pt) ? 0 : 1;
  }

  const int offset = timelineOffsetSamples(data_);
  const int di = hoverSample_ - offset;
  const auto& src = data_.channels[static_cast<size_t>(channel)].samples;
  if (di >= 0 && di < static_cast<int>(src.size())) {
    hoverValue_ = src[static_cast<size_t>(di)];
  } else {
    hoverValue_ = 0.0;
  }
}

QString SignalGraphWindow::formatTimeValue(int sample, bool withSuffix) const {
  if (data_.isAudio && data_.sampleRate > 0) {
    const double sec = static_cast<double>(sample) / static_cast<double>(data_.sampleRate);
    return withSuffix ? QString("%1s").arg(sec, 0, 'f', 3) : QString::number(sec, 'f', 3);
  }
  return withSuffix ? QString("%1i").arg(sample) : QString::number(sample);
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
      (hoverActive_ && hoverSample_ >= 0) ? QString("(%1,%2)").arg(formatTimeValue(hoverSample_, false)).arg(hoverValue_, 0, 'f', 3)
                                          : QString();
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
