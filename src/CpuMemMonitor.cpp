#include "CpuMemMonitor.h"
#include "EventBus.h"
#include "Theme.h"
#include <fstream>
#include <sstream>
#include <QFont>
#include <QScrollArea>
#include <QFrame>

CpuMemMonitor::CpuMemMonitor(QWidget* parent)
    : QWidget(parent), tick(0), prevIdle(0), prevTotal(0)
{
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* content = new QWidget();
    content->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Title row ─────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("📊  CPU & Memory Monitor");
    title->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● LIVE  /proc/stat");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::BLUE).arg(Theme::BLUE_LIGHT));
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    // ── CPU card ──────────────────────────────────────────────────────────────
    auto* cpuCard = new QWidget();
    cpuCard->setStyleSheet(Theme::card());
    auto* cpuLayout = new QVBoxLayout(cpuCard);
    cpuLayout->setContentsMargins(16, 14, 16, 14);
    cpuLayout->setSpacing(8);

    auto* cpuHeader = new QHBoxLayout();
    auto* cpuTitleLbl = new QLabel("CPU Usage");
    cpuTitleLbl->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    cpuLabel = new QLabel("— %");
    cpuLabel->setStyleSheet(QString(
        "color:%1; font-size:22px; font-weight:700;"
    ).arg(Theme::BLUE));
    cpuHeader->addWidget(cpuTitleLbl);
    cpuHeader->addStretch();
    cpuHeader->addWidget(cpuLabel);
    cpuLayout->addLayout(cpuHeader);

    cpuBar = new QProgressBar();
    cpuBar->setRange(0, 100);
    cpuBar->setTextVisible(false);
    cpuBar->setFixedHeight(8);
    cpuBar->setStyleSheet(Theme::progressBar(Theme::BLUE));
    cpuLayout->addWidget(cpuBar);

    cpuSeries = new QLineSeries();
    cpuChart  = new QChart();
    cpuChartView = makeChart(cpuChart, cpuSeries, "CPU %", QColor(Theme::BLUE));
    cpuLayout->addWidget(cpuChartView);
    outer->addWidget(cpuCard);

    // ── Memory card ───────────────────────────────────────────────────────────
    auto* memCard = new QWidget();
    memCard->setStyleSheet(Theme::card());
    auto* memLayout = new QVBoxLayout(memCard);
    memLayout->setContentsMargins(16, 14, 16, 14);
    memLayout->setSpacing(8);

    auto* memHeader = new QHBoxLayout();
    auto* memTitleLbl = new QLabel("Memory Usage");
    memTitleLbl->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    memLabel = new QLabel("— MB / — MB");
    memLabel->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:700;"
    ).arg(Theme::GREEN));
    memHeader->addWidget(memTitleLbl);
    memHeader->addStretch();
    memHeader->addWidget(memLabel);
    memLayout->addLayout(memHeader);

    memBar = new QProgressBar();
    memBar->setRange(0, 100);
    memBar->setTextVisible(false);
    memBar->setFixedHeight(8);
    memBar->setStyleSheet(Theme::progressBar(Theme::GREEN));
    memLayout->addWidget(memBar);

    memSeries = new QLineSeries();
    memChart  = new QChart();
    memChartView = makeChart(memChart, memSeries, "RAM %", QColor(Theme::GREEN));
    memLayout->addWidget(memChartView);
    outer->addWidget(memCard);

    outer->addStretch();

    // Refresh every second
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &CpuMemMonitor::refresh);
    refreshTimer->start(1000);
    refresh();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}" + Theme::scrollbar());
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(scroll);
}

QChartView* CpuMemMonitor::makeChart(QChart* chart, QLineSeries* series,
                                      const QString& /*title*/, const QColor& color)
{
    QPen pen(color);
    pen.setWidth(2);
    series->setPen(pen);

    chart->addSeries(series);
    chart->legend()->hide();
    chart->setBackgroundBrush(QColor("white"));
    chart->setMargins(QMargins(4, 4, 4, 4));
    chart->setBackgroundRoundness(0);

    auto* axisX = new QValueAxis();
    axisX->setRange(0, 60);
    axisX->setLabelsVisible(false);
    axisX->setGridLineColor(QColor("#F1F5F9"));
    axisX->setLinePen(QPen(Qt::NoPen));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setRange(0, 100);
    axisY->setLabelFormat("%d%%");
    axisY->setLabelsColor(QColor(Theme::TEXT_MUTED));
    axisY->setGridLineColor(QColor("#F1F5F9"));
    axisY->setLinePen(QPen(Qt::NoPen));
    axisY->setTickCount(3);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setFixedHeight(120);
    view->setStyleSheet("border: none; background: transparent;");
    return view;
}

