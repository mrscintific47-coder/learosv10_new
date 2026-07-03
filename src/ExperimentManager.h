#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ExperimentManager — the brain of LearnOS.
//
// Orchestrates full experiments end-to-end:
//   1. Spawns the right workloads
//   2. Applies kernel settings (writes to /proc/sys)
//   3. Collects metrics every tick
//   4. Fires EventBus events so all panels react
//   5. Builds a result report when done
//
// This is what turns LearnOS from a monitor into a laboratory.
// ─────────────────────────────────────────────────────────────────────────────
#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include "EventBus.h"

// ── One process in an experiment ─────────────────────────────────────────────
struct ExpProcess {
    pid_t   pid       = -1;
    QString name;
    QString workload;   // "cpu" | "memory" | "io" | "mixed"
    int     priority  = 0;
    int     burstLeft = 20;   // for scheduler simulation
    bool    running   = false;
    bool    alive     = false;

    // Metrics collected each tick
    float   cpuPercent = 0;
    long    rssKB      = 0;
    int     waitTicks  = 0;
    int     runTicks   = 0;
    long long prevCpuTime = 0;
};

// ── Metrics snapshot for one experiment tick ──────────────────────────────────
struct ExpMetric {
    int     tick;
    QString runningProcess;
    float   cpuUsage;
    long    totalRssKB;
    int     contextSwitches;
    QString algo;
};

// ── Full experiment result ────────────────────────────────────────────────────
struct ExpResult {
    QString algo;
    int     totalTicks;
    float   avgWaitTime;
    float   avgTurnaround;
    int     contextSwitches;
    float   cpuUtilization;
    QVector<ExpMetric> timeline;
};

// ── Experiment definition ─────────────────────────────────────────────────────
struct Experiment {
    enum Type {
        SchedulerComparison,   // FCFS vs RR vs Priority vs SJF side by side
        MemoryPressure,        // fill RAM, watch kernel respond
        IPCThroughput,         // measure pipe vs shm vs socket bandwidth
        DeadlockDemo,          // create real deadlock, show detection
        PageFaultStorm,        // trigger page faults, watch vmstat
        SignalCascade,         // send signals, watch cascade effects
        ProcessLifecycle,      // full fork→exec→wait→exit cycle
    };

    Type    type;
    QString name;
    QString description;
    int     durationTicks = 30;
    QString algo;          // for scheduler experiments
    int     processCount = 4;
};

class ExperimentManager : public QObject {
    Q_OBJECT

public:
    static ExperimentManager& get() {
        static ExperimentManager instance;
        return instance;
    }

    // ── Run a full experiment ──────────────────────────────────────────────
    void runExperiment(const Experiment& exp);
    void stopExperiment();
    bool isRunning() const { return experimentRunning; }

    // ── Kernel parameter control (writes to /proc/sys) ────────────────────
    // Returns true if write succeeded
    bool setSchedLatency(long ns);          // /proc/sys/kernel/sched_latency_ns
    bool setSchedMinGranularity(long ns);   // /proc/sys/kernel/sched_min_granularity_ns
    bool setSwappiness(int value);          // /proc/sys/vm/swappiness  (0-100)
    bool setOvercommit(int value);          // /proc/sys/vm/overcommit_memory
    bool setDirtyRatio(int value);          // /proc/sys/vm/dirty_ratio

    // Read current kernel parameters
    long readSchedLatency();
    int  readSwappiness();
    int  readOvercommit();

    // ── Available experiments ─────────────────────────────────────────────
    static QVector<Experiment> availableExperiments();

    // ── Last result ───────────────────────────────────────────────────────
    ExpResult lastResult() const { return result; }

signals:
    void experimentStarted(Experiment exp);
    void experimentTick(int tick, int total, QVector<ExpProcess> processes);
    void experimentFinished(ExpResult result);
    void kernelParamChanged(QString param, QString value);
    void kernelWriteFailed(QString param, QString attemptedValue);
    void explanationNeeded(QString html);
    void stageChanged(QString stageName, QString description);

private slots:
    void onTick();

private:
    ExperimentManager() : QObject(nullptr) {}

    QTimer*            tickTimer;
    Experiment         currentExp;
    QVector<ExpProcess> processes;
    ExpResult          result;
    int                currentTick   = 0;
    bool               experimentRunning = false;
    int                contextSwitches = 0;
    QString            lastRunning;

    // Spawn workload processes
    void spawnProcesses(int count, const QStringList& workloads);
    void killAllProcesses();

    // Per-algorithm tick handlers
    void tickFCFS();
    void tickRoundRobin();
    void tickPriority();
    void tickSJF();

    // Metric collection
    void collectMetrics();
    float readProcessCpu(pid_t pid);
    long  readProcessRss(pid_t pid);

    // Kernel writes
    bool writeSysFile(const QString& path, const QString& value);
    QString readSysFile(const QString& path);

    // Result building
    void buildResult();
    QString generateReport();
};
