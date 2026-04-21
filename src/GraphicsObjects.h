#pragma once

#include "AuxEngineFacade.h"

#include <QColor>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

enum class GraphicsObjectType {
  Figure,
  Axes,
  Line,
  Text,
};

struct GraphicsObjectCommon {
  std::uint64_t id = 0;
  GraphicsObjectType type = GraphicsObjectType::Figure;
  std::uint64_t parentId = 0;
  std::vector<std::uint64_t> children;
  std::array<double, 4> pos{0.0, 0.0, 0.0, 0.0};
  QColor color = Qt::black;
  QString tag;
  bool visible = true;
};

struct GraphicsFigureHandle {
  GraphicsObjectCommon common;
  QString title;
  bool namedPlot = false;
  QString sourcePath;
};

struct GraphicsAxesHandle {
  GraphicsObjectCommon common;
  bool box = true;
  int lineWidth = 1;
  std::array<double, 2> xlim{0.0, 1.0};
  std::array<double, 2> ylim{-1.0, 1.0};
  bool autoXLim = true;
  bool autoYLim = true;
  QString fontName = "Helvetica";
  int fontSize = 11;
  QString xscale = "linear";
  QString yscale = "linear";
  QVector<double> xtick;
  QVector<double> ytick;
  QVector<QString> xtickLabel;
  QVector<QString> ytickLabel;
  bool xgrid = true;
  bool ygrid = true;
  int logicalChannel = 0;
};

struct GraphicsLineHandle {
  GraphicsObjectCommon common;
  QVector<double> xdata;
  QVector<double> ydata;
  int lineWidth = 1;
  QString lineStyle = "-";
  QString marker;
  int markerSize = 4;
  int logicalChannel = 0;
};

struct GraphicsTextHandle {
  GraphicsObjectCommon common;
  QString fontName = "Helvetica";
  int fontSize = 12;
  QString stringValue;
};

class GraphicsFigureModel {
public:
  static GraphicsFigureModel createEmptyFigure(const QString& title,
                                               bool namedPlot,
                                               const QString& sourcePath);
  static GraphicsFigureModel createSignalFigure(const QString& title,
                                                const SignalData& data,
                                                bool namedPlot,
                                                const QString& sourcePath);

  void updateSignalData(const SignalData& data);
  void setStereoOverlay(bool overlay);
  void applyStyleToAllLines(const std::optional<QColor>& color,
                            const QString& marker,
                            const QString& lineStyle);
  void applyXDataToAllLines(const QVector<double>& xdata);
  std::uint64_t addAxes(const std::array<double, 4>& pos);
  std::uint64_t addLine(std::uint64_t axesId, const QVector<double>& xdata, const QVector<double>& ydata);
  std::uint64_t addText(std::uint64_t parentId, double x, double y, const QString& text);
  bool setCurrentAxes(std::uint64_t axesId);
  bool removeAxes(std::uint64_t axesId);
  bool removeLine(std::uint64_t lineId);
  bool removeText(std::uint64_t textId);
  bool containsAxes(std::uint64_t axesId) const;
  bool containsLine(std::uint64_t lineId) const;
  bool containsText(std::uint64_t textId) const;

  const GraphicsFigureHandle& figure() const { return figure_; }
  GraphicsFigureHandle& figureMutable() { return figure_; }
  const std::vector<GraphicsAxesHandle>& axes() const { return axes_; }
  const std::vector<GraphicsLineHandle>& lines() const { return lines_; }
  const std::vector<GraphicsTextHandle>& texts() const { return texts_; }

  bool stereoOverlay() const { return stereoOverlay_; }
  bool isNamedPlot() const { return figure_.namedPlot; }
  const QString& sourcePath() const { return figure_.sourcePath; }

  const GraphicsAxesHandle* currentAxes() const;
  const GraphicsAxesHandle* leftChannelAxes() const;
  GraphicsAxesHandle* axesByIdMutable(std::uint64_t axesId);
  GraphicsLineHandle* lineByIdMutable(std::uint64_t lineId);
  GraphicsTextHandle* textByIdMutable(std::uint64_t textId);
  const GraphicsLineHandle* lineById(std::uint64_t lineId) const;
  std::vector<const GraphicsLineHandle*> linesForAxes(std::uint64_t axesId) const;

private:
  GraphicsFigureModel() = default;

  void rebuildSignalChildren(const SignalData& data);
  GraphicsAxesHandle& addAxesHandle(int logicalChannel, const std::array<double, 4>& pos);
  GraphicsLineHandle& addDefaultLine(std::uint64_t axesId, int logicalChannel, const QColor& color);
  void syncLineData(const SignalData& data);
  void applyStereoLayout();
  std::uint64_t nextId();

  GraphicsFigureHandle figure_;
  std::vector<GraphicsAxesHandle> axes_;
  std::vector<GraphicsLineHandle> lines_;
  std::vector<GraphicsTextHandle> texts_;
  std::uint64_t currentAxesId_ = 0;
  bool stereoOverlay_ = false;
  int channelCount_ = 0;
};
