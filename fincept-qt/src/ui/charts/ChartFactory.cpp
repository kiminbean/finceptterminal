#include "ui/charts/ChartFactory.h"

#include "ui/theme/ThemeManager.h"

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <cmath>

namespace fincept::ui {

QVector<ChartFactory::DataPoint> ChartFactory::lttb_downsample(const QVector<DataPoint>& data, int threshold) {
    const int n = data.size();
    if (n <= threshold || threshold <= 2) return data;

    QVector<DataPoint> sampled;
    sampled.reserve(threshold);

    // Always include first and last points
    sampled.append(data[0]);

    const double bucket_size = static_cast<double>(n - 2) / (threshold - 2);
    int prev_index = 0;

    for (int i = 1; i < threshold - 1; ++i) {
        // Calculate bucket boundaries
        const double avg_start = std::floor((i - 1) * bucket_size) + 1;
        const double avg_end = std::floor(i * bucket_size) + 1;

        // Calculate average of next bucket (used as reference point)
        double avg_x = 0, avg_y = 0;
        const double next_start = std::floor(i * bucket_size) + 1;
        const double next_end = std::floor((i + 1) * bucket_size) + 1;
        const int next_count = static_cast<int>(next_end - next_start);
        if (next_count > 0) {
            for (int j = static_cast<int>(next_start); j < static_cast<int>(next_end) && j < n; ++j) {
                avg_x += data[j].x;
                avg_y += data[j].y;
            }
            avg_x /= next_count;
            avg_y /= next_count;
        } else {
            avg_x = data[std::min(static_cast<int>(next_start), n - 1)].x;
            avg_y = data[std::min(static_cast<int>(next_start), n - 1)].y;
        }

        // Find point in current bucket with largest triangle area
        double max_area = -1.0;
        int max_index = static_cast<int>(avg_start);
        const DataPoint& prev = data[prev_index];

        for (int j = static_cast<int>(avg_start); j < static_cast<int>(avg_end) && j < n; ++j) {
            // Triangle area formula (no division by 2, we only compare)
            const double area = std::abs(
                (prev.x - avg_x) * (data[j].y - prev.y) -
                (prev.x - data[j].x) * (avg_y - prev.y)
            );
            if (area > max_area) {
                max_area = area;
                max_index = j;
            }
        }

        sampled.append(data[max_index]);
        prev_index = max_index;
    }

    sampled.append(data[n - 1]);
    return sampled;
}

void ChartFactory::apply_theme(QChart* chart) {
    const auto& t = ThemeManager::instance().tokens();
    chart->setBackgroundBrush(QBrush(QColor(t.bg_surface)));
    chart->setPlotAreaBackgroundBrush(QBrush(QColor(t.bg_surface)));
    chart->setPlotAreaBackgroundVisible(true);
    chart->legend()->setVisible(false);
    chart->setMargins(QMargins(0, 0, 0, 0));

    for (auto* axis : chart->axes()) {
        axis->setLabelsColor(QColor(t.text_secondary));
        axis->setGridLineColor(QColor(t.border_dim));
        axis->setLinePenColor(QColor(t.border_med));
    }
}

QChartView* ChartFactory::line_chart(const QString& title, const QVector<DataPoint>& data, const QString& color) {
    const auto& t = ThemeManager::instance().tokens();
    const QColor line_color = color.isEmpty() ? QColor(t.accent) : QColor(color);
    auto* series = new QLineSeries;
    series->setPen(QPen(line_color, 1.5));
    // Downsample large datasets with LTTB for faster rendering
    const auto plot_data = lttb_downsample(data, 500);
    for (const auto& p : plot_data) {
        series->append(p.x, p.y);
    }

    auto* chart = new QChart;
    chart->addSeries(series);
    chart->setTitle(title);
    chart->setTitleBrush(QBrush(QColor(t.text_secondary)));
    chart->createDefaultAxes();
    apply_theme(chart);

    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    view->setStyleSheet("background: transparent; border: none;");
    return view;
}

QChartView* ChartFactory::bar_chart(const QString& title, const QStringList& categories, const QVector<double>& values,
                                    const QString& color) {
    const auto& t = ThemeManager::instance().tokens();
    const QColor bar_color = color.isEmpty() ? QColor(t.accent) : QColor(color);
    auto* set = new QBarSet("");
    set->setColor(bar_color);
    for (double v : values) {
        *set << v;
    }

    auto* series = new QBarSeries;
    series->append(set);

    auto* chart = new QChart;
    chart->addSeries(series);
    chart->setTitle(title);
    chart->setTitleBrush(QBrush(QColor(t.text_secondary)));

    auto* axisX = new QBarCategoryAxis;
    axisX->append(categories);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto* axisY = new QValueAxis;
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    apply_theme(chart);

    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    view->setStyleSheet("background: transparent; border: none;");
    return view;
}

QChartView* ChartFactory::sparkline(const QVector<double>& data, const QString& color, int width, int height) {
    const auto& t = ThemeManager::instance().tokens();
    const QColor spark_color = color.isEmpty() ? QColor(t.text_secondary) : QColor(color);
    auto* series = new QLineSeries;
    series->setPen(QPen(spark_color, 1.0));
    for (int i = 0; i < data.size(); ++i) {
        series->append(i, data[i]);
    }

    auto* chart = new QChart;
    chart->addSeries(series);
    chart->createDefaultAxes();
    chart->legend()->setVisible(false);
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->setBackgroundBrush(Qt::transparent);
    chart->setPlotAreaBackgroundVisible(false);

    for (auto* axis : chart->axes()) {
        axis->setVisible(false);
    }

    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setFixedSize(width, height);
    view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    view->setStyleSheet("background: transparent; border: none;");
    return view;
}

} // namespace fincept::ui
