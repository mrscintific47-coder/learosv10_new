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

private:
    // Signal definitions
    static const std::vector<SignalInfo> SIGNALS;

    // UI
    QComboBox*    signalBox;
    QTableWidget* processTable;
    QTextEdit*    signalLog;
    QLabel*       signalDescLabel;
    QLabel*       statusLabel;
    QPushButton*  sendBtn;
    QPushButton*  spawnBtn;
    QPushButton*  killBtn;
    QTimer*       refreshTimer;

    // State
    std::vector<pid_t>    sandboxPids;
    std::vector<pid_t>    ownTargets;   // processes we spawned here
    std::vector<SignalEvent> eventLog;
    pid_t selectedPid = -1;

    void refreshProcessTable();
    void logEvent(int signum, pid_t pid, const QString& result);
    QString processState(pid_t pid);
};
