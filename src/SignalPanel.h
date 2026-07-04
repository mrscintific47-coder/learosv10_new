#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <vector>
#include <string>
#include <unistd.h>
#include <signal.h>

struct SignalInfo {
    int         number;
    const char* name;
    const char* shortDesc;
    const char* fullDesc;
    const char* defaultAction; // "Terminate" | "Ignore" | "Stop" | "Continue" | "Core"
};

struct SignalEvent {
    QString timestamp;
    int     signum;
    pid_t   targetPid;
    QString result;
};

class SignalPanel : public QWidget {
    Q_OBJECT
public:
    explicit SignalPanel(QWidget* parent = nullptr);
    ~SignalPanel();

    // Called from MainWindow to give it the sandbox PIDs
    void setSandboxPids(const std::vector<pid_t>& pids);

signals:
    void explanationNeeded(QString text);

private slots:
    void onSendSignal();
    void onSignalSelected(int index);
    void onRefreshTargets();
    void onTargetSelected(int row, int col);
    void onSpawnTarget();
    void onKillTarget();
    void onAsyncDemo();

private:
    // Signal definitions
    static const std::vector<SignalInfo> SIGNALS;

    // UI
    QComboBox*    signalBox;
    QTableWidget* processTable;
    QTextEdit*    signalLog;
    QLabel*       signalDescLabel;
    QLabel*       signalMaskLabel;   // shows SigPnd/SigBlk/SigCgt from /proc
    QLabel*       statusLabel;
    QPushButton*  sendBtn;
    QPushButton*  spawnBtn;
    QPushButton*  killBtn;
    QPushButton*  asyncDemoBtn;      // async-signal-safety bug demo
    QTimer*       refreshTimer;

    // State
    std::vector<pid_t>    sandboxPids;
    std::vector<pid_t>    ownTargets;
    std::vector<SignalEvent> eventLog;
    pid_t selectedPid = -1;
    pid_t asyncDemoPid = -1;   // process running the async-signal-safety demo

    void refreshProcessTable();
    void logEvent(int signum, pid_t pid, const QString& result);
    QString processState(pid_t pid);
    void readSignalMasks(pid_t pid);   // reads /proc/[pid]/status bitmasks
};
