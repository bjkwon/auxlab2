#include "GraphicsObjects.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
std::array<double, 4> kDefaultMonoAxesPos{0.08, 0.18, 0.86, 0.72};
std::array<double, 4> kDefaultTopAxesPos{0.08, 0.55, 0.86, 0.35};
std::array<double, 4> kDefaultBottomAxesPos{0.08, 0.10, 0.86, 0.35};
QColor kDefaultFigureColor(212, 212, 196);
QColor kDefaultAxesColor(188, 196, 190);
QColor kDefaultLeftLineColor(28, 62, 178);
QColor kDefaultRightLineColor(255, 86, 86);
}  // namespace

GraphicsFigureModel GraphicsFigureModel::createEmptyFigure(const QString& title,
                                                           bool namedPlot,
                                                           const QString& sourcePath) {
  GraphicsFigureModel model;
  model.figure_.common.id = model.nextId();
  model.figure_.common.type = GraphicsObjectType::Figure;
  model.figure_.common.color = kDefaultFigureColor;
  model.figure_.title = title;
  model.figure_.namedPlot = namedPlot;
  model.figure_.sourcePath = sourcePath;
  return model;
}

GraphicsFigureModel GraphicsFigureModel::createSignalFigure(const QString& title,
                                                            const SignalData& data,
                                                            bool namedPlot,
                                                            const QString& sourcePath) {
  GraphicsFigureModel model = createEmptyFigure(title, namedPlot, sourcePath);
  model.rebuildSignalChildren(data);
  return model;
}

void GraphicsFigureModel::updateSignalData(const SignalData& data) {
  const int newChannelCount = static_cast<int>(data.channels.size());
  if (newChannelCount == 1 && !axes_.empty() && lines_.empty()) {
    channelCount_ = 1;
    stereoOverlay_ = false;
    if (currentAxesId_ == 0 || !axesByIdMutable(currentAxesId_)) {
      currentAxesId_ = axes_.front().common.id;
    }
    if (linesForAxes(currentAxesId_).empty()) {
      addDefaultLine(currentAxesId_, 0, kDefaultLeftLineColor);
    }
    syncLineData(data);
    applyStereoLayout();
    return;
  }

  if (newChannelCount != channelCount_ || (newChannelCount > 0 && (axes_.empty() || lines_.empty()))) {
    rebuildSignalChildren(data);
    return;
  }
  syncLineData(data);
}

void GraphicsFigureModel::setStereoOverlay(bool overlay) {
  if (stereoOverlay_ == overlay) {
    return;
  }
  stereoOverlay_ = overlay;
  applyStereoLayout();
}

void GraphicsFigureModel::applyStyleToAllLines(const std::optional<QColor>& color,
                                               const QString& marker,
                                               const QString& lineStyle) {
  for (auto& line : lines_) {
    if (color.has_value()) {
      line.common.color = *color;
    }
    line.marker = marker;
    line.lineStyle = lineStyle;
  }
}

void GraphicsFigureModel::applyXDataToAllLines(const QVector<double>& xdata) {
  for (auto& line : lines_) {
    if (line.ydata.size() == xdata.size()) {
      line.xdata = xdata;
    }
  }
}

const GraphicsAxesHandle* GraphicsFigureModel::currentAxes() const {
  auto it = std::find_if(axes_.begin(), axes_.end(), [this](const GraphicsAxesHandle& axes) {
    return axes.common.id == currentAxesId_;
  });
  return it == axes_.end() ? nullptr : &*it;
}

const GraphicsAxesHandle* GraphicsFigureModel::leftChannelAxes() const {
  auto it = std::find_if(axes_.begin(), axes_.end(), [](const GraphicsAxesHandle& axes) {
    return axes.logicalChannel == 0;
  });
  return it == axes_.end() ? nullptr : &*it;
}

