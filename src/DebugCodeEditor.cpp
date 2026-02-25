#include "DebugCodeEditor.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>

class DebugCodeEditor::LineNumberArea : public QWidget {
public:
  explicit LineNumberArea(DebugCodeEditor* editor) : QWidget(editor), editor_(editor) {}

  QSize sizeHint() const override {
    return QSize(editor_->lineNumberAreaWidth(), 0);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    editor_->lineNumberAreaPaintEvent(event);
  }

private:
  DebugCodeEditor* editor_ = nullptr;
};

DebugCodeEditor::DebugCodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
  lineNumberArea_ = new LineNumberArea(this);

  connect(this, &QPlainTextEdit::blockCountChanged, this, &DebugCodeEditor::updateLineNumberAreaWidth);
  connect(this, &QPlainTextEdit::updateRequest, this, &DebugCodeEditor::updateLineNumberArea);

  updateLineNumberAreaWidth();
}

int DebugCodeEditor::lineNumberAreaWidth() const {
  int digits = 1;
  int max = qMax(1, blockCount());
  while (max >= 10) {
    max /= 10;
    ++digits;
  }

  const int space = kMarkerColumnWidth + 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  return space;
}

void DebugCodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
  QPainter painter(lineNumberArea_);
  painter.fillRect(event->rect(), QColor(38, 40, 48));
  painter.setPen(QColor(58, 60, 72));
  painter.drawLine(kMarkerColumnWidth, event->rect().top(), kMarkerColumnWidth, event->rect().bottom());

  QTextBlock block = firstVisibleBlock();
  int blockNumber = block.blockNumber();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      const int lineNo = blockNumber + 1;
      if (breakpointLines_.contains(lineNo)) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 70, 70));
        const int cy = top + fontMetrics().height() / 2;
        painter.drawEllipse(QPoint(kMarkerColumnWidth / 2, cy), 4, 4);
      }
      const QString number = QString::number(blockNumber + 1);
      painter.setPen(QColor(150, 155, 170));
      painter.drawText(kMarkerColumnWidth + 2, top, lineNumberArea_->width() - kMarkerColumnWidth - 6, fontMetrics().height(),
                       Qt::AlignRight, number);
    }

    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++blockNumber;
  }
}

void DebugCodeEditor::setBreakpointLines(const QSet<int>& lines) {
  breakpointLines_ = lines;
  if (lineNumberArea_) {
    lineNumberArea_->update();
  }
}

void DebugCodeEditor::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);

  const QRect cr = contentsRect();
  lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void DebugCodeEditor::updateLineNumberAreaWidth() {
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void DebugCodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
  if (dy) {
    lineNumberArea_->scroll(0, dy);
  } else {
    lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
  }

  if (rect.contains(viewport()->rect())) {
    updateLineNumberAreaWidth();
  }
}
