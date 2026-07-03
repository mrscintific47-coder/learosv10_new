#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QTabWidget>
#include "HeatMap.h"
#include "HeatMapDriver.h"
#include "ProcessViewer.h"
#include "CpuMemMonitor.h"
#include "SandboxManager.h"
#include "AlgorithmStepper.h"
#include "MemoryInspector.h"
#include "MemoryLab.h"
#include "DataStructureLab.h"
#include "IPCLab.h"
#include "SignalPanel.h"
#include "ActivityFeed.h"
#include "ExperimentLab.h"
#include "Explainer.h"
#include "ThreadLab.h"
#include "NamespaceLab.h"
#include "EbpfLab.h"
#include "FilesystemLab.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    HeatMap*          heatMap;
    HeatMapDriver*    heatMapDriver;
    ActivityFeed*     activityFeed;
    ProcessViewer*    processViewer;
    CpuMemMonitor*    cpuMemMonitor;
    SandboxManager*   sandboxManager;
    AlgorithmStepper* algoStepper;
    MemoryInspector*  memInspector;
    MemoryLab*        memoryLab;
    DataStructureLab* dataStructureLab;
    IPCLab*           ipcLab;
    SignalPanel*      signalPanel;
    Explainer*        explainer;
    QTabWidget*       tabs;
    ExperimentLab*    experimentLab;
    ThreadLab*        threadLab;
    NamespaceLab*     namespaceLab;
    EbpfLab*          ebpfLab;
    FilesystemLab*    filesystemLab;
    void setupUI();
    void connectSignals();
};
