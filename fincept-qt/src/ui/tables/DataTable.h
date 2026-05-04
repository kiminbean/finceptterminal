#pragma once
#include <QStringList>
#include <QTableWidget>
#include <QVector>

namespace fincept::ui {

/// Reusable data table with alternating rows.
class DataTable : public QTableWidget {
    Q_OBJECT
  public:
    explicit DataTable(QWidget* parent = nullptr);

    void set_headers(const QStringList& headers);
    void set_data(const QVector<QStringList>& rows);
    void add_row(const QStringList& row);
    void clear_data();
    void set_column_widths(const QVector<int>& widths);

    /// Bulk-add rows without per-row signals — significantly faster for large datasets.
    void set_data_bulk(const QVector<QStringList>& rows);

    // Color a specific cell
    void set_cell_color(int row, int col, const QString& color);

protected:
    // Item recycling pool — reuse QTableWidgetItem objects instead of
    // new/delete per row refresh. Dramatically reduces allocation overhead
    // for real-time data feeds that refresh frequently.
    QVector<QTableWidgetItem*> item_pool_;
    QTableWidgetItem* take_from_pool(const QString& text, const QColor& fg);
    void return_to_pool(int old_row_count);
};

} // namespace fincept::ui
