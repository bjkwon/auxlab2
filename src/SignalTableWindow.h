#pragma once

#include "AuxEngineFacade.h"

#include <QTableWidget>
#include <QWidget>

class SignalTableWindow : public QWidget {
  Q_OBJECT
public:
  SignalTableWindow(const QString& varName, const SignalData& data, QWidget* parent = nullptr);

  QString varName() const;
  void updateData(const SignalData& data);

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void fillTable(const SignalData& data);

  QString varName_;
  QTableWidget* table_ = nullptr;
};
