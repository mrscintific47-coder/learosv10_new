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
#include <QPainter>
#include <vector>
#include <unistd.h>
#include <pthread.h>

struct ThreadInfo {
    long    tid;
    QString state;
    long    voluntarySwitches;
    long    involuntarySwitches;
    long    rssKB;
    bool    alive;
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

private:
    // Worker process PID (threads live inside it)
    pid_t workerPid = -1;

    // UI
    QComboBox*      demoBox;
    QSpinBox*       threadCountSpin;
    QPushButton*    spawnBtn;
    QPushButton*    killBtn;
    QPushButton*    clearLogBtn;
    QTableWidget*   threadTable;
    ThreadTimeline* timeline;
    QTextEdit*      logView;
    QLabel*         statusLabel;
    QLabel*         statPid;
    QLabel*         statThreads;
    QLabel*         statRunning;
    QTimer*         refreshTimer;

    void refreshTable();
    QVector<ThreadInfo> readThreads(pid_t pid);
    long readCtxSwitches(pid_t pid, long tid, bool voluntary);
};
