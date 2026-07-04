#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QProcess>
#include <QPainter>
#include <vector>
#include <unistd.h>
#include <pthread.h>

struct ThreadInfo {
    long    tid;
    QString state;        // R/S/D etc. from /proc/task/[tid]/status
    QString futexState;   // extended label when state=S (Futex:, pipe_wait, etc.)
    long    voluntarySwitches;
    long    involuntarySwitches;
    long    rssKB;
    bool    alive;
    // Lock graph (for deadlock demo)
    QString holdsLock;    // "A", "B", "none"
    QString waitsForLock; // "A", "B", "none"
};

// Custom-painted timeline showing which thread is running each second
class ThreadTimeline : public QWidget {
    Q_OBJECT
public:
    explicit ThreadTimeline(QWidget* parent = nullptr);
    void addTick(const QVector<ThreadInfo>& threads);
    void clear();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    struct Tick { QVector<QPair<long,QString>> states; }; // tid -> state
    QVector<Tick> history;
    QVector<long> tids;
    QMap<long,QColor> colors;
    static const QColor PALETTE[];
};

// Lock-graph view: shows which thread holds / waits for which mutex
class LockGraphView : public QWidget {
    Q_OBJECT
public:
    explicit LockGraphView(QWidget* parent = nullptr);
    void setThreads(const QVector<ThreadInfo>& threads);
    void setDeadlockDetected(long tid_a, long tid_b);
    void clear();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<ThreadInfo> threads;
    bool deadlockDetected = false;
    long deadTidA = -1, deadTidB = -1;
};

class ThreadLab : public QWidget {
    Q_OBJECT
public:
    explicit ThreadLab(QWidget* parent = nullptr);
    ~ThreadLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void onSpawnThreads();
    void onKillWorker();
    void onDemoChanged(int index);
    void onRefresh();
    void onWorkerOutput();   // reads STATUS/LOCKSTATE/DEADLOCK_DETECTED lines

private:
    // Worker process
    QProcess* workerProc = nullptr;
    pid_t     workerPid  = -1;

    // Lock graph state (updated from LOCKSTATE protocol lines)
    QMap<long, QString> lockHolds;   // tid → "A"/"B"/"none"
    QMap<long, QString> lockWaits;   // tid → "A"/"B"/"none"
    long deadTidA = -1, deadTidB = -1;

    // UI
    QComboBox*      demoBox;
    QSpinBox*       threadCountSpin;
    QCheckBox*      lockToggle;
    QPushButton*    spawnBtn;
    QPushButton*    killBtn;
    QPushButton*    clearLogBtn;
    QTableWidget*   threadTable;
    ThreadTimeline* timeline;
    LockGraphView*  lockGraph;
    QTextEdit*      logView;
    QLabel*         statusLabel;
    QLabel*         statPid;
    QLabel*         statThreads;
    QLabel*         statRunning;
    QLabel*         counterLabel;
    QTimer*         refreshTimer;

    void refreshTable();
    QVector<ThreadInfo> readThreads(pid_t pid);
    QString readFutexState(pid_t pid, long tid);
};
