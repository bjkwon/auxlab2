#pragma once

#include "AuxEngineFacade.h"
#include "GraphicsObjects.h"

#include <QAudioSink>
#include <QBuffer>
#include <QImage>
#include <QMoveEvent>
#include <optional>
#include <QTimer>
#include <QWidget>
#include <functional>

class SignalGraphWindow : public QWidget {
  Q_OBJECT
public:
  struct CreationOptions {
    CreationOptions() : namedPlot(false) {}
    CreationOptions(QString titleIn, bool namedPlotIn, QString sourcePathIn)
        : title(std::move(titleIn)), namedPlot(namedPlotIn), sourcePath(std::move(sourcePathIn)) {}

    QString title;
    bool namedPlot;
    QString sourcePath;
  };

  using FftProvider = std::function<std::vector<std::vector<double>>(int, int)>;
  struct FftPaneLayout {
    int channel = 0;
    QRect box;
    QRect inner;
    QRect leftMargin;
  };

  SignalGraphWindow(const QString& varName,
                    const SignalData& data,
                    CreationOptions options = CreationOptions(),
                    QWidget* parent = nullptr,
                    FftProvider fftProvider = {});
  ~SignalGraphWindow() override;

  QString varName() const;
  void setWorkspaceActive(bool active);
  void updateData(const SignalData& data);
  std::uint64_t addAxes(const std::array<double, 4>& pos);
  std::uint64_t addLine(std::uint64_t axesId, const QVector<double>& xdata, const QVector<double>& ydata);
  std::uint64_t addText(std::uint64_t parentId, double x, double y, const QString& text);
  bool selectAxes(std::uint64_t axesId);
  bool removeAxes(std::uint64_t axesId);
  bool removeLine(std::uint64_t lineId);
  bool removeText(std::uint64_t textId);
  void applyStyleToAllLines(const std::optional<QColor>& color,
                            const QString& marker,
                            const QString& lineStyle);
  void applyXDataToAllLines(const QVector<double>& xdata);
  const GraphicsFigureModel& graphicsModel() const { return graphics_; }
  GraphicsFigureModel& graphicsModelMutable() { return graphics_; }
  std::array<double, 4> currentFigurePos() const;
  void applyFigurePos(const std::array<double, 4>& pos);
  void refreshGraphics();
  void setAxesXLim(std::uint64_t axesId, const std::array<double, 2>& xlim);
  void setAxesYLim(std::uint64_t axesId, const std::array<double, 2>& ylim);

protected:
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  struct Range {
    int start = 0;
    int end = 0;
  };

  QRect axesRectForPlot(const GraphicsAxesHandle& axes, const QRect& plot) const;
  void drawLine(QPainter& p, const QRect& area, const GraphicsAxesHandle& axes, const GraphicsLineHandle& line);
  void cycleStereoMode();
  void applyRange(const Range& range, bool recordHistory = true);
  Range clampRange(const Range& range) const;
  Range fullRange() const;
  void recordRangeHistory(const Range& range);
  bool canStepRangeHistory(int delta) const;
  void stepRangeHistory(int delta);
  void anchorRangeStartToZero();
  void anchorRangeEndToSignalEnd();
  void resetRangeToFull();
  void zoomIn();
  void zoomOut();
  void panView(int direction);
  void togglePlayPause();
  void stopPlayback();
  void startPlaybackForRange(const Range& range);
  void startPlaybackFromSample(const Range& range, int startSample, bool startPaused);
  Range activePlaybackRange() const;
  Range normalizedSelection() const;
  int currentPlaybackSample() const;
  void handlePlaybackAfterRangeChange();
  int xToSample(const QPoint& pt) const;
  void updatePlayhead();
  void updateYRange();
  void syncVisibleXRangeToAxes();
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
  void syncFigurePosFromWidget();

  QString varName_;
  SignalData data_;
  CreationOptions options_;
  GraphicsFigureModel graphics_;
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
  double hoverXCoord_ = 0.0;
  double hoverValue_ = 0.0;
  bool hoverInFft_ = false;
  double hoverFftValue_ = 0.0;
  double hoverFftFreqHz_ = 0.0;

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
  std::vector<Range> rangeHistory_;
  int rangeHistoryIndex_ = -1;

  QImage staticLayer_;
  bool staticLayerValid_ = false;
  QRect staticPlotRect_;
  int dataSerial_ = 0;
  int cachedDataSerial_ = -1;
  int cachedViewStart_ = -1;
  int cachedViewLen_ = -1;
  double cachedYMin_ = 0.0;
  double cachedYMax_ = 0.0;
  bool cachedStereoOverlay_ = false;
  bool cachedWorkspaceActive_ = true;
};
