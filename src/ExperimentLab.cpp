#include "ExperimentLab.h"
#include "Theme.h"
#include <QHeaderView>
#include <QPainterPath>
#include <QScrollArea>
#include <QSplitter>
#include <QSignalBlocker>

// ── Color palette for processes ───────────────────────────────────────────────
static const QColor PROC_COLORS[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};

// ── ExperimentTimeline ────────────────────────────────────────────────────────

ExperimentTimeline::ExperimentTimeline(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setStyleSheet(QString("background:white;border-radius:10px;border:1px solid %1;").arg(Theme::BORDER));
}

void ExperimentTimeline::clear() {
    history.clear(); processNames.clear(); colors.clear(); update();
}

void ExperimentTimeline::addTick(int tick, const QVector<ExpProcess>& processes) {
    TickState ts; ts.tick = tick;

    int colorIdx = 0;
    for (auto& p : processes) {
        if (!processNames.contains(p.name)) {
            processNames.append(p.name);
            colors[p.name] = PROC_COLORS[colorIdx % 8];
        }
        colorIdx++;
        ts.states.append({p.name, p.running});
    }
    history.append(ts);

    // Keep last 60 ticks
    if (history.size() > 60) history.removeFirst();
    update();
}

void ExperimentTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (history.isEmpty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter, "Timeline will appear when experiment starts");
        return;
    }

    int w=width(), h=height();
    int numProcs = processNames.size();
    if (numProcs == 0) return;

    int rowH    = std::max(16, (h-30) / numProcs);
    int labelW  = 80;
    int chartW  = w - labelW - 10;
    int ticks   = history.size();
    float tickW = ticks > 0 ? (float)chartW / ticks : 10;

    // Process labels
    for (int i=0; i<numProcs; i++) {
        int y = 20 + i*rowH;
        p.setPen(colors[processNames[i]]);
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(2, y+rowH/2+4, processNames[i].left(10));
    }

    // Timeline bars
    for (int t=0; t<history.size(); t++) {
        const TickState& ts = history[t];
        for (auto& state : ts.states) {
            int i = processNames.indexOf(state.first);
            if (i < 0) continue;
            int y  = 20 + i*rowH + 2;
            int x  = labelW + (int)(t * tickW);
            int bw = std::max(1, (int)tickW - 1);
            QRect bar(x, y, bw, rowH-4);

            if (state.second) {
                // Running — solid color
                QPainterPath bp; bp.addRoundedRect(bar, 2, 2);
                p.fillPath(bp, colors[state.first]);
            } else {
                // Waiting — light tint
                QColor c = colors[state.first]; c.setAlpha(40);
                QPainterPath bp; bp.addRoundedRect(bar, 2, 2);
                p.fillPath(bp, c);
            }
        }
        // Tick number every 5
        if (t % 5 == 0) {
            p.setPen(QColor(Theme::TEXT_MUTED));
            p.setFont(QFont("Segoe UI", 7));
            p.drawText(labelW + (int)(t*tickW), 14, QString::number(ts.tick));
        }
    }
}

// ── KernelParamsPanel ─────────────────────────────────────────────────────────

