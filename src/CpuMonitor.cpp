#include "CpuMonitor.h"
#include <QFile>
#include <QTextStream>

CpuMonitor::CpuMonitor(QObject* parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &CpuMonitor::refresh);
}

void CpuMonitor::start() {
    refresh();
    m_timer.start(1000); // every 1 second for CPU
}

void CpuMonitor::stop() {
    m_timer.stop();
}

void CpuMonitor::refresh() {
    // /proc/stat has one line per CPU core
    // cpu  = total across all cores
    // cpu0 = core 0, cpu1 = core 1, etc.
    QFile file("/proc/stat");
    if (!file.open(QIODevice::ReadOnly)) return;

    QTextStream stream(&file);
    QVector<CpuCoreInfo> current;
    bool firstLine = true;

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (!line.startsWith("cpu")) break;

        CpuCoreInfo info = parseLine(line);

        if (firstLine) {
            // First line is "cpu" = total
            if (!m_prevCores.isEmpty()) {
                auto& prev = m_prevCores[0];
                long prevIdle = prev.idle + prev.iowait;
                long currIdle = info.idle + info.iowait;
                long prevTotal = prev.user + prev.nice + prev.system + prevIdle + prev.irq + prev.softirq;
                long currTotal = info.user + info.nice + info.system + currIdle + info.irq + info.softirq;

                long deltaTotal = currTotal - prevTotal;
                long deltaIdle  = currIdle  - prevIdle;

                m_totalUsage = deltaTotal > 0
                    ? (float)(deltaTotal - deltaIdle) / deltaTotal * 100.0f
                    : 0.0f;
            }
            firstLine = false;
        }
        current.append(info);
    }

    // Calculate per-core usage
    m_cores.clear();
    for (int i = 1; i < current.size(); i++) {
        CpuCoreInfo& c = current[i];
        c.core = i - 1;

        if (m_prevCores.size() > i) {
            auto& prev = m_prevCores[i];
            long prevIdle = prev.idle + prev.iowait;
            long currIdle = c.idle + c.iowait;
            long prevTotal = prev.user + prev.nice + prev.system + prevIdle + prev.irq + prev.softirq;
            long currTotal = c.user + c.nice + c.system + currIdle + c.irq + c.softirq;

            long deltaTotal = currTotal - prevTotal;
            long deltaIdle  = currIdle  - prevIdle;

            c.usagePercent = deltaTotal > 0
                ? (float)(deltaTotal - deltaIdle) / deltaTotal * 100.0f
                : 0.0f;
        }
        m_cores.append(c);
    }

    m_prevCores = current;
    emit updated(m_totalUsage, m_cores);
}

CpuCoreInfo CpuMonitor::parseLine(const QString& line) {
    // Format: "cpu  264930 3935 74045 3766653 5966 0 3410 0 0 0"
    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    CpuCoreInfo info;
    info.core       = 0;
    info.usagePercent = 0;
    if (parts.size() < 8) return info;

    info.user    = parts[1].toLong();
    info.nice    = parts[2].toLong();
    info.system  = parts[3].toLong();
    info.idle    = parts[4].toLong();
    info.iowait  = parts[5].toLong();
    info.irq     = parts[6].toLong();
    info.softirq = parts[7].toLong();
    return info;
}
