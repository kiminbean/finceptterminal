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
    // When sorting is enabled, every setItem() triggers a re-sort — O(n log n)
    // per call, so a bulk load degrades to O(n^2 log n). Pause sorting for the
    // duration of the load and resume after.
    const bool was_sorting = isSortingEnabled();
    if (was_sorting)
        setSortingEnabled(false);
    setUpdatesEnabled(false);
    // Return existing items to the pool for reuse
    return_to_pool(rowCount());
    setRowCount(rows.size()); // Pre-allocate all rows at once
    const QColor fg(colors::WHITE());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (int c = 0; c < row.size() && c < columnCount(); ++c) {
            setItem(r, c, take_from_pool(row[c], fg));
        }
    }
    setUpdatesEnabled(true);
    if (was_sorting)
        setSortingEnabled(true);
}

void DataTable::set_data_bulk(const QVector<QStringList>& rows) {
    const bool was_sorting = isSortingEnabled();
    if (was_sorting)
        setSortingEnabled(false);
    setUpdatesEnabled(false);
    return_to_pool(rowCount());
    setRowCount(rows.size());
    const QColor fg(colors::WHITE());
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        for (int c = 0; c < row.size() && c < columnCount(); ++c) {
            setItem(r, c, take_from_pool(row[c], fg));
        }
    }
    setUpdatesEnabled(true);
    if (was_sorting)
        setSortingEnabled(true);
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
    return_to_pool(rowCount());
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

namespace fincept::ui {

QTableWidgetItem* DataTable::take_from_pool(const QString& text, const QColor& fg) {
    QTableWidgetItem* item;
    if (!item_pool_.isEmpty()) {
        item = item_pool_.takeLast();
        item->setText(text);
        item->setForeground(fg);
        item->setData(Qt::UserRole, QVariant()); // Reset user data
    } else {
        item = new QTableWidgetItem(text);
        item->setForeground(fg);
    }
    return item;
}

void DataTable::return_to_pool(int old_row_count) {
    const int cols = columnCount();
    for (int r = 0; r < old_row_count; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (auto* it = takeItem(r, c)) {
                item_pool_.append(it);
            }
        }
    }
    // Cap pool size to prevent unbounded memory growth (max ~1000 items)
    while (item_pool_.size() > 1000) {
        delete item_pool_.takeLast();
    }
}

} // namespace fincept::ui
