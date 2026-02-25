#pragma once

#include <QSet>
#include <QPlainTextEdit>

class DebugCodeEditor : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit DebugCodeEditor(QWidget* parent = nullptr);
  int lineNumberAreaWidth() const;
  void lineNumberAreaPaintEvent(QPaintEvent* event);
  void setBreakpointLines(const QSet<int>& lines);

protected:
  void resizeEvent(QResizeEvent* event) override;

private slots:
  void updateLineNumberAreaWidth();
  void updateLineNumberArea(const QRect& rect, int dy);

private:
  static constexpr int kMarkerColumnWidth = 14;
  class LineNumberArea;
  LineNumberArea* lineNumberArea_ = nullptr;
  QSet<int> breakpointLines_;
};
