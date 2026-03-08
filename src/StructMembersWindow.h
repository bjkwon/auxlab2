#pragma once

#include "AuxEngineFacade.h"

#include <QWidget>

class QTreeWidget;

class StructMembersWindow : public QWidget {
  Q_OBJECT
public:
  StructMembersWindow(const QString& structPath, const std::vector<VarSnapshot>& members, QWidget* parent = nullptr);
  QString structPath() const;

signals:
  void requestOpenGraph(const QString& fullPath);
  void requestPlayAudio(const QString& fullPath);
  void requestOpenDetail(const QString& fullPath);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QString selectedFullPath() const;

  QString structPath_;
  QTreeWidget* tree_ = nullptr;
};
