#pragma once

#include "AuxEngineFacade.h"

#include <QAudioSink>
#include <QBuffer>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <functional>

class SignalGraphWindow : public QWidget {
  Q_OBJECT
public:
  using FftProvider = std::function<std::vector<std::vector<double>>(int, int)>;
  struct FftPaneLayout {
    int channel = 0;
    QRect box;
    QRect inner;
    QRect leftMargin;
  };

  enum class StereoMode {
    Vertical = 0,
    OverlayBlueRed = 1,
    OverlayRedBlue = 2,
  };

  SignalGraphWindow(const QString& varName, const SignalData& data, QWidget* parent = nullptr, FftProvider fftProvider = {});
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
  void leaveEvent(QEvent* event) override;
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
  void panView(int direction);
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
  QRect plotRect() const;
  void updateHoverFromPoint(const QPoint& pt);
  QString formatTimeValue(int sample, bool withSuffix) const;
  QString formatRmsInfo(const Range& range) const;
  void drawStatusBar(QPainter& p) const;
  void toggleFftOverlay();
  void ensureFftData();
  void drawFftOverlays(QPainter& p, const QRect& plot);
  std::vector<FftPaneLayout> buildFftPaneLayouts(const QRect& plot, int nChannels) const;
  QPoint clampFftPaneOffset(const QRect& plot, const QPoint& desired, int channelIndex) const;

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
  bool hoverActive_ = false;
  int hoverSample_ = -1;
  double hoverValue_ = 0.0;
  bool hoverInFft_ = false;
  double hoverFftValue_ = 0.0;
  double hoverFftFreqHz_ = 0.0;

  StereoMode stereoMode_ = StereoMode::Vertical;
  bool showFftOverlay_ = false;
  bool fftComputed_ = false;
  FftProvider fftProvider_;
  std::vector<std::vector<double>> fftDb_;
  int fftViewStart_ = -1;
  int fftViewLen_ = -1;
  int fftDataSerial_ = -1;
  std::vector<QPoint> fftPaneOffsets_;
  bool fftMovePending_ = false;
  bool fftMoveReady_ = false;
  int fftMoveChannel_ = -1;
  QPoint fftMovePressPos_;
  QPoint fftMoveStartOffset_;
  QTimer fftMoveHoldTimer_;

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
