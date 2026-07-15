#include "MainWindow.h"
#include "Theme.h"
#include "EventBus.h"
#include <QStatusBar>
#include <QLabel>
#include <QScrollArea>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LearnOS — Linux Systems Laboratory");
    setMinimumSize(1280, 800);
    setStyleSheet(QString(
        "QMainWindow { background: %1; }"
        "QStatusBar  { background: #181C2E; border-top: 1px solid #252A40;"
        "              color: #64748B; font-size: 10px; padding: 0 12px; }"
    ).arg(Theme::BG_APP));
    setupUI();
    connectSignals();
}

void MainWindow::setupUI() {
    // ── Root: horizontal splitter ─────────────────────────────────────────────
    auto* root = new QSplitter(Qt::Horizontal, this);
    root->setHandleWidth(1);
    root->setStyleSheet(QString("QSplitter::handle { background: %1; }").arg(Theme::BORDER));

    // ══════════════════════════════════════════════════════════════════════════
    // LEFT SIDEBAR — dark navy, always visible
    // ══════════════════════════════════════════════════════════════════════════
    auto* sidebar = new QWidget();
    sidebar->setFixedWidth(270);
    sidebar->setStyleSheet(QString("background: %1;").arg(Theme::BG_SIDEBAR));

    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    // ── App header
    auto* sideHeader = new QWidget();
    sideHeader->setFixedHeight(60);
    sideHeader->setStyleSheet(
        "background: #181C2E; border-bottom: 1px solid rgba(255,255,255,0.07);");
    auto* headerRow = new QHBoxLayout(sideHeader);
    headerRow->setContentsMargins(16, 0, 12, 0);

    auto* appIcon = new QLabel("⬡");
    appIcon->setStyleSheet("color: #4F6EF7; font-size: 22px; background: transparent;");

    auto* appTitle = new QLabel("LearnOS");
    appTitle->setStyleSheet(
        "color: #F8FAFC; font-size: 16px; font-weight: 800; "
        "letter-spacing: -0.01em; background: transparent;");

    auto* liveChip = new QLabel("LIVE");
    liveChip->setStyleSheet(
        "color: #4ADE80; background: rgba(74,222,128,0.12);"
        "border: 1px solid rgba(74,222,128,0.25);"
        "border-radius: 5px; padding: 2px 8px;"
        "font-size: 9px; font-weight: 800; letter-spacing: .10em;");

    headerRow->addWidget(appIcon);
    headerRow->addSpacing(6);
    headerRow->addWidget(appTitle);
    headerRow->addStretch();
    headerRow->addWidget(liveChip);
    sideLayout->addWidget(sideHeader);

    // ── Heat map (takes most of the sidebar)
    heatMap = new HeatMap();
    sideLayout->addWidget(heatMap, 3);

    // ── Divider
    auto* div = new QWidget();
    div->setFixedHeight(1);
    div->setStyleSheet("background: rgba(255,255,255,0.07);");
    sideLayout->addWidget(div);

    // ── Activity feed
    activityFeed = new ActivityFeed();
    sideLayout->addWidget(activityFeed, 2);

    root->addWidget(sidebar);

    // ══════════════════════════════════════════════════════════════════════════
    // CENTER — top bar + tab widget
    // ══════════════════════════════════════════════════════════════════════════
    auto* center = new QWidget();
    center->setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // ── Top bar
    auto* topBar = new QWidget();
    topBar->setFixedHeight(52);
    topBar->setStyleSheet(
        "background: white;"
        "border-bottom: 2px solid #E2E8F0;");
    auto* topRow = new QHBoxLayout(topBar);
    topRow->setContentsMargins(20, 0, 16, 0);
    topRow->setSpacing(10);

    auto* appDot = new QLabel("⬡");
    appDot->setStyleSheet("color: #4F6EF7; font-size: 16px;");

    auto* pageTitle = new QLabel("Linux Systems Laboratory");
    pageTitle->setStyleSheet(
        "color: #0F172A; font-size: 14px; font-weight: 800; letter-spacing: -0.01em;");

    auto* versionTag = new QLabel("v10");
    versionTag->setStyleSheet(
        "color: #4F6EF7; background: #EEF2FF; border-radius: 6px;"
        "padding: 3px 9px; font-size: 10px; font-weight: 700;");

    auto* kaliTag = new QLabel("Kali Linux");
    kaliTag->setStyleSheet(
        "color: #DC2626; background: #FEF2F2; border-radius: 6px;"
        "padding: 3px 9px; font-size: 10px; font-weight: 700;");

    topRow->addWidget(appDot);
    topRow->addSpacing(4);
    topRow->addWidget(pageTitle);
    topRow->addStretch();
    topRow->addWidget(kaliTag);
    topRow->addSpacing(4);
    topRow->addWidget(versionTag);
    centerLayout->addWidget(topBar);

    // ── Tab widget — scrollable, compact tabs
    tabs = new QTabWidget();
    tabs->setStyleSheet(Theme::tabs());
    tabs->setDocumentMode(true);
    tabs->setUsesScrollButtons(true);  // arrow buttons appear when tabs overflow
    tabs->setElideMode(Qt::ElideNone);

    // Instantiate all lab widgets
    processViewer    = new ProcessViewer();
    cpuMemMonitor    = new CpuMemMonitor();
    sandboxManager   = new SandboxManager();
    algoStepper      = new AlgorithmStepper();
    memoryLab        = new MemoryLab();
    dataStructureLab = new DataStructureLab();
    ipcLab           = new IPCLab();
    signalPanel      = new SignalPanel();
    threadLab        = new ThreadLab();
    namespaceLab     = new NamespaceLab();
    ebpfLab          = new EbpfLab();
    filesystemLab    = new FilesystemLab();

    // Tab labels — no emojis on the bar itself (they render inconsistently
    // across Linux DEs); use short, scannable text with a category prefix
    tabs->addTab(processViewer,    "Processes");
    tabs->addTab(cpuMemMonitor,    "CPU & RAM");
    tabs->addTab(sandboxManager,   "Sandbox");
    tabs->addTab(algoStepper,      "Scheduler");
    tabs->addTab(memoryLab,        "Mem Lab");
    tabs->addTab(dataStructureLab, "DS Lab");
    tabs->addTab(ipcLab,           "IPC");
    tabs->addTab(signalPanel,      "Signals");
    tabs->addTab(threadLab,        "Threads");
    tabs->addTab(namespaceLab,     "Namespaces");
    tabs->addTab(ebpfLab,          "Observability");
    tabs->addTab(filesystemLab,    "Filesystem");

    centerLayout->addWidget(tabs, 1);
    root->addWidget(center);

    // ══════════════════════════════════════════════════════════════════════════
    // RIGHT — Explainer panel, wider, always visible
    // ══════════════════════════════════════════════════════════════════════════
    explainer = new Explainer();
    root->addWidget(explainer);

    // Splitter proportions: sidebar=fixed, center=stretch, explainer=fixed
    root->setStretchFactor(0, 0);
    root->setStretchFactor(1, 1);
    root->setStretchFactor(2, 0);

    setCentralWidget(root);

    // Status bar
    statusBar()->showMessage(
        "  LearnOS v10   ·   live /proc data   ·   real kernel syscalls   ·   Scheduler tab = algorithm visualiser only");

    heatMapDriver = new HeatMapDriver(heatMap, this);
}

