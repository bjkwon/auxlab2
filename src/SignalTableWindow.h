#pragma once

#include "AuxEngineFacade.h"

#include <QTableView>
#include <QWidget>

class SignalTableWindow : public QWidget {
  Q_OBJECT
public:
  SignalTableWindow(const QString& varName, const SignalData& data, int displayLimitX = 10, int displayLimitY = 10,
                    QWidget* parent = nullptr);

  QString varName() const;
  void updateData(const SignalData& data);

protected:
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void autoSizeForData();
  bool hasMatrixShape() const;
  void fillTable();
  size_t maxSignalLength() const;

  QString varName_;
  SignalData data_;
  int displayLimitX_ = 10;
  int displayLimitY_ = 10;
  QTableView* table_ = nullptr;
  QTableView* indexTable_ = nullptr;
  bool didInitialAutoSize_ = false;
  bool capturedInitialGeometry_ = false;
  QRect initialGeometry_;
};