KernelParamsPanel::KernelParamsPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background:transparent;");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(10);

    auto* title = new QLabel("⚙ Kernel Parameters — Live Control");
    title->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* hint = new QLabel("These write directly to /proc/sys — real kernel settings changing in real time.");
    hint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    permissionStatus = new QLabel();
    permissionStatus->setWordWrap(true);
    permissionStatus->setVisible(false);
    permissionStatus->setStyleSheet(QString(
        "background:%1;color:%2;border:1px solid #FED7AA;border-radius:8px;"
        "padding:6px 10px;font-size:10px;font-weight:bold;"
    ).arg(Theme::ORANGE_LIGHT, Theme::TEXT_PRIMARY));
    layout->addWidget(permissionStatus);

    auto makeRow = [&](const QString& label, const QString& path,
                       QSlider*& slider, QLabel*& valLabel,
                       int min, int max, int current) {
        auto* row = new QWidget();
        row->setStyleSheet(QString(
            "background:white;border-radius:8px;border:1px solid %1;").arg(Theme::BORDER));
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(10,8,10,8);
        rl->setSpacing(4);

        auto* top = new QHBoxLayout();
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
        valLabel = new QLabel(QString::number(current));
        valLabel->setStyleSheet(QString("color:%1;font-size:11px;font-family:monospace;").arg(Theme::BLUE));
        auto* pathLbl = new QLabel(path);
        pathLbl->setStyleSheet(QString("color:%1;font-size:9px;font-family:monospace;").arg(Theme::TEXT_MUTED));
        top->addWidget(lbl); top->addWidget(pathLbl); top->addStretch(); top->addWidget(valLabel);
        rl->addLayout(top);

        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(current);
        slider->setStyleSheet(
            "QSlider::groove:horizontal{background:#F1F5F9;height:4px;border-radius:2px;}"
            "QSlider::handle:horizontal{background:#4F6EF7;width:14px;height:14px;margin:-5px 0;border-radius:7px;}"
            "QSlider::sub-page:horizontal{background:#4F6EF7;border-radius:2px;}");
        rl->addWidget(slider);
        layout->addWidget(row);
    };

    makeRow("vm.swappiness", "/proc/sys/vm/swappiness",
            swappinessSlider, swappinessVal, 0, 100,
            ExperimentManager::get().readSwappiness());

    makeRow("sched_latency_ns (ms)", "/proc/sys/kernel/sched_latency_ns",
            schedLatencySlider, schedLatencyVal, 1, 50,
            (int)(ExperimentManager::get().readSchedLatency()/1000000));

    // Overcommit
    auto* ocRow = new QWidget();
    ocRow->setStyleSheet(QString("background:white;border-radius:8px;border:1px solid %1;").arg(Theme::BORDER));
    auto* ocl = new QVBoxLayout(ocRow);
    ocl->setContentsMargins(10,8,10,8); ocl->setSpacing(4);
    auto* ocTop = new QHBoxLayout();
    auto* ocLbl = new QLabel("vm.overcommit_memory");
    ocLbl->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* ocPath = new QLabel("/proc/sys/vm/overcommit_memory");
    ocPath->setStyleSheet(QString("color:%1;font-size:9px;font-family:monospace;").arg(Theme::TEXT_MUTED));
    overcommitVal = new QLabel();
    overcommitVal->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::BLUE));
    ocTop->addWidget(ocLbl); ocTop->addWidget(ocPath); ocTop->addStretch(); ocTop->addWidget(overcommitVal);
    overcommitBox = new QComboBox();
    overcommitBox->addItems({"0 — heuristic (default)", "1 — always overcommit", "2 — never overcommit"});
    overcommitBox->setCurrentIndex(ExperimentManager::get().readOvercommit());
    overcommitBox->setStyleSheet(Theme::input());
    ocl->addLayout(ocTop);
    ocl->addWidget(overcommitBox);
    layout->addWidget(ocRow);

    layout->addStretch();

    // Connections
    connect(swappinessSlider, &QSlider::valueChanged, this, &KernelParamsPanel::onSwappinessChanged);
    connect(schedLatencySlider, &QSlider::valueChanged, this, &KernelParamsPanel::onSchedLatencyChanged);
    connect(overcommitBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &KernelParamsPanel::onOvercommitChanged);
    connect(&ExperimentManager::get(), &ExperimentManager::kernelParamChanged,
            this, &KernelParamsPanel::onParamChanged);
    connect(&ExperimentManager::get(), &ExperimentManager::kernelWriteFailed,
            this, &KernelParamsPanel::onWriteFailed);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &KernelParamsPanel::refreshValues);
    refreshTimer->start(3000);
}

