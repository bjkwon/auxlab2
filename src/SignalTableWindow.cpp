#include "SignalTableWindow.h"

#include <QHeaderView>
#include <QVBoxLayout>

SignalTableWindow::SignalTableWindow(const QString& varName, const SignalData& data, QWidget* parent)
    : QWidget(parent), varName_(varName) {
  setWindowTitle(QString("Signal Table - %1").arg(varName_));
  resize(700, 420);

  auto* layout = new QVBoxLayout(this);
  table_ = new QTableWidget(this);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(table_);

  fillTable(data);
}

QString SignalTableWindow::varName() const {
  return varName_;
}

void SignalTableWindow::updateData(const SignalData& data) {
  fillTable(data);
}

void SignalTableWindow::fillTable(const SignalData& data) {
  const int channels = static_cast<int>(data.channels.size());
  if (channels == 0) {
    table_->clear();
    table_->setRowCount(0);
    table_->setColumnCount(0);
    return;
  }

  size_t maxLen = 0;
  for (const auto& ch : data.channels) {
    maxLen = std::max(maxLen, ch.samples.size());
  }

  const int maxRows = static_cast<int>(std::min<size_t>(maxLen, 5000));
  table_->setColumnCount(channels + 1);
  table_->setRowCount(maxRows);

  QStringList headers;
  headers << "Index";
  for (int c = 0; c < channels; ++c) {
    headers << QString("Ch%1").arg(c + 1);
  }
  table_->setHorizontalHeaderLabels(headers);

  for (int r = 0; r < maxRows; ++r) {
    table_->setItem(r, 0, new QTableWidgetItem(QString::number(r)));
    for (int c = 0; c < channels; ++c) {
      QString text;
      if (static_cast<size_t>(r) < data.channels[static_cast<size_t>(c)].samples.size()) {
        text = QString::number(data.channels[static_cast<size_t>(c)].samples[static_cast<size_t>(r)], 'g', 8);
      }
      table_->setItem(r, c + 1, new QTableWidgetItem(text));
    }
  }
}
