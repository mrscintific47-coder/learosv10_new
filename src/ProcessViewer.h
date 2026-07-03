#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QTimer>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>
#include <string>

struct ProcessInfo {
    int pid;
    std::string name;
    std::string state;
    float cpuPercent;
    long memKB;
    std::string user;
    bool isSystem;
    bool isSandbox;
};

class ProcessViewer : public QWidget {
    Q_OBJECT
public:
    explicit ProcessViewer(QWidget* parent = nullptr);
signals:
    void processSelected(ProcessInfo info);
    void pidSelected(pid_t pid);  // for memory inspector
    void explanationNeeded(QString text);
private slots:
    void refresh();
    void onRowClicked(int row, int col);
    void onFilterChanged(const QString& text);
private:
    QTableWidget* table;
    QTimer*       refreshTimer;
    QLineEdit*    filterInput;
    QLabel*       statusLabel;
    QLabel*       runningChip;
    QLabel*       sleepingChip;
    QLabel*       totalChip;
    std::vector<ProcessInfo> allProcesses;
    std::vector<ProcessInfo> readAllProcesses();
    ProcessInfo   readProcess(int pid);
    float         computeCpuUsage(int pid);
    void          colorRow(int row, const ProcessInfo& p);
    void          populateTable(const std::vector<ProcessInfo>& processes);
};
