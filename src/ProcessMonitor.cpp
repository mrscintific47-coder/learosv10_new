#include "ProcessMonitor.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <pwd.h>
#include <sys/stat.h>

ProcessMonitor::ProcessMonitor(QObject* parent) : QObject(parent) {
    // Refresh every 2 seconds
    connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::refresh);
}

void ProcessMonitor::start() {
    refresh(); // immediate first read
    m_timer.start(2000);
}

void ProcessMonitor::stop() {
    m_timer.stop();
}

void ProcessMonitor::refresh() {
    QVector<ProcessInfo> result;

    // /proc contains a numbered folder for every running process
    QDir procDir("/proc");
    QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& entry : entries) {
        bool ok;
        int pid = entry.toInt(&ok);
        if (!ok) continue; // skip non-numeric entries like /proc/sys

        ProcessInfo info = readProcess(pid);
        if (info.pid == -1) continue; // process died while we were reading

        info.cpuPercent = calcCpuUsage(pid);
        info.user = getProcessUser(pid);
        result.append(info);
    }

    m_processes = result;
    emit updated(m_processes);
}

ProcessInfo ProcessMonitor::readProcess(int pid) {
    ProcessInfo info;
    info.pid = -1; // default to invalid

    QString statusPath = QString("/proc/%1/status").arg(pid);
    QFile statusFile(statusPath);
    if (!statusFile.open(QIODevice::ReadOnly)) return info;

    info.pid = pid;
    info.isSandbox = false;

    QTextStream stream(&statusFile);
    while (!stream.atEnd()) {
        QString line = stream.readLine();

        // Each line in /proc/[pid]/status is "Key:\tValue"
        if (line.startsWith("Name:"))
            info.name = line.section('\t', 1).trimmed();

        else if (line.startsWith("State:"))
            info.state = line.section('\t', 1).trimmed();

        else if (line.startsWith("VmRSS:")) {
            // VmRSS = actual RAM used (Resident Set Size)
            QString val = line.section('\t', 1).trimmed();
            info.memoryKB = val.split(' ').first().toLong();
        }
        else if (line.startsWith("Threads:"))
            info.threads = line.section('\t', 1).trimmed().toInt();
    }

    return info;
}

float ProcessMonitor::calcCpuUsage(int pid) {
    // CPU usage = how much time this process used since last check
    // /proc/[pid]/stat contains timing info
    QString statPath = QString("/proc/%1/stat").arg(pid);
    QFile statFile(statPath);
    if (!statFile.open(QIODevice::ReadOnly)) return 0.0f;

    QString content = statFile.readAll();
    QStringList parts = content.split(' ');
    if (parts.size() < 15) return 0.0f;

    // fields 14 and 15 = utime and stime (user and system CPU ticks)
    long utime = parts[13].toLong();
    long stime = parts[14].toLong();
    long total = utime + stime;

    float cpuPercent = 0.0f;

    if (m_prevStats.contains(pid)) {
        long prevTotal = m_prevStats[pid].total;
        long delta = total - prevTotal;
        // 2000ms interval, 100 ticks/sec = 200 ticks per interval
        cpuPercent = (delta / 200.0f) * 100.0f;
        if (cpuPercent > 100.0f) cpuPercent = 100.0f;
    }

    m_prevStats[pid] = {utime, stime, total};
    return cpuPercent;
}

QString ProcessMonitor::getProcessUser(int pid) {
    // /proc/[pid] folder is owned by the user who owns the process
    struct stat st;
    QString path = QString("/proc/%1").arg(pid);
    if (stat(path.toStdString().c_str(), &st) != 0) return "unknown";

    struct passwd* pw = getpwuid(st.st_uid);
    if (pw) return QString(pw->pw_name);
    return QString::number(st.st_uid);
}
