#pragma once
#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <vector>

class CpuMemMonitor : public QWidget {
    Q_OBJECT

public:
    explicit CpuMemMonitor(QWidget* parent = nullptr);

signals:
    void explanationNeeded(QString text);

private slots:
    void refresh();

private:
    // CPU
    QLabel*       cpuLabel;
    QProgressBar* cpuBar;
    QLineSeries*  cpuSeries;
    QChart*       cpuChart;
    QChartView*   cpuChartView;

    // Memory
    QLabel*       memLabel;
    QProgressBar* memBar;
    QLineSeries*  memSeries;
    QChart*       memChart;
    QChartView*   memChartView;

    QTimer* refreshTimer;
    int     tick; // how many refreshes have happened — used as X axis

    // Reads /proc/stat for total CPU usage percent
    float   readCpuUsage();

    // Reads /proc/meminfo for total/used/free RAM
    void    readMemInfo(long& totalKB, long& usedKB, long& freeKB);

    // Previous CPU times for delta calculation
    long long prevIdle, prevTotal;

    // Builds a styled chart
    QChartView* makeChart(QChart* chart, QLineSeries* series,
                          const QString& title, const QColor& color);
};
