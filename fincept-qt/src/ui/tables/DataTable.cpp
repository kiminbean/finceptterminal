#include "ui/tables/DataTable.h"

#include "ui/theme/Theme.h"

#include <QHeaderView>

namespace fincept::ui {

DataTable::DataTable(QWidget* parent) : QTableWidget(parent) {
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    setUniformRowHeights(true); // All rows are 26px — skip per-row height calc
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(26); // Set default once instead of per-row
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setStyleSheet(QString("QTableWidget { background: %1; alternate-background-color: %2; "
                          "gridline-color: %3; border: none; }"
                          "QTableWidget::item { padding: 4px 8px; height: 26px; }"
                          "QTableWidget::item:selected { background: #111111; }")
                      .arg(colors::DARK(), colors::ROW_ALT(), colors::BORDER()));
}

void DataTable::set_headers(const QStringList& headers) {
    setColumnCount(headers.size());
    setHorizontalHeaderLabels(headers);
}

void DataTable::set_data(const QVector<QStringList>& rows) {
    setUpdatesEnabled(false);
    setRowCount(0);
    setRowCount(rows.size()); // Pre-allocate all rows at once
    const QColor fg(colors::WHITE());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (int c = 0; c < row.size() && c < columnCount(); ++c) {
            auto* item = new QTableWidgetItem(row[c]);
            item->setForeground(fg);
            setItem(r, c, item);
        }
    }
    setUpdatesEnabled(true);
}

void DataTable::set_data_bulk(const QVector<QStringList>& rows) {
    setUpdatesEnabled(false);
    setRowCount(0);
    setRowCount(rows.size());
    const QColor fg(colors::WHITE());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (int c = 0; c < row.size() && c < columnCount(); ++c) {
            auto* item = new QTableWidgetItem(row[c]);
            item->setForeground(fg);
            setItem(r, c, item);
        }
    }
    setUpdatesEnabled(true);
}

void DataTable::add_row(const QStringList& row) {
    int r = rowCount();
    insertRow(r);
    const QColor fg(colors::WHITE());
    for (int c = 0; c < row.size() && c < columnCount(); ++c) {
        auto* item = new QTableWidgetItem(row[c]);
        item->setForeground(fg);
        setItem(r, c, item);
    }
}

void DataTable::clear_data() {
    setRowCount(0);
}

void DataTable::set_column_widths(const QVector<int>& widths) {
    for (int i = 0; i < widths.size() && i < columnCount(); ++i) {
        setColumnWidth(i, widths[i]);
    }
}

void DataTable::set_cell_color(int row, int col, const QString& color) {
    auto* it = item(row, col);
    if (it)
        it->setForeground(QColor(color));
}

} // namespace fincept::ui
