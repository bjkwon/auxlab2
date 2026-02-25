#include "CommandConsole.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextOption>

#include <algorithm>

CommandConsole::CommandConsole(QWidget* parent) : QPlainTextEdit(parent) {
  setUndoRedoEnabled(false);
  setWordWrapMode(QTextOption::NoWrap);
  appendPrompt();
}

bool CommandConsole::event(QEvent* event) {
  if (event->type() == QEvent::ShortcutOverride) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const auto mods = ke->modifiers();
#ifdef Q_OS_MAC
    const bool ctrlLike = (mods & Qt::ControlModifier) || (mods & Qt::MetaModifier);
#else
    const bool ctrlLike = (mods & Qt::ControlModifier);
#endif
    if (ctrlLike) {
      switch (ke->key()) {
        case Qt::Key_A:
        case Qt::Key_E:
        case Qt::Key_U:
        case Qt::Key_K:
        case Qt::Key_P:
        case Qt::Key_N:
        case Qt::Key_R:
          event->accept();
          return true;
        default:
          break;
      }
    }
  }
  return QPlainTextEdit::event(event);
}

QString CommandConsole::currentCommand() const {
  QTextCursor c(document());
  c.setPosition(inputStartPos_);
  c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
  QString out = c.selectedText();
  out.replace(QChar(0x2029), '\n');
  return out;
}

void CommandConsole::setPrompt(const QString& prompt) {
  if (prompt == prompt_) {
    return;
  }

  const QString oldPrompt = prompt_;
  const int oldInputStart = inputStartPos_;
  const int promptStart = oldInputStart - oldPrompt.size();
  if (promptStart < 0) {
    prompt_ = prompt;
    return;
  }

  QTextCursor c(document());
  c.setPosition(promptStart);
  c.setPosition(oldInputStart, QTextCursor::KeepAnchor);
  c.removeSelectedText();
  c.insertText(prompt);
  prompt_ = prompt;
  inputStartPos_ = promptStart + prompt.size();
}

void CommandConsole::setCurrentCommand(const QString& cmd) {
  QTextCursor c(document());
  c.setPosition(inputStartPos_);
  c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
  c.removeSelectedText();
  QTextCharFormat inputFmt;
  inputFmt.setForeground(palette().text().color());
  c.setCharFormat(inputFmt);
  c.insertText(cmd);
  setTextCursor(c);
  ensureEditableCursor();
}

void CommandConsole::submitCurrentCommand() {
  emit commandSubmitted(currentCommand());
}

void CommandConsole::appendExecutionResult(const QString& output) {
  QTextCursor c(document());
  c.movePosition(QTextCursor::End);
  c.insertText("\n");
  if (!output.isEmpty()) {
    c.insertText(output);
    if (!output.endsWith('\n')) {
      c.insertText("\n");
    }
  }
  setTextCursor(c);
  appendPrompt();
}

void CommandConsole::keyPressEvent(QKeyEvent* event) {
  const int key = event->key();
  const auto mods = event->modifiers();

  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(mods & Qt::ShiftModifier)) {
    emit commandSubmitted(currentCommand());
    event->accept();
    return;
  }

#ifdef Q_OS_MAC
  const bool ctrlLike = (mods & Qt::ControlModifier) || (mods & Qt::MetaModifier);
#else
  const bool ctrlLike = (mods & Qt::ControlModifier);
#endif

  const bool clipboardEditShortcut = ctrlLike && (key == Qt::Key_V || key == Qt::Key_X);
  const QString keyText = event->text();
  const bool plainTextInput =
      !ctrlLike &&
      !(mods & Qt::AltModifier) &&
      !keyText.isEmpty() &&
      keyText.at(0).isPrint();
  const bool modifiesText =
      clipboardEditShortcut ||
      plainTextInput ||
      key == Qt::Key_Backspace ||
      key == Qt::Key_Delete;

  if (modifiesText) {
    ensureEditableCursor();
  }

  QTextCursor c = textCursor();

  if (ctrlLike && key == Qt::Key_R) {
    emit reverseSearchRequested();
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_A) {
    c.setPosition(inputStartPos_);
    setTextCursor(c);
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_E) {
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_U) {
    const int oldPos = c.position();
    c.setPosition(inputStartPos_);
    c.setPosition(std::max(inputStartPos_, oldPos), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_K) {
    c.setPosition(std::max(c.position(), inputStartPos_));
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_P) {
    emit historyNavigateRequested(-1);
    event->accept();
    return;
  }

  if (ctrlLike && key == Qt::Key_N) {
    emit historyNavigateRequested(1);
    event->accept();
    return;
  }

  if (key == Qt::Key_Up) {
    emit historyNavigateRequested(-1);
    event->accept();
    return;
  }

  if (key == Qt::Key_Down) {
    emit historyNavigateRequested(1);
    event->accept();
    return;
  }

  if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    event->accept();
    return;
  }

  if (key == Qt::Key_Home) {
    c.setPosition(inputStartPos_);
    setTextCursor(c);
    event->accept();
    return;
  }

  if (key == Qt::Key_Left && !c.hasSelection() && c.position() <= inputStartPos_) {
    event->accept();
    return;
  }

  if (key == Qt::Key_Backspace && !c.hasSelection() && c.position() <= inputStartPos_) {
    event->accept();
    return;
  }

  if (key == Qt::Key_Delete && !c.hasSelection() && c.position() < inputStartPos_) {
    event->accept();
    return;
  }

  if (modifiesText && c.hasSelection() && c.selectionStart() < inputStartPos_) {
    c.setPosition(inputStartPos_);
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
  }

  QPlainTextEdit::keyPressEvent(event);
  if (modifiesText) {
    ensureEditableCursor();
  }
}

void CommandConsole::mousePressEvent(QMouseEvent* event) {
  QPlainTextEdit::mousePressEvent(event);
}

void CommandConsole::mouseReleaseEvent(QMouseEvent* event) {
  QPlainTextEdit::mouseReleaseEvent(event);
}

void CommandConsole::appendPrompt() {
  QTextCursor c(document());
  c.movePosition(QTextCursor::End);

  QTextCharFormat promptFmt;
  promptFmt.setForeground(promptColor_);
  c.insertText(prompt_, promptFmt);

  QTextCharFormat inputFmt;
  inputFmt.setForeground(palette().text().color());
  c.setCharFormat(inputFmt);

  inputStartPos_ = c.position();
  setTextCursor(c);
  ensureCursorVisible();
}

void CommandConsole::ensureEditableCursor() {
  QTextCursor c = textCursor();
  if (c.position() < inputStartPos_) {
    c.setPosition(document()->characterCount() - 1);
    setTextCursor(c);
  }
}
