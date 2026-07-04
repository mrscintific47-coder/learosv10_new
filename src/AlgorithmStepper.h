#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QProcess>
#include <vector>
#include <unistd.h>

// ── Live Gantt chart ─────────────────────────────────────────────────────────
// Shows sampled /proc data: one column per sample tick, one row per TID.
// Color-filled = real "R" state from /proc; faded = sleeping/waiting.
class SchedGanttView : public QWidget {
    Q_OBJECT
public:
    explicit SchedGanttView(QWidget* parent = nullptr);

    struct TidSample {
        long    tid;
        QString policy;   // OTHER / FIFO / RR / DEADLINE
        int     priority; // sched_priority
        int     nice;
        long    utime;
        long    stime;
        long    switches;
        QString state;    // R / S / D etc. from /proc/stat
    };

    void addSample(const QVector<TidSample>& tids);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Tick { QVector<TidSample> tids; };
    QVector<Tick>  history;
    QVector<long>  knownTids;
    QMap<long,QColor> colors;
    static const QColor PALETTE[];
};

// ── AlgorithmStepper  (now "Scheduler Lab") ──────────────────────────────────
class AlgorithmStepper : public QWidget {
    Q_OBJECT
public:
    explicit AlgorithmStepper(QWidget* parent = nullptr);
    void loadProcesses(const std::vector<pid_t>& pids);

signals:
    void explanationNeeded(QString text);
    void applyNice(pid_t pid, int nice);

private slots:
    void onSpawnWorker();
    void onKillWorker();
    void onPolicyChanged(int idx);
    void onWorkerOutput();
    void onSampleTick();

private:
    // Worker process
    QProcess* workerProc = nullptr;
    pid_t     workerPid  = -1;
    QTimer*   sampleTimer = nullptr;

    // UI
    QComboBox*    policyBox;
    QSpinBox*     threadSpin;
    QPushButton*  spawnBtn;
    QPushButton*  killBtn;
    QTableWidget* tidTable;
    SchedGanttView* ganttView;
    QTextEdit*    logView;
    QLabel*       statusLabel;
    QLabel*       statPid;
    QLabel*       statPolicy;
    QLabel*       statThreads;

    // Latest sample
    QVector<SchedGanttView::TidSample> latestSamples;

    void refreshTable(const QVector<SchedGanttView::TidSample>& samples);
    QVector<SchedGanttView::TidSample> parseSamples();
};
