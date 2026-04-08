#include "GraphicsManager.h"

#include "SignalGraphWindow.h"

#include <algorithm>

void GraphicsManager::registerWindow(SignalGraphWindow* window) {
  if (!window) {
    return;
  }
  unregisterWindow(window);

  WindowRecord record;
  record.window = window;
  record.figureId = window->graphicsModel().figure().common.id;
  if (const auto* axes = window->graphicsModel().currentAxes()) {
    record.currentAxesId = axes->common.id;
  }
  record.namedPlot = window->graphicsModel().isNamedPlot();
  record.title = window->graphicsModel().figure().title;
  record.sourcePath = window->graphicsModel().sourcePath();
  windows_.push_back(record);
}

void GraphicsManager::unregisterWindow(SignalGraphWindow* window) {
  if (!window) {
    return;
  }
  auto it = findRecord(window);
  if (it != windows_.end()) {
    windows_.erase(it);
  }
  if (currentWindow_ == window) {
    currentWindow_.clear();
  }
}

void GraphicsManager::markFocused(SignalGraphWindow* window) {
  if (!window) {
    return;
  }
  auto it = findRecord(window);
  if (it == windows_.end()) {
    registerWindow(window);
    it = findRecord(window);
  }
  if (it != windows_.end()) {
    it->figureId = window->graphicsModel().figure().common.id;
    it->title = window->graphicsModel().figure().title;
    it->namedPlot = window->graphicsModel().isNamedPlot();
    it->sourcePath = window->graphicsModel().sourcePath();
    if (const auto* axes = window->graphicsModel().currentAxes()) {
      it->currentAxesId = axes->common.id;
    }
  }
  currentWindow_ = window;
}

void GraphicsManager::clearCurrentWindow(SignalGraphWindow* window) {
  if (!window || currentWindow_ == window) {
    currentWindow_.clear();
  }
}

void GraphicsManager::reconcile() {
  windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [](const WindowRecord& record) {
                   return !record.window;
                 }),
                 windows_.end());
  if (currentWindow_ && findRecord(currentWindow_) == windows_.end()) {
    currentWindow_.clear();
  }
}

SignalGraphWindow* GraphicsManager::currentFigureWindow() const {
  return currentWindow_.data();
}

std::uint64_t GraphicsManager::currentFigureId() const {
  return currentWindow_ ? currentWindow_->graphicsModel().figure().common.id : 0;
}

std::uint64_t GraphicsManager::currentAxesId() const {
  if (!currentWindow_) {
    return 0;
  }
  const auto* axes = currentWindow_->graphicsModel().currentAxes();
  return axes ? axes->common.id : 0;
}

SignalGraphWindow* GraphicsManager::findNamedFigure(const QString& sourcePath) const {
  for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
    if (it->window && it->namedPlot && it->sourcePath == sourcePath) {
      return it->window.data();
    }
  }
  return nullptr;
}

SignalGraphWindow* GraphicsManager::findFigureByTitle(const QString& title) const {
  for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
    if (it->window && it->title == title) {
      return it->window.data();
    }
  }
  return nullptr;
}

SignalGraphWindow* GraphicsManager::findFigureById(std::uint64_t figureId) const {
  for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
    if (it->window && it->figureId == figureId) {
      return it->window.data();
    }
  }
  return nullptr;
}

SignalGraphWindow* GraphicsManager::findAxesOwner(std::uint64_t axesId) const {
  for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
    if (!it->window) {
      continue;
    }
    const auto& axes = it->window->graphicsModel().axes();
    auto axIt = std::find_if(axes.begin(), axes.end(), [axesId](const GraphicsAxesHandle& ax) {
      return ax.common.id == axesId;
    });
    if (axIt != axes.end()) {
      return it->window.data();
    }
  }
  return nullptr;
}

QString GraphicsManager::nextUnnamedFigureTitle() {
  return QString("Figure %1").arg(nextUnnamedFigureNumber_++);
}

std::vector<GraphicsManager::WindowRecord>::iterator GraphicsManager::findRecord(SignalGraphWindow* window) {
  return std::find_if(windows_.begin(), windows_.end(), [window](const WindowRecord& record) {
    return record.window.data() == window;
  });
}

std::vector<GraphicsManager::WindowRecord>::const_iterator GraphicsManager::findRecord(SignalGraphWindow* window) const {
  return std::find_if(windows_.begin(), windows_.end(), [window](const WindowRecord& record) {
    return record.window.data() == window;
  });
}
