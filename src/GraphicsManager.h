#pragma once

#include <QPointer>
#include <QString>

#include <cstdint>
#include <vector>

class SignalGraphWindow;

class GraphicsManager {
public:
  struct WindowRecord {
    std::uint64_t figureId = 0;
    std::uint64_t currentAxesId = 0;
    bool namedPlot = false;
    QString title;
    QString sourcePath;
    QPointer<SignalGraphWindow> window;
  };

  void registerWindow(SignalGraphWindow* window);
  void unregisterWindow(SignalGraphWindow* window);
  void markFocused(SignalGraphWindow* window);
  void clearCurrentWindow(SignalGraphWindow* window = nullptr);
  void reconcile();

  SignalGraphWindow* currentFigureWindow() const;
  std::uint64_t currentFigureId() const;
  std::uint64_t currentAxesId() const;

  SignalGraphWindow* findNamedFigure(const QString& sourcePath) const;
  SignalGraphWindow* findFigureByTitle(const QString& title) const;
  SignalGraphWindow* findFigureById(std::uint64_t figureId) const;
  SignalGraphWindow* findAxesOwner(std::uint64_t axesId) const;

  QString nextUnnamedFigureTitle();

private:
  std::vector<WindowRecord>::iterator findRecord(SignalGraphWindow* window);
  std::vector<WindowRecord>::const_iterator findRecord(SignalGraphWindow* window) const;

  std::vector<WindowRecord> windows_;
  QPointer<SignalGraphWindow> currentWindow_;
  int nextUnnamedFigureNumber_ = 1;
};