GraphicsAxesHandle* GraphicsFigureModel::axesByIdMutable(std::uint64_t axesId) {
  auto it = std::find_if(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
  return it == axes_.end() ? nullptr : &*it;
}

GraphicsLineHandle* GraphicsFigureModel::lineByIdMutable(std::uint64_t lineId) {
  auto it = std::find_if(lines_.begin(), lines_.end(), [lineId](const GraphicsLineHandle& line) {
    return line.common.id == lineId;
  });
  return it == lines_.end() ? nullptr : &*it;
}

GraphicsTextHandle* GraphicsFigureModel::textByIdMutable(std::uint64_t textId) {
  auto it = std::find_if(texts_.begin(), texts_.end(), [textId](const GraphicsTextHandle& text) {
    return text.common.id == textId;
  });
  return it == texts_.end() ? nullptr : &*it;
}

const GraphicsLineHandle* GraphicsFigureModel::lineById(std::uint64_t lineId) const {
  auto it = std::find_if(lines_.begin(), lines_.end(), [lineId](const GraphicsLineHandle& line) {
    return line.common.id == lineId;
  });
  return it == lines_.end() ? nullptr : &*it;
}

std::vector<const GraphicsLineHandle*> GraphicsFigureModel::linesForAxes(std::uint64_t axesId) const {
  std::vector<const GraphicsLineHandle*> out;
  for (const auto& line : lines_) {
    if (line.common.parentId == axesId) {
      out.push_back(&line);
    }
  }
  return out;
}

void GraphicsFigureModel::rebuildSignalChildren(const SignalData& data) {
  axes_.clear();
  lines_.clear();
  texts_.clear();
  figure_.common.children.clear();
  channelCount_ = static_cast<int>(data.channels.size());
  stereoOverlay_ = false;

  if (channelCount_ <= 0) {
    currentAxesId_ = 0;
    return;
  }

  if (channelCount_ >= 2) {
    const std::uint64_t leftAxesId = addAxesHandle(0, kDefaultTopAxesPos).common.id;
    const std::uint64_t rightAxesId = addAxesHandle(1, kDefaultBottomAxesPos).common.id;
    currentAxesId_ = leftAxesId;
    addDefaultLine(leftAxesId, 0, kDefaultLeftLineColor);
    addDefaultLine(rightAxesId, 1, kDefaultRightLineColor);
  } else {
    const std::uint64_t axesId = addAxesHandle(0, kDefaultMonoAxesPos).common.id;
    currentAxesId_ = axesId;
    addDefaultLine(axesId, 0, kDefaultLeftLineColor);
  }

  syncLineData(data);
  applyStereoLayout();
}

GraphicsAxesHandle& GraphicsFigureModel::addAxesHandle(int logicalChannel, const std::array<double, 4>& pos) {
  GraphicsAxesHandle axes;
  axes.common.id = nextId();
  axes.common.type = GraphicsObjectType::Axes;
  axes.common.parentId = figure_.common.id;
  axes.common.pos = pos;
  axes.common.color = kDefaultAxesColor;
  axes.logicalChannel = logicalChannel;
  figure_.common.children.push_back(axes.common.id);
  axes_.push_back(axes);
  return axes_.back();
}

GraphicsLineHandle& GraphicsFigureModel::addDefaultLine(std::uint64_t axesId, int logicalChannel, const QColor& color) {
  GraphicsLineHandle line;
  line.common.id = nextId();
  line.common.type = GraphicsObjectType::Line;
  line.common.parentId = axesId;
  line.common.color = color;
  line.logicalChannel = logicalChannel;
  auto axIt = std::find_if(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
  if (axIt != axes_.end()) {
    axIt->common.children.push_back(line.common.id);
  }
  lines_.push_back(line);
  return lines_.back();
}

std::uint64_t GraphicsFigureModel::addLine(std::uint64_t axesId, const QVector<double>& xdata, const QVector<double>& ydata) {
  if (xdata.size() != ydata.size() || xdata.isEmpty()) {
    return 0;
  }
  auto axIt = std::find_if(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
  if (axIt == axes_.end()) {
    return 0;
  }

  GraphicsLineHandle line;
  line.common.id = nextId();
  line.common.type = GraphicsObjectType::Line;
  line.common.parentId = axesId;
  line.common.color = Qt::black;
  line.logicalChannel = -1;
  line.xdata = xdata;
  line.ydata = ydata;
  axIt->common.children.push_back(line.common.id);
  lines_.push_back(line);
  double xmin = xdata[0];
  double xmax = xdata[0];
  double ymin = ydata[0];
  double ymax = ydata[0];
  for (int i = 1; i < xdata.size(); ++i) {
    xmin = std::min(xmin, xdata[i]);
    xmax = std::max(xmax, xdata[i]);
    ymin = std::min(ymin, ydata[i]);
    ymax = std::max(ymax, ydata[i]);
  }
  if (std::fabs(xmax - xmin) < 1e-12) xmax = xmin + 1.0;
  if (std::fabs(ymax - ymin) < 1e-12) ymax = ymin + 1.0;
  if (axIt->autoXLim) {
    axIt->xlim = {xmin, xmax};
  }
  if (axIt->autoYLim) {
    axIt->ylim = {ymin, ymax};
  }
  return line.common.id;
}

std::uint64_t GraphicsFigureModel::addText(std::uint64_t parentId, double x, double y, const QString& text) {
  const bool figureParent = (parentId == figure_.common.id);
  auto axIt = std::find_if(axes_.begin(), axes_.end(), [parentId](const GraphicsAxesHandle& axes) {
    return axes.common.id == parentId;
  });
  if (!figureParent && axIt == axes_.end()) {
    return 0;
  }

  GraphicsTextHandle obj;
  obj.common.id = nextId();
  obj.common.type = GraphicsObjectType::Text;
  obj.common.parentId = parentId;
  obj.common.pos = {x, y, 0.0, 0.0};
  obj.common.color = Qt::black;
  obj.stringValue = text;

  if (figureParent) {
    figure_.common.children.push_back(obj.common.id);
  } else {
    axIt->common.children.push_back(obj.common.id);
  }
  texts_.push_back(obj);
  return obj.common.id;
}

void GraphicsFigureModel::syncLineData(const SignalData& data) {
  for (auto& line : lines_) {
    line.xdata.clear();
    line.ydata.clear();
    if (line.logicalChannel < 0 || line.logicalChannel >= static_cast<int>(data.channels.size())) {
      continue;
    }

    const auto& channel = data.channels[static_cast<size_t>(line.logicalChannel)].samples;
    const auto& segments = data.channels[static_cast<size_t>(line.logicalChannel)].segments;
    line.ydata.reserve(static_cast<qsizetype>(channel.size()));
    if (data.isAudio && !segments.empty()) {
      const double gapValue = std::numeric_limits<double>::quiet_NaN();
      line.ydata.fill(gapValue, static_cast<qsizetype>(channel.size()));
      for (const auto& seg : segments) {
        const size_t start = static_cast<size_t>(std::max(0, seg.startSample));
        if (start >= channel.size() || seg.length <= 0) {
          continue;
        }
        const size_t count = std::min(static_cast<size_t>(seg.length), channel.size() - start);
        for (size_t i = 0; i < count; ++i) {
          line.ydata[static_cast<qsizetype>(start + i)] = channel[start + i];
        }
      }
    } else {
      for (size_t i = 0; i < channel.size(); ++i) {
        line.ydata.push_back(channel[i]);
      }
    }

    if (!data.isAudio || data.sampleRate <= 0) {
      line.xdata.reserve(static_cast<qsizetype>(channel.size()));
      for (size_t i = 0; i < channel.size(); ++i) {
        line.xdata.push_back(static_cast<double>(i + 1));
      }
    }
  }

  for (auto& axes : axes_) {
    bool any = false;
    double xmin = 0.0;
    double xmax = 1.0;
    double ymin = -1.0;
    double ymax = 1.0;
    for (const auto& line : lines_) {
      if (line.common.parentId != axes.common.id || line.ydata.isEmpty()) {
        continue;
      }
      if (data.isAudio && data.sampleRate > 0) {
        const double lineXMin = data.startTimeSec;
        const int sampleCount = line.ydata.size();
        const double lineXMax = data.startTimeSec +
                                static_cast<double>(std::max(0, sampleCount - 1)) /
                                    static_cast<double>(data.sampleRate);
        if (!any) {
          xmin = lineXMin;
          xmax = lineXMax;
          any = true;
        } else {
          xmin = std::min(xmin, lineXMin);
          xmax = std::max(xmax, lineXMax);
        }
        continue;
      }
      for (int i = 0; i < line.xdata.size(); ++i) {
        const double x = line.xdata[i];
        const double y = line.ydata[i];
        if (!std::isfinite(y)) {
          continue;
        }
        if (!any) {
          xmin = xmax = x;
          ymin = ymax = y;
          any = true;
        } else {
          xmin = std::min(xmin, x);
          xmax = std::max(xmax, x);
          ymin = std::min(ymin, y);
          ymax = std::max(ymax, y);
        }
      }
    }
    if (any) {
      if (std::fabs(xmax - xmin) < 1e-12) {
        xmax = xmin + 1.0;
      }
      if (axes.autoXLim) {
        axes.xlim = {xmin, xmax};
      }
      if (data.isAudio) {
        if (axes.autoYLim) {
          axes.ylim = {-1.0, 1.0};
        }
      } else {
        if (std::fabs(ymax - ymin) < 1e-12) {
          ymax = ymin + 1.0;
        }
        if (axes.autoYLim) {
          axes.ylim = {ymin, ymax};
        }
      }
    } else {
      if (axes.autoXLim) {
        axes.xlim = {0.0, 1.0};
      }
      if (axes.autoYLim) {
        axes.ylim = {-1.0, 1.0};
      }
    }
  }
}

void GraphicsFigureModel::applyStereoLayout() {
  if (axes_.size() < 2) {
    if (!axes_.empty()) {
      axes_.front().common.visible = true;
      axes_.front().common.pos = kDefaultMonoAxesPos;
    }
    return;
  }

  auto leftIt = std::find_if(axes_.begin(), axes_.end(), [](const GraphicsAxesHandle& axes) {
    return axes.logicalChannel == 0;
  });
  auto rightIt = std::find_if(axes_.begin(), axes_.end(), [](const GraphicsAxesHandle& axes) {
    return axes.logicalChannel == 1;
  });
  if (leftIt == axes_.end() || rightIt == axes_.end()) {
    return;
  }

  if (stereoOverlay_) {
    leftIt->common.pos = kDefaultMonoAxesPos;
    rightIt->common.pos = kDefaultMonoAxesPos;
    leftIt->common.visible = true;
    rightIt->common.visible = false;
  } else {
    leftIt->common.pos = kDefaultTopAxesPos;
    rightIt->common.pos = kDefaultBottomAxesPos;
    leftIt->common.visible = true;
    rightIt->common.visible = true;
  }
}

std::uint64_t GraphicsFigureModel::nextId() {
  static std::atomic<std::uint64_t> globalNextId{1};
  return globalNextId.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t GraphicsFigureModel::addAxes(const std::array<double, 4>& pos) {
  auto& axes = addAxesHandle(static_cast<int>(axes_.size()), pos);
  if (currentAxesId_ == 0) {
    currentAxesId_ = axes.common.id;
  }
  return axes.common.id;
}

bool GraphicsFigureModel::setCurrentAxes(std::uint64_t axesId) {
  auto it = std::find_if(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
  if (it == axes_.end()) {
    return false;
  }
  currentAxesId_ = axesId;
  return true;
}

bool GraphicsFigureModel::removeAxes(std::uint64_t axesId) {
  auto axesIt = std::find_if(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
  if (axesIt == axes_.end()) {
    return false;
  }

  figure_.common.children.erase(
      std::remove(figure_.common.children.begin(), figure_.common.children.end(), axesId),
      figure_.common.children.end());

  lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [axesId](const GraphicsLineHandle& line) {
                 return line.common.parentId == axesId;
               }),
               lines_.end());

  axes_.erase(axesIt);
  if (currentAxesId_ == axesId) {
    currentAxesId_ = 0;
  }
  channelCount_ = static_cast<int>(axes_.size());
  return true;
}

bool GraphicsFigureModel::removeLine(std::uint64_t lineId) {
  auto lineIt = std::find_if(lines_.begin(), lines_.end(), [lineId](const GraphicsLineHandle& line) {
    return line.common.id == lineId;
  });
  if (lineIt == lines_.end()) {
    return false;
  }

  const auto parentId = lineIt->common.parentId;
  auto axIt = std::find_if(axes_.begin(), axes_.end(), [parentId](const GraphicsAxesHandle& axes) {
    return axes.common.id == parentId;
  });
  if (axIt != axes_.end()) {
    axIt->common.children.erase(
        std::remove(axIt->common.children.begin(), axIt->common.children.end(), lineId),
        axIt->common.children.end());
  }
  lines_.erase(lineIt);
  return true;
}

bool GraphicsFigureModel::removeText(std::uint64_t textId) {
  auto textIt = std::find_if(texts_.begin(), texts_.end(), [textId](const GraphicsTextHandle& text) {
    return text.common.id == textId;
  });
  if (textIt == texts_.end()) {
    return false;
  }

  const auto parentId = textIt->common.parentId;
  if (parentId == figure_.common.id) {
    figure_.common.children.erase(
        std::remove(figure_.common.children.begin(), figure_.common.children.end(), textId),
        figure_.common.children.end());
  } else {
    auto axIt = std::find_if(axes_.begin(), axes_.end(), [parentId](const GraphicsAxesHandle& axes) {
      return axes.common.id == parentId;
    });
    if (axIt != axes_.end()) {
      axIt->common.children.erase(
          std::remove(axIt->common.children.begin(), axIt->common.children.end(), textId),
          axIt->common.children.end());
    }
  }
  texts_.erase(textIt);
  return true;
}

bool GraphicsFigureModel::containsAxes(std::uint64_t axesId) const {
  return std::any_of(axes_.begin(), axes_.end(), [axesId](const GraphicsAxesHandle& axes) {
    return axes.common.id == axesId;
  });
}

bool GraphicsFigureModel::containsLine(std::uint64_t lineId) const {
  return std::any_of(lines_.begin(), lines_.end(), [lineId](const GraphicsLineHandle& line) {
    return line.common.id == lineId;
  });
}

bool GraphicsFigureModel::containsText(std::uint64_t textId) const {
  return std::any_of(texts_.begin(), texts_.end(), [textId](const GraphicsTextHandle& text) {
    return text.common.id == textId;
  });
}
