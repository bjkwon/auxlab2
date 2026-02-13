#include "SignalGraphWindow.h"

#include <QAudioFormat>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

SignalGraphWindow::SignalGraphWindow(const QString& varName, const SignalData& data, QWidget* parent)
    : QWidget(parent), varName_(varName), data_(data) {
  setWindowTitle(QString("Signal Graph - %1").arg(varName_));
  resize(900, 460);
  setFocusPolicy(Qt::StrongFocus);

  if (!data_.channels.empty()) {
    viewStart_ = 0;
    viewLen_ = static_cast<int>(data_.channels.front().samples.size());
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
  data_ = data;
  ++dataSerial_;
  if (!data_.channels.empty()) {
    const int totalLen = static_cast<int>(data_.channels.front().samples.size());
    viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - 1));
    viewLen_ = std::clamp(viewLen_, 1, std::max(1, totalLen));
  }
  updateYRange();
  invalidateStaticLayer();
  update();
}

void SignalGraphWindow::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor(20, 22, 28));

  QRect plot = rect().adjusted(50, 25, -20, -40);
  ensureStaticLayer(plot);
  if (!staticLayer_.isNull()) {
    p.drawImage(QPoint(0, 0), staticLayer_);
  }

  if (selStart_ >= 0 && selEnd_ >= 0 && selStart_ != selEnd_) {
    const int s = std::min(selStart_, selEnd_);
    const int e = std::max(selStart_, selEnd_);
    int x1 = sampleToX(plot, s);
    int x2 = sampleToX(plot, e);
    p.fillRect(QRect(std::min(x1, x2), plot.top(), std::abs(x2 - x1), plot.height()), QColor(120, 170, 255, 60));
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
}

void SignalGraphWindow::keyPressEvent(QKeyEvent* event) {
  if (!workspaceActive_) {
    return;
  }

  switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoomIn();
      break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
      zoomOut();
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
  selecting_ = true;
  selStart_ = xToSample(event->pos());
  selEnd_ = selStart_;
  update();
}

void SignalGraphWindow::mouseMoveEvent(QMouseEvent* event) {
  if (!selecting_) {
    return QWidget::mouseMoveEvent(event);
  }
  selEnd_ = xToSample(event->pos());
  update();
}

void SignalGraphWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    selecting_ = false;
    selEnd_ = xToSample(event->pos());
    update();
  }
}

void SignalGraphWindow::drawChannel(QPainter& p, const QRect& area, int channel, const QColor& color, bool) {
  if (channel >= static_cast<int>(data_.channels.size())) {
    return;
  }
  const auto& src = data_.channels[static_cast<size_t>(channel)].samples;
  if (src.empty() || viewLen_ <= 1) {
    return;
  }

  const int totalLen = static_cast<int>(src.size());
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
      const double xNorm = static_cast<double>(i - from) / std::max(1, to - from - 1);
      const double y = src[static_cast<size_t>(i)];
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
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();
    for (int i = s0; i < s1; ++i) {
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
  const int totalLen = static_cast<int>(data_.channels.front().samples.size());
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
  const int totalLen = static_cast<int>(data_.channels.front().samples.size());
  viewLen_ = std::min(totalLen, static_cast<int>(viewLen_ * 1.8));
  viewStart_ = std::clamp(viewStart_, 0, std::max(0, totalLen - viewLen_));
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

  const int start = std::clamp(range.start, 0, static_cast<int>(data_.channels.front().samples.size()) - 1);
  const int end = std::clamp(range.end, start + 1, static_cast<int>(data_.channels.front().samples.size()));
  playingRange_ = {start, end};

  QAudioFormat fmt;
  fmt.setSampleRate(data_.sampleRate);
  fmt.setChannelCount(static_cast<int>(std::min<size_t>(2, data_.channels.size())));
  fmt.setSampleFormat(QAudioFormat::Int16);

  const int chCount = fmt.channelCount();
  const int frames = end - start;
  pcmData_.resize(frames * chCount * static_cast<int>(sizeof(qint16)));

  auto* out = reinterpret_cast<qint16*>(pcmData_.data());
  for (int i = start; i < end; ++i) {
    for (int c = 0; c < chCount; ++c) {
      const auto& src = data_.channels[static_cast<size_t>(c)].samples;
      double v = 0.0;
      if (i < static_cast<int>(src.size())) {
        v = std::clamp(src[static_cast<size_t>(i)], -1.0, 1.0);
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
  QRect plot = rect().adjusted(50, 25, -20, -40);
  const double t = (pt.x() - plot.left()) / static_cast<double>(std::max(1, plot.width()));
  const double clamped = std::clamp(t, 0.0, 1.0);
  return viewStart_ + static_cast<int>(clamped * std::max(1, viewLen_ - 1));
}

void SignalGraphWindow::updatePlayhead() {
  update(rect().adjusted(50, 25, -20, -40));
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

  const int end = std::min(viewStart_ + viewLen_, static_cast<int>(data_.channels.front().samples.size()));
  yMin_ = std::numeric_limits<double>::max();
  yMax_ = std::numeric_limits<double>::lowest();
  for (const auto& ch : data_.channels) {
    for (int i = viewStart_; i < end && i < static_cast<int>(ch.samples.size()); ++i) {
      const double v = ch.samples[static_cast<size_t>(i)];
      yMin_ = std::min(yMin_, v);
      yMax_ = std::max(yMax_, v);
    }
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
  return plot.left() + (sample - viewStart_) * plot.width() / total;
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
  staticLayer_.fill(QColor(20, 22, 28));
  QPainter p(&staticLayer_);

  p.setPen(QColor(70, 75, 90));
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

    p.setPen(QColor(52, 56, 68));
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
      drawChannel(p, top, 0, QColor(64, 150, 255), false);
      drawChannel(p, bottom, 1, QColor(255, 86, 86), false);
    } else {
      bool blueOnTop = stereoMode_ != StereoMode::OverlayRedBlue;
      if (blueOnTop) {
        drawChannel(p, plot, 1, QColor(255, 86, 86, 170), true);
        drawChannel(p, plot, 0, QColor(64, 150, 255), true);
      } else {
        drawChannel(p, plot, 0, QColor(64, 150, 255, 170), true);
        drawChannel(p, plot, 1, QColor(255, 86, 86), true);
      }
    }

    p.setPen(QColor(220, 220, 220));
    const QString xLabel = data_.isAudio ? "Time (s)" : "Index";
    const QString yLabel = data_.isAudio ? "Amplitude [-1, 1]" : QString("Value [%1, %2]").arg(yMin_, 0, 'g', 5).arg(yMax_, 0, 'g', 5);
    p.drawText(QRect(plot.left(), plot.bottom() + 28, plot.width(), 20), Qt::AlignCenter, xLabel);
    p.drawText(QRect(plot.left() + 8, plot.top() + 6, plot.width() - 16, 20), Qt::AlignLeft, yLabel);

    p.setPen(QColor(170, 175, 190));
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
