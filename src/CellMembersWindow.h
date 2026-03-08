#pragma once

#include "AuxEngineFacade.h"

#include <QWidget>

class QTreeWidget;

class CellMembersWindow : public QWidget {
  Q_OBJECT
public:
  CellMembersWindow(const QString& cellPath, const std::vector<VarSnapshot>& members, QWidget* parent = nullptr);
  QString cellPath() const;

signals:
  void requestOpenGraph(const QString& fullPath);
  void requestPlayAudio(const QString& fullPath);
  void requestOpenDetail(const QString& fullPath);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QString selectedFullPath() const;

  QString cellPath_;
  QTreeWidget* tree_ = nullptr;
};
