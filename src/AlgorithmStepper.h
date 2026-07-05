#pragma once
#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>
#include <deque>
#include <unistd.h>

// One process in the scheduler simulation
struct SchedProcess {
    pid_t       pid;
    QString     name;
    int         priority;    // nice value
    int         burstLeft;   // remaining CPU ticks
    int         waitTime;
    int         state;       // 0=ready 1=running 2=done
    QColor      color;
};

class AlgorithmStepper : public QWidget {
    Q_OBJECT

public:
    explicit AlgorithmStepper(QWidget* parent = nullptr);
    void loadProcesses(const std::vector<pid_t>& pids);

signals:
    void explanationNeeded(QString text);
    void applyNice(pid_t pid, int nice);

private slots:
    void stepOnce();
    void toggleAutoPlay();
    void onAlgoChanged(const QString& algo);
    void onSpeedChanged(int val);

private:
    // UI
    QComboBox*    algoBox;
    QTableWidget* processTable;
    QLabel*       tickLabel;
    QLabel*       procCountLbl;
    QLabel*       statusLabel;
    QPushButton*  stepBtn;
    QPushButton*  playBtn;
    QSlider*      speedSlider;
    QLabel*       ganttLabel;   // shows recent CPU assignments as colored text

    // State
    QTimer*                  autoTimer;
    int                      tick;
    int                      quantum;        // for Round Robin
    int                      currentSlot;   // index into readyQueue for RR
    std::vector<SchedProcess> allProcs;
    std::deque<int>           readyQueue;   // indices into allProcs
    QString                  ganttHistory;  // last 20 assignments

    void resetScheduler();
    void stepFCFS();
    void stepRR();
    void stepPriority();
    void stepSJF();
    void refreshTable();
    void appendGantt(const QString& name, const QColor& color);
    QColor processColor(int index);
};