void CpuMemMonitor::refresh() {
    tick++;

    // ── CPU ───────────────────────────────────────────────────────────────────
    float cpu = readCpuUsage();
    cpuLabel->setText(QString("%1%").arg(cpu, 0, 'f', 1));
    cpuBar->setValue((int)cpu);

    // Dynamic bar color
    const char* cpuColor = cpu > 80 ? Theme::RED : cpu > 50 ? Theme::ORANGE : Theme::BLUE;
    cpuBar->setStyleSheet(Theme::progressBar(cpuColor));
    cpuLabel->setStyleSheet(QString("color:%1; font-size:22px; font-weight:700;").arg(cpuColor));

    cpuSeries->append(tick, cpu);
    if (cpuSeries->count() > 60) cpuSeries->remove(0);
    auto axes = cpuChart->axes(Qt::Horizontal);
    if (!axes.isEmpty())
        qobject_cast<QValueAxis*>(axes.first())->setRange(tick - 60, tick);

    // ── Memory ────────────────────────────────────────────────────────────────
    long totalKB, usedKB, freeKB;
    readMemInfo(totalKB, usedKB, freeKB);
    float memPercent = totalKB > 0 ? (usedKB * 100.0f / totalKB) : 0;

    memLabel->setText(QString("%1 MB / %2 MB  (%3%)")
        .arg(usedKB / 1024)
        .arg(totalKB / 1024)
        .arg(memPercent, 0, 'f', 1));
    memBar->setValue((int)memPercent);

    const char* memColor = memPercent > 80 ? Theme::RED : memPercent > 60 ? Theme::ORANGE : Theme::GREEN;
    memBar->setStyleSheet(Theme::progressBar(memColor));
    memLabel->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;").arg(memColor));

    memSeries->append(tick, memPercent);
    if (memSeries->count() > 60) memSeries->remove(0);
    auto memAxes = memChart->axes(Qt::Horizontal);
    if (!memAxes.isEmpty())
        qobject_cast<QValueAxis*>(memAxes.first())->setRange(tick - 60, tick);

    // Fire high-load events into the Activity Feed
    if (cpu > 80) {
        OSEvent ev; ev.type = OSEvent::CPUHighLoad;
        ev.detail = QString("CPU high: %1%").arg(cpu, 0, 'f', 1);
        ev.valueLong = (long)cpu;
        EventBus::get().fire(ev);
    }
    if (memPercent > 80) {
        OSEvent ev; ev.type = OSEvent::MemoryPressureHigh;
        ev.detail = QString("RAM high: %1% (%2 MB / %3 MB)")
            .arg(memPercent, 0, 'f', 1).arg(usedKB/1024).arg(totalKB/1024);
        ev.valueLong = usedKB;
        EventBus::get().fire(ev);
    }

    // Emit explanation every 10 ticks
    if (tick % 10 == 0) {
        emit explanationNeeded(QString(
            "<b>System Resources — Live</b><br><br>"
            "CPU: <b>%1%</b>. %2<br><br>"
            "RAM: <b>%3 MB</b> of <b>%4 MB</b> in use (%5%). %6<br><br>"
            "The kernel manages both. When CPU hits 100%, processes queue. "
            "When RAM fills up, the kernel uses swap on disk — 10–100× slower."
        ).arg(cpu, 0, 'f', 1)
         .arg(cpu > 80 ? "⚠ High load — processes competing for CPU." :
              cpu > 40 ? "Moderate load — normal for an active system." :
              "Low load — most CPU cores are idle.")
         .arg(usedKB / 1024)
         .arg(totalKB / 1024)
         .arg(memPercent, 0, 'f', 1)
         .arg(memPercent > 80 ? "⚠ High — risk of swap." : "Normal usage."));
    }
}

float CpuMemMonitor::readCpuUsage() {
    std::ifstream stat("/proc/stat");
    std::string line;
    std::getline(stat, line);
    std::istringstream ss(line);
    std::string label;
    long long user, nice, system, idle, iowait, irq, softirq, steal;
    ss >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    long long idleTime  = idle + iowait;
    long long totalTime = user + nice + system + idle + iowait + irq + softirq + steal;
    long long deltaIdle  = idleTime  - prevIdle;
    long long deltaTotal = totalTime - prevTotal;
    prevIdle  = idleTime;
    prevTotal = totalTime;
    if (deltaTotal == 0) return 0.0f;
    return 100.0f * (1.0f - (float)deltaIdle / deltaTotal);
}

void CpuMemMonitor::readMemInfo(long& totalKB, long& usedKB, long& freeKB) {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long available = 0;
    totalKB = usedKB = freeKB = 0;
    while (std::getline(meminfo, line)) {
        std::istringstream ss(line);
        std::string key; long val;
        ss >> key >> val;
        if (key == "MemTotal:")     totalKB   = val;
        if (key == "MemFree:")      freeKB    = val;
        if (key == "MemAvailable:") available = val;
    }
    usedKB = totalKB - available;
}