void KernelParamsPanel::onSwappinessChanged(int v) {
    swappinessVal->setText(QString::number(v));
    ExperimentManager::get().setSwappiness(v);
}
void KernelParamsPanel::onSchedLatencyChanged(int v) {
    schedLatencyVal->setText(QString::number(v)+" ms");
    ExperimentManager::get().setSchedLatency((long)v * 1000000);
}
void KernelParamsPanel::onOvercommitChanged(int idx) {
    ExperimentManager::get().setOvercommit(idx);
}
void KernelParamsPanel::onParamChanged(QString param, QString value) {
    permissionStatus->setVisible(false);
    refreshValues();
}
void KernelParamsPanel::onWriteFailed(QString param, QString attemptedValue) {
    permissionStatus->setText(QString(
        "⚠ Couldn't write %1 — permission denied. LearnOS needs root to actually "
        "change kernel parameters (try running it with sudo, or via pkexec). "
        "The slider moved but the kernel value below is unchanged — that's the "
        "real current value, not what you asked for."
    ).arg(param));
    permissionStatus->setVisible(true);
    // Snap displayed values back to whatever the kernel actually reports,
    // so the UI never claims a control change took effect when it didn't.
    refreshValues();
}
void KernelParamsPanel::refreshValues() {
    int realSwap = ExperimentManager::get().readSwappiness();
    swappinessVal->setText(QString::number(realSwap));
    {
        QSignalBlocker blocker(swappinessSlider);
        swappinessSlider->setValue(realSwap);
    }

    long lat = ExperimentManager::get().readSchedLatency();
    schedLatencyVal->setText(QString::number(lat/1000000)+" ms");
    {
        QSignalBlocker blocker(schedLatencySlider);
        schedLatencySlider->setValue((int)(lat/1000000));
    }

    int realOvercommit = ExperimentManager::get().readOvercommit();
    overcommitVal->setText(QString::number(realOvercommit));
    {
        QSignalBlocker blocker(overcommitBox);
        overcommitBox->setCurrentIndex(realOvercommit);
    }
}

// ── ExperimentLab ─────────────────────────────────────────────────────────────

