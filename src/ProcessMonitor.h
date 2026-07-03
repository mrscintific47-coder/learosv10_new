#pragma once
#include <QString>
#include <QVector>
#include <QTimer>
#include <QObject>

// Holds all info about one process
struct ProcessInfo {
    int     pid;
    QString name;
    QString state;       // R=running, S=sleeping, Z=zombie, etc
    long    memoryKB;
    float   cpuPercent;
    int     threads;
    QString user;
    bool    isSandbox;   // is this one of our experimental processes?
};

class ProcessMonitor : public QObject {
    Q_OBJECT

public:
    explicit ProcessMonitor(QObject* parent = nullptr);
    void start();
    void stop();

    QVector<ProcessInfo> getProcesses() const { return m_processes; }

signals:
    void updated(QVector<ProcessInfo> processes);

private slots:
    void refresh();

private:
    ProcessInfo  readProcess(int pid);
    float        calcCpuUsage(int pid);
    QString      getProcessUser(int pid);

    QTimer               m_timer;
    QVector<ProcessInfo> m_processes;

    // For CPU delta calculation
    struct CpuStat { long utime, stime, total; };
    QMap<int, CpuStat>   m_prevStats;
};
