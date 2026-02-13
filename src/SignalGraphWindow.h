#pragma once

#include "AuxEngineFacade.h"

#include <QAudioSink>
#include <QBuffer>
#include <QImage>
#include <QTimer>
#include <QWidget>

class SignalGraphWindow : public QWidget {
  Q_OBJECT
public:
  enum class StereoMode {
    Vertical = 0,
    OverlayBlueRed = 1,
    OverlayRedBlue = 2,
  };

  SignalGraphWindow(const QString& varName, const SignalData& data, QWidget* parent = nullptr);
  ~SignalGraphWindow() override;

  QString varName() const;
  void setWorkspaceActive(bool active);
  void updateData(const SignalData& data);

protected:
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  struct Range {
    int start = 0;
    int end = 0;
  };

  void drawChannel(QPainter& p, const QRect& area, int channel, const QColor& color, bool overlay);
  void cycleStereoMode();
  void zoomIn();
  void zoomOut();
  void togglePlayPause();
  void stopPlayback();
  void startPlaybackForRange(const Range& range);
  Range activePlaybackRange() const;
  Range normalizedSelection() const;
  int xToSample(const QPoint& pt) const;
  void updatePlayhead();
  void updateYRange();
  void invalidateStaticLayer();
  void ensureStaticLayer(const QRect& plot);
  int sampleToX(const QRect& plot, int sample) const;

  QString varName_;
  SignalData data_;
  bool workspaceActive_ = true;

  int viewStart_ = 0;
  int viewLen_ = 0;
  double yMin_ = -1.0;
  double yMax_ = 1.0;

  bool selecting_ = false;
  int selStart_ = -1;
  int selEnd_ = -1;

  StereoMode stereoMode_ = StereoMode::Vertical;

  QAudioSink* audioSink_ = nullptr;
  QBuffer* audioBuffer_ = nullptr;
  QByteArray pcmData_;
  QTimer playheadTimer_;
  Range playingRange_{};

  QImage staticLayer_;
  bool staticLayerValid_ = false;
  QRect staticPlotRect_;
  int dataSerial_ = 0;
  int cachedDataSerial_ = -1;
  int cachedViewStart_ = -1;
  int cachedViewLen_ = -1;
  double cachedYMin_ = 0.0;
  double cachedYMax_ = 0.0;
  StereoMode cachedStereoMode_ = StereoMode::Vertical;
  bool cachedWorkspaceActive_ = true;
};