ExperimentLab::ExperimentLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    experiments = ExperimentManager::availableExperiments();

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16,16,16,16);
    outerLayout->setSpacing(10);

    // Title
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🔬  Experiment Manager");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● ONE BUTTON LABS");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::PURPLE).arg(Theme::PURPLE_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outerLayout->addLayout(titleRow);

    // Main split: left=controls, right=kernel params
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(10);

    // ── Left: experiment selector + progress ──
    auto* ctrlCard = new QWidget();
    ctrlCard->setStyleSheet(Theme::card());
    auto* ctrlLayout = new QVBoxLayout(ctrlCard);
    ctrlLayout->setContentsMargins(14,12,14,12);
    ctrlLayout->setSpacing(8);

    auto* ctrlTitle = new QLabel("Select Experiment");
    ctrlTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    ctrlLayout->addWidget(ctrlTitle);

    experimentBox = new QComboBox();
    for (auto& e : experiments) experimentBox->addItem(e.name);
    experimentBox->setStyleSheet(Theme::input());
    ctrlLayout->addWidget(experimentBox);

    expDescLabel = new QLabel();
    expDescLabel->setWordWrap(true);
    expDescLabel->setStyleSheet(QString("color:%1;font-size:11px;background:%2;"
        "border-radius:8px;padding:8px;border:1px solid %3;")
        .arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    expDescLabel->setMinimumHeight(60);
    ctrlLayout->addWidget(expDescLabel);

    // Algorithm selector (for scheduler experiments)
    algoBox = new QComboBox();
    algoBox->addItems({"FCFS — First Come First Served",
                       "Round Robin — Equal time slices",
                       "Priority — Lowest nice value first",
                       "SJF — Shortest Job First"});
    algoBox->setStyleSheet(Theme::input());
    ctrlLayout->addWidget(algoBox);

    auto* btnRow = new QHBoxLayout();
    runBtn = new QPushButton("▶  Run Experiment");
    stopBtn = new QPushButton("■  Stop");
    runBtn->setStyleSheet(
        "QPushButton{background:#A855F7;color:white;border:none;border-radius:8px;"
        "padding:10px;font-size:13px;font-weight:bold;}"
        "QPushButton:hover{background:#9333EA;}"
        "QPushButton:pressed{background:#7C3AED;}");
    stopBtn->setStyleSheet(Theme::btnDanger());
    stopBtn->setEnabled(false);
    btnRow->addWidget(runBtn, 2);
    btnRow->addWidget(stopBtn);
    ctrlLayout->addLayout(btnRow);

    // Progress
    stageLabel = new QLabel("Ready");
    stageLabel->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::BLUE));
    ctrlLayout->addWidget(stageLabel);

    progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(6);
    progressBar->setStyleSheet(Theme::progressBar(Theme::PURPLE));
    ctrlLayout->addWidget(progressBar);

    tickLabel = new QLabel("Tick: 0 / 0");
    tickLabel->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_MUTED));
    ctrlLayout->addWidget(tickLabel);

    topRow->addWidget(ctrlCard, 2);

    // ── Right: kernel params ──
    auto* kpCard = new QWidget();
    kpCard->setStyleSheet(Theme::card());
    auto* kpLayout = new QVBoxLayout(kpCard);
    kpLayout->setContentsMargins(14,12,14,12);
    kernelParams = new KernelParamsPanel();
    connect(kernelParams, &KernelParamsPanel::explanationNeeded,
            this, &ExperimentLab::explanationNeeded);
    kpLayout->addWidget(kernelParams);
    topRow->addWidget(kpCard, 2);
    outerLayout->addLayout(topRow);

    // ── Live process table ──
    auto* liveCard = new QWidget();
    liveCard->setStyleSheet(Theme::card());
    auto* liveLayout = new QVBoxLayout(liveCard);
    liveLayout->setContentsMargins(12,10,12,10);
    liveLayout->setSpacing(6);

    auto* liveTitle = new QLabel("Live Process State");
    liveTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    liveLayout->addWidget(liveTitle);

    processTable = new QTableWidget(0, 6);
    processTable->setHorizontalHeaderLabels({"Name","Workload","State","Burst Left","Wait","RSS"});
    processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    processTable->verticalHeader()->setVisible(false);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setFixedHeight(130);
    processTable->setStyleSheet(Theme::table());
    liveLayout->addWidget(processTable);
    outerLayout->addWidget(liveCard);

    // ── Timeline ──
    auto* timelineCard = new QWidget();
    timelineCard->setStyleSheet(Theme::card());
    auto* timelineLayout = new QVBoxLayout(timelineCard);
    timelineLayout->setContentsMargins(12,10,12,10);
    timelineLayout->setSpacing(4);
    auto* timelineTitle = new QLabel("CPU Timeline — colored = running, faded = waiting");
    timelineTitle->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    timelineLayout->addWidget(timelineTitle);
    timeline = new ExperimentTimeline();
    timelineLayout->addWidget(timeline);
    outerLayout->addWidget(timelineCard, 1);

    // ── Results log ──
    auto* resCard = new QWidget();
    resCard->setStyleSheet(Theme::card());
    auto* resLayout = new QVBoxLayout(resCard);
    resLayout->setContentsMargins(12,10,12,10);
    resLayout->setSpacing(4);
    auto* resTitle = new QLabel("Results & Analysis");
    resTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    resLayout->addWidget(resTitle);
    resultsLog = new QTextEdit();
    resultsLog->setReadOnly(true);
    resultsLog->setMinimumHeight(80);
    resultsLog->setMaximumHeight(120);
    resultsLog->setStyleSheet(Theme::termLog());
    resLayout->addWidget(resultsLog);
    outerLayout->addWidget(resCard);

    // ── Connections ──
    connect(experimentBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExperimentLab::onExperimentSelected);
    connect(algoBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExperimentLab::onAlgoChanged);
    connect(runBtn,  &QPushButton::clicked, this, &ExperimentLab::onRunExperiment);
    connect(stopBtn, &QPushButton::clicked, this, &ExperimentLab::onStopExperiment);

    connect(&ExperimentManager::get(), &ExperimentManager::experimentStarted,
            this, &ExperimentLab::onExperimentStarted);
    connect(&ExperimentManager::get(), &ExperimentManager::experimentTick,
            this, &ExperimentLab::onExperimentTick);
    connect(&ExperimentManager::get(), &ExperimentManager::experimentFinished,
            this, &ExperimentLab::onExperimentFinished);
    connect(&ExperimentManager::get(), &ExperimentManager::stageChanged,
            this, &ExperimentLab::onStageChanged);
    connect(&ExperimentManager::get(), &ExperimentManager::explanationNeeded,
            this, &ExperimentLab::explanationNeeded);

    // Init description
    onExperimentSelected(0);
}

