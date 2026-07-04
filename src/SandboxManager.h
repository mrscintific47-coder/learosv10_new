#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTextEdit>
#include <QTimer>
#include <vector>
#include <unistd.h>
#include <signal.h>

enum class WorkloadType { CPU, MEMORY, IO };

struct SandboxProcess {
    pid_t        pid;
    std::string  name;
    WorkloadType type;
    int          priority;
    bool         paused;
};

class SandboxManager : public QWidget {
    Q_OBJECT

public:
    explicit SandboxManager(QWidget* parent = nullptr);
    ~SandboxManager();
    std::vector<pid_t> sandboxPids() const;

signals:
    void explanationNeeded(QString text);
    void processesChanged(std::vector<pid_t> pids);
    void processSelected(pid_t pid);

public slots:
    // Called externally when a signal may have killed one of our processes
    void checkProcessAlive(pid_t pid);

private slots:
    void spawnCPU();
    void spawnMemory();
    void spawnIO();
    void killSelected();
    void pauseSelected();
    void resumeSelected();
    void onRowClicked(QListWidgetItem* item);
    void applyScheduling();
    void onZombieReap();
    void onSeccompDemo();
    void onCgroupDemo();

private:
    QListWidget* processList;
    QComboBox*   algorithmBox;
    QTextEdit*   seccompLog;
    QLabel*      cgroupLabel;

    std::vector<SandboxProcess> processes;
    pid_t seccompDemoPid = -1;

    void spawn(WorkloadType type);
    void refreshList();
    void emitPids();
    void explainAlgorithm(const QString& algo);
};