void MainWindow::connectSignals() {
    auto ex = explainer;

    connect(processViewer,    &ProcessViewer::explanationNeeded,    ex, &Explainer::setExplanation);
    connect(cpuMemMonitor,    &CpuMemMonitor::explanationNeeded,    ex, &Explainer::setExplanation);
    connect(sandboxManager,   &SandboxManager::explanationNeeded,   ex, &Explainer::setExplanation);
    connect(algoStepper,      &AlgorithmStepper::explanationNeeded, ex, &Explainer::setExplanation);
    connect(memoryLab,        &MemoryLab::explanationNeeded,        ex, &Explainer::setExplanation);
    connect(dataStructureLab, &DataStructureLab::explanationNeeded, ex, &Explainer::setExplanation);
    connect(ipcLab,           &IPCLab::explanationNeeded,           ex, &Explainer::setExplanation);
    connect(signalPanel,      &SignalPanel::explanationNeeded,       ex, &Explainer::setExplanation);
    connect(heatMap,          &HeatMap::explanationNeeded,          ex, &Explainer::setExplanation);
    connect(threadLab,        &ThreadLab::explanationNeeded,        ex, &Explainer::setExplanation);
    connect(namespaceLab,     &NamespaceLab::explanationNeeded,     ex, &Explainer::setExplanation);
    connect(ebpfLab,          &EbpfLab::explanationNeeded,          ex, &Explainer::setExplanation);
    connect(filesystemLab,    &FilesystemLab::explanationNeeded,    ex, &Explainer::setExplanation);

    // Cross-tab wiring
    connect(sandboxManager, &SandboxManager::processesChanged,
            algoStepper,    &AlgorithmStepper::loadProcesses);
    connect(sandboxManager, &SandboxManager::processesChanged,
            signalPanel,    &SignalPanel::setSandboxPids);

    // Sandbox process selected → load process stack in DS Lab + show memory map
    connect(sandboxManager, &SandboxManager::processSelected,
            this, [this](pid_t pid) {
                dataStructureLab->loadProcessStack(pid);
                memoryLab->showMemMapForPid(pid);
            });

    // Click a process in ProcessViewer → jump to Mem Lab tab (now index 4)
    connect(processViewer, &ProcessViewer::pidSelected,
            this, [this](pid_t) {
                tabs->setCurrentIndex(4); // Mem Lab tab
            });

    // Signal fired at a sandbox PID → notify Sandbox so it can remove dead processes.
    connect(&EventBus::get(), &EventBus::osEvent,
            this, [this](const OSEvent& e) {
                if (e.type == OSEvent::SignalSent && e.pid > 0)
                    sandboxManager->checkProcessAlive(e.pid);
            });

    // ── Lazy-start MemoryLab worker the first time its tab is shown ───────────
    // Mem Lab tab is index 4. Starting the worker here (not in MemoryLab ctor)
    // prevents an extra process from being spawned on application launch.
    connect(tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        // Mem Lab is at fixed index 4
        if (idx == 4) memoryLab->ensureStarted();
    });
}
