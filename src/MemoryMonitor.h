#pragma once
#include <QObject>
#include <QTimer>

struct MemoryInfo {
    long totalKB;
    long usedKB;
    long freeKB;
    long availableKB;
    long cachedKB;
    long buffersKB;
    long swapTotalKB;
    long swapUsedKB;

    float usedPercent() const {
        return totalKB > 0 ? (float)usedKB / totalKB * 100.0f : 0;
    }
    float swapUsedPercent() const {
        return swapTotalKB > 0 ? (float)swapUsedKB / swapTotalKB * 100.0f : 0;
    }
};

class MemoryMonitor : public QObject {
    Q_OBJECT

public:
    explicit MemoryMonitor(QObject* parent = nullptr);
    void start();
    void stop();

    MemoryInfo current() const { return m_info; }

signals:
    void updated(MemoryInfo info);

private slots:
    void refresh();

private:
    QTimer     m_timer;
    MemoryInfo m_info;
};
