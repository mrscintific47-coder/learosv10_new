#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>

struct CpuCoreInfo {
    int   core;
    float usagePercent;
    long  user, nice, system, idle, iowait, irq, softirq;
};

class CpuMonitor : public QObject {
    Q_OBJECT

public:
    explicit CpuMonitor(QObject* parent = nullptr);
    void start();
    void stop();

    float totalUsage() const { return m_totalUsage; }
    QVector<CpuCoreInfo> coreUsages() const { return m_cores; }

signals:
    void updated(float total, QVector<CpuCoreInfo> cores);

private slots:
    void refresh();

private:
    CpuCoreInfo parseLine(const QString& line);

    QTimer               m_timer;
    float                m_totalUsage = 0.0f;
    QVector<CpuCoreInfo> m_cores;
    QVector<CpuCoreInfo> m_prevCores; // for delta calculation
};