void ExperimentLab::onExperimentSelected(int idx) {
    if (idx < 0 || idx >= experiments.size()) return;
    expDescLabel->setText(experiments[idx].description);
    bool isScheduler = experiments[idx].type == Experiment::SchedulerComparison;
    algoBox->setVisible(isScheduler);
}

void ExperimentLab::onAlgoChanged(int idx) {
    static const char* algos[] = {"fcfs","rr","priority","sjf"};
    if (idx >= 0 && idx < 4) {
        // Update current experiment's algo
    }
}

void ExperimentLab::onRunExperiment() {
    int idx = experimentBox->currentIndex();
    if (idx < 0 || idx >= experiments.size()) return;

    Experiment exp = experiments[idx];

    // Set algorithm
    static const char* algos[] = {"fcfs","rr","priority","sjf"};
    int algoIdx = algoBox->currentIndex();
    if (algoIdx >= 0 && algoIdx < 4) exp.algo = algos[algoIdx];

    timeline->clear();
    resultsLog->clear();
    progressBar->setValue(0);

    runBtn->setEnabled(false);
    stopBtn->setEnabled(true);

    ExperimentManager::get().runExperiment(exp);
}

void ExperimentLab::onStopExperiment() {
    ExperimentManager::get().stopExperiment();
    runBtn->setEnabled(true);
    stopBtn->setEnabled(false);
    stageLabel->setText("Stopped");
}

void ExperimentLab::onExperimentStarted(Experiment exp) {
    stageLabel->setText(QString("▶ %1").arg(exp.name));
    tickLabel->setText(QString("Tick: 0 / %1").arg(exp.durationTicks));
    processTable->setRowCount(0);
}

void ExperimentLab::onExperimentTick(int tick, int total, QVector<ExpProcess> processes) {
    progressBar->setValue((int)(100.0 * tick / total));
    tickLabel->setText(QString("Tick: %1 / %2").arg(tick).arg(total));
    updateProcessTable(processes);
    timeline->addTick(tick, processes);
}

void ExperimentLab::onExperimentFinished(ExpResult result) {
    runBtn->setEnabled(true);
    stopBtn->setEnabled(false);
    progressBar->setValue(100);

    resultsLog->setHtml(QString(
        "<b>%1 — Complete</b><br>"
        "Avg wait: <b>%2</b> ticks  |  "
        "Avg turnaround: <b>%3</b> ticks  |  "
        "Context switches: <b>%4</b>  |  "
        "CPU utilization: <b>%5%</b>"
    ).arg(result.algo.toUpper())
     .arg(result.avgWaitTime, 0, 'f', 1)
     .arg(result.avgTurnaround, 0, 'f', 1)
     .arg(result.contextSwitches)
     .arg(result.cpuUtilization, 0, 'f', 1));
}

void ExperimentLab::onStageChanged(QString stage, QString desc) {
    stageLabel->setText(QString("● %1 — %2").arg(stage).arg(desc));
}

void ExperimentLab::updateProcessTable(const QVector<ExpProcess>& processes) {
    processTable->setRowCount(0);
    int colorIdx = 0;
    for (auto& p : processes) {
        int row = processTable->rowCount();
        processTable->insertRow(row);
        auto cell = [&](const QString& t, const char* c=nullptr){
            auto* i = new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            if (c) i->setForeground(QColor(c));
            return i;
        };
        QString state = !p.alive ? "Done" : p.running ? "▶ Running" : "⏸ Waiting";
        const char* stateColor = !p.alive ? Theme::TEXT_MUTED :
                                  p.running ? Theme::GREEN : Theme::ORANGE;
        processTable->setItem(row,0,cell(p.name, PROC_COLORS[colorIdx%8].name().toLatin1().constData()));
        processTable->setItem(row,1,cell(p.workload));
        processTable->setItem(row,2,cell(state, stateColor));
        processTable->setItem(row,3,cell(p.burstLeft > 0 ? QString::number(p.burstLeft) : "✓"));
        processTable->setItem(row,4,cell(QString::number(p.waitTicks)));
        processTable->setItem(row,5,cell(QString("%1 KB").arg(p.rssKB)));
        colorIdx++;
    }
}
