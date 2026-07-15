#include "AlgorithmStepper.h"
#include "EventBus.h"
#include "Theme.h"
#include <QHeaderView>
#include <QFont>
#include <sys/resource.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

static QColor COLORS[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};

AlgorithmStepper::AlgorithmStepper(QWidget* parent)
    : QWidget(parent), tick(0), quantum(3), currentSlot(0)
{
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Title row ─────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("⚙  Scheduling Algorithm Stepper");
    title->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● SIMULATOR");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::PURPLE).arg(Theme::PURPLE_LIGHT));
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Load sandbox processes, then step through FCFS, Round Robin, Priority, or SJF. "
        "Watch the Gantt chart grow and see which process runs each tick.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString(
        "background:%1; color:%2; border:1px solid %3;"
        "border-radius:10px; padding:10px 14px; font-size:11px;"
    ).arg(Theme::PURPLE_LIGHT, Theme::TEXT_PRIMARY, Theme::BORDER));
    outer->addWidget(hint);

    // ── Controls card ─────────────────────────────────────────────────────────
    auto* ctrl = new QWidget();
    ctrl->setStyleSheet(Theme::card());
    auto* ctrlL = new QGridLayout(ctrl);
    ctrlL->setContentsMargins(16, 12, 16, 12);
    ctrlL->setSpacing(10);

    auto mkLabel = [&](const QString& txt) -> QLabel* {
        auto* l = new QLabel(txt);
        l->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
            .arg(Theme::TEXT_SECONDARY));
        return l;
    };
    ctrlL->addWidget(mkLabel("Algorithm"), 0, 0);
    ctrlL->addWidget(mkLabel(""),          0, 1);
    ctrlL->addWidget(mkLabel(""),          0, 2);
    ctrlL->addWidget(mkLabel("Speed"),     0, 3);

    algoBox = new QComboBox();
    algoBox->addItems({
        "FCFS — First Come First Served",
        "Round Robin  (quantum = 3 ticks)",
        "Priority — lowest nice value wins",
        "SJF — Shortest Job First"
    });
    algoBox->setStyleSheet(Theme::input());

    stepBtn = new QPushButton("▶ Step");
    stepBtn->setStyleSheet(Theme::btnPrimary());
    stepBtn->setMinimumHeight(32);

    playBtn = new QPushButton("⏵ Auto Play");
    playBtn->setStyleSheet(Theme::btnSuccess());
    playBtn->setMinimumHeight(32);

    auto* resetBtn = new QPushButton("↺ Reset");
    resetBtn->setStyleSheet(Theme::btnGhost());
    resetBtn->setMinimumHeight(32);

    speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setRange(100, 2000);
    speedSlider->setValue(800);
    speedSlider->setStyleSheet(Theme::slider(Theme::PURPLE));

    ctrlL->addWidget(algoBox,     1, 0);
    ctrlL->addWidget(stepBtn,     1, 1);
    ctrlL->addWidget(playBtn,     1, 2);
    ctrlL->addWidget(speedSlider, 1, 3);
    ctrlL->addWidget(resetBtn,    2, 0);
    ctrlL->setColumnStretch(0, 3);
    ctrlL->setColumnStretch(1, 1);
    ctrlL->setColumnStretch(2, 1);
    ctrlL->setColumnStretch(3, 2);
    outer->addWidget(ctrl);

    // ── Tick stat row ─────────────────────────────────────────────────────────
    auto* statRow = new QHBoxLayout();
    statRow->setSpacing(10);

    auto mkStatCard = [&](const QString& label, QLabel*& valueOut,
                          const char* accent) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet(QString(
            "background:white; border-radius:10px; border:1px solid %1;"
        ).arg(Theme::BORDER));
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(14, 10, 14, 10);
        vl->setSpacing(2);
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QString("color:%1; font-size:10px; font-weight:600;")
            .arg(Theme::TEXT_MUTED));
        valueOut = new QLabel("0");
        valueOut->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700;")
            .arg(accent));
        vl->addWidget(lbl);
        vl->addWidget(valueOut);
        return card;
    };

    statRow->addWidget(mkStatCard("Current Tick",    tickLabel,   Theme::PURPLE));
    statRow->addWidget(mkStatCard("Processes Loaded", procCountLbl, Theme::BLUE));
    outer->addLayout(statRow);

    // ── Process table ─────────────────────────────────────────────────────────
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tl = new QVBoxLayout(tableCard);
    tl->setContentsMargins(14, 12, 14, 12);
    tl->setSpacing(6);
    auto* tableTitle = new QLabel("Process Queue");
    tableTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    tl->addWidget(tableTitle);

    processTable = new QTableWidget(0, 5);
    processTable->setHorizontalHeaderLabels({"PID","Name","Priority","Burst Left","State"});
    processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    processTable->verticalHeader()->setVisible(false);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setAlternatingRowColors(true);
    processTable->setShowGrid(false);
    processTable->setMinimumHeight(100);
    processTable->setMaximumHeight(160);
    processTable->setStyleSheet(Theme::table());
    tl->addWidget(processTable);
    outer->addWidget(tableCard);

    // ── Gantt strip ───────────────────────────────────────────────────────────
    auto* ganttCard = new QWidget();
    ganttCard->setStyleSheet(Theme::card());
    auto* gl = new QVBoxLayout(ganttCard);
    gl->setContentsMargins(14, 12, 14, 12);
    gl->setSpacing(6);

    auto* ganttHeader = new QHBoxLayout();
    auto* ganttTitleLbl = new QLabel("CPU Gantt Chart");
    ganttTitleLbl->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    statusLabel = new QLabel("Load sandbox processes, then step through the algorithm.");
    statusLabel->setStyleSheet(QString(
        "color:%1; font-size:10px;"
    ).arg(Theme::TEXT_MUTED));
    ganttHeader->addWidget(ganttTitleLbl);
    ganttHeader->addStretch();
    ganttHeader->addWidget(statusLabel);
    gl->addLayout(ganttHeader);

    ganttLabel = new QLabel("(not started)");
    ganttLabel->setStyleSheet(QString(
        "background:%1; border:1px solid %2; border-radius:8px;"
        "padding:8px 10px; font-family:Consolas; font-size:11px; color:%3;"
    ).arg(Theme::BG_INPUT, Theme::BORDER, Theme::TEXT_PRIMARY));
    ganttLabel->setWordWrap(true);
    ganttLabel->setMinimumHeight(44);
    gl->addWidget(ganttLabel);
    outer->addWidget(ganttCard);

    outer->addStretch();

    // Auto play timer
    autoTimer = new QTimer(this);
    connect(autoTimer, &QTimer::timeout, this, &AlgorithmStepper::stepOnce);

    connect(stepBtn,    &QPushButton::clicked, this, &AlgorithmStepper::stepOnce);
    connect(playBtn,    &QPushButton::clicked, this, &AlgorithmStepper::toggleAutoPlay);
    connect(resetBtn,   &QPushButton::clicked, this, &AlgorithmStepper::resetScheduler);
    connect(algoBox,    &QComboBox::currentTextChanged, this, &AlgorithmStepper::onAlgoChanged);
    connect(speedSlider,&QSlider::valueChanged, this, &AlgorithmStepper::onSpeedChanged);
}

void AlgorithmStepper::loadProcesses(const std::vector<pid_t>& pids) {
    allProcs.clear();
    int idx = 0;
    for (pid_t pid : pids) {
        SchedProcess p;
        p.pid      = pid;
        p.burstLeft = 10 + (idx * 3);
        p.waitTime  = 0;
        p.state     = 0;
        p.color     = COLORS[idx % 8];

        std::ifstream f("/proc/" + std::to_string(pid) + "/status");
        std::string line;
        p.name = QString("proc_%1").arg(pid);
        while (std::getline(f, line)) {
            if (line.rfind("Name:",0)==0) {
                p.name = QString::fromStdString(line.substr(6));
                break;
            }
        }
        p.priority = getpriority(PRIO_PROCESS, pid);
        allProcs.push_back(p);
        idx++;
    }
    resetScheduler();
}

void AlgorithmStepper::resetScheduler() {
    tick        = 0;
    currentSlot = 0;
    ganttHistory.clear();
    ganttLabel->setText("(not started)");
    tickLabel->setText("0");
    procCountLbl->setText(QString::number(allProcs.size()));

    for (auto& p : allProcs) {
        p.state    = 0;
        p.waitTime = 0;
        p.burstLeft = 10 + (&p - &allProcs[0]) * 3;
    }

    readyQueue.clear();
    QString algo = algoBox->currentText();
    if (algo.startsWith("SJF")) {
        std::vector<int> indices(allProcs.size());
        for (int i=0;i<(int)allProcs.size();i++) indices[i]=i;
        std::sort(indices.begin(), indices.end(), [&](int a, int b){
            return allProcs[a].burstLeft < allProcs[b].burstLeft;
        });
        for (int i : indices) readyQueue.push_back(i);
    } else if (algo.startsWith("Priority")) {
        std::vector<int> indices(allProcs.size());
        for (int i=0;i<(int)allProcs.size();i++) indices[i]=i;
        std::sort(indices.begin(), indices.end(), [&](int a, int b){
            return allProcs[a].priority < allProcs[b].priority;
        });
        for (int i : indices) readyQueue.push_back(i);
    } else {
        for (int i=0;i<(int)allProcs.size();i++) readyQueue.push_back(i);
    }

    statusLabel->setText("Press Step or Auto Play to begin.");
    refreshTable();
}

void AlgorithmStepper::stepOnce() {
    if (allProcs.empty()) {
        statusLabel->setText("No processes loaded. Spawn sandbox processes first.");
        return;
    }
    bool anyLeft = false;
    for (auto& p : allProcs) if (p.state != 2) { anyLeft = true; break; }
    if (!anyLeft) {
        statusLabel->setText("✓ All processes complete. Press Reset to run again.");
        autoTimer->stop();
        playBtn->setText("⏵ Auto Play");
        playBtn->setStyleSheet(Theme::btnSuccess());
        return;
    }

    tick++;
    tickLabel->setText(QString::number(tick));

    QString algo = algoBox->currentText();
    if      (algo.startsWith("FCFS"))     stepFCFS();
    else if (algo.startsWith("Round"))    stepRR();
    else if (algo.startsWith("Priority")) stepPriority();
    else if (algo.startsWith("SJF"))      stepSJF();

    // Fire a sched tick into the activity feed for every step
    QString running;
    for (auto& p : allProcs) if (p.state == 1) { running = p.name; break; }
    EventBus::get().schedTick(tick, algo.left(4), running.isEmpty() ? "idle" : running);

    for (auto& p : allProcs)
        if (p.state == 0) p.waitTime++;

    refreshTable();
}

void AlgorithmStepper::stepFCFS() {
    if (readyQueue.empty()) return;
    int idx = readyQueue.front();
    SchedProcess& p = allProcs[idx];
    p.state = 1;
    p.burstLeft--;
    appendGantt(p.name, p.color);

    QString explain = QString(
        "<b>Tick %1 — FCFS</b><br><br>"
        "Running: <b>%2</b> (PID %3)<br>"
        "Burst remaining: %4 ticks<br>"
        "Wait time so far: %5 ticks<br><br>"
        "FCFS picks the process that arrived first and runs it until completion. "
        "No interruptions — once a process starts, it runs to the end."
    ).arg(tick).arg(p.name).arg(p.pid).arg(p.burstLeft).arg(p.waitTime);

    if (p.burstLeft <= 0) {
        p.state = 2;
        readyQueue.pop_front();
        explain += QString("<br><br>✓ <b>%1 finished</b> at tick %2.").arg(p.name).arg(tick);
        kill(p.pid, SIGSTOP);
        OSEvent ev; ev.type = OSEvent::SchedProcessDone; ev.pid = p.pid;
        ev.detail = QString("FCFS: %1 (PID %2) finished at tick %3").arg(p.name).arg(p.pid).arg(tick);
        EventBus::get().fire(ev);
    }
    emit explanationNeeded(explain);
}

void AlgorithmStepper::stepRR() {
    while (!readyQueue.empty() && allProcs[readyQueue.front()].state == 2)
        readyQueue.pop_front();
    if (readyQueue.empty()) return;

    int idx = readyQueue.front();
    SchedProcess& p = allProcs[idx];
    p.state = 1;
    p.burstLeft--;
    appendGantt(p.name, p.color);

    static int rrTick = 0;
    rrTick++;

    QString explain = QString(
        "<b>Tick %1 — Round Robin (quantum=%2)</b><br><br>"
        "Running: <b>%3</b> (PID %4)<br>"
        "Burst remaining: %5  |  Quantum tick: %6/%7<br><br>"
        "Round Robin gives each process a fixed time slice. "
        "After %7 ticks, the next process gets its turn."
    ).arg(tick).arg(quantum).arg(p.name).arg(p.pid)
     .arg(p.burstLeft).arg(rrTick).arg(quantum);

    if (p.burstLeft <= 0) {
        p.state = 2;
        readyQueue.pop_front();
        rrTick = 0;
        kill(p.pid, SIGSTOP);
        explain += QString("<br><br>✓ <b>%1 finished.</b>").arg(p.name);
    } else if (rrTick >= quantum) {
        readyQueue.pop_front();
        readyQueue.push_back(idx);
        p.state = 0;
        rrTick = 0;
        explain += QString("<br><br>⏱ Quantum expired — <b>%1</b> goes to back of queue. "
                           "Next: <b>%2</b>")
                   .arg(p.name)
                   .arg(allProcs[readyQueue.front()].name);
    }
    emit explanationNeeded(explain);
}

void AlgorithmStepper::stepPriority() {
    int best = -1;
    for (int i=0;i<(int)allProcs.size();i++) {
        if (allProcs[i].state == 2) continue;
        if (best == -1 || allProcs[i].priority < allProcs[best].priority)
            best = i;
    }
    if (best == -1) return;

    SchedProcess& p = allProcs[best];
    p.state = 1;
    p.burstLeft--;
    appendGantt(p.name, p.color);

    QString explain = QString(
        "<b>Tick %1 — Priority Scheduling</b><br><br>"
        "Running: <b>%2</b> (PID %3)  nice=%4<br>"
        "Burst remaining: %5<br><br>"
        "Priority picks the process with the lowest nice value each tick. "
        "If a higher-priority process arrives, it preempts immediately."
    ).arg(tick).arg(p.name).arg(p.pid).arg(p.priority).arg(p.burstLeft);

    if (p.burstLeft <= 0) {
        p.state = 2;
        kill(p.pid, SIGSTOP);
        explain += QString("<br><br>✓ <b>%1 finished.</b>").arg(p.name);
    }
    emit explanationNeeded(explain);
}

void AlgorithmStepper::stepSJF() {
    int best = -1;
    for (int i=0;i<(int)allProcs.size();i++) {
        if (allProcs[i].state == 2) continue;
        if (best == -1 || allProcs[i].burstLeft < allProcs[best].burstLeft)
            best = i;
    }
    if (best == -1) return;

    SchedProcess& p = allProcs[best];
    p.state = 1;
    p.burstLeft--;
    appendGantt(p.name, p.color);

    QString explain = QString(
        "<b>Tick %1 — SJF</b><br><br>"
        "Running: <b>%2</b> (PID %3)<br>"
        "Burst remaining: %4 (shortest of all ready processes)<br><br>"
        "SJF always picks the process closest to finishing. "
        "This minimizes average waiting time — "
        "but in reality, you can't know burst time in advance."
    ).arg(tick).arg(p.name).arg(p.pid).arg(p.burstLeft);

    if (p.burstLeft <= 0) {
        p.state = 2;
        kill(p.pid, SIGSTOP);
        explain += QString("<br><br>✓ <b>%1 finished.</b>").arg(p.name);
    }
    emit explanationNeeded(explain);
}

void AlgorithmStepper::appendGantt(const QString& name, const QColor& color) {
    ganttHistory += QString("<span style='color:%1;font-weight:bold;'>[ %2 ]</span> ")
                    .arg(color.name()).arg(name.left(6));
    QStringList parts = ganttHistory.split("</span> ", Qt::SkipEmptyParts);
    if (parts.size() > 18) parts = parts.mid(parts.size() - 18);
    ganttHistory = parts.join("</span> ") + "</span> ";
    ganttLabel->setText(ganttHistory);
}

void AlgorithmStepper::refreshTable() {
    processTable->setRowCount(0);
    for (auto& p : allProcs) {
        int row = processTable->rowCount();
        processTable->insertRow(row);

        auto item = [](const QString& t, const QColor& fg = QColor(Theme::TEXT_PRIMARY)) {
            auto* i = new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            i->setForeground(fg);
            return i;
        };

        QString stateStr = p.state==0 ? "◌ Ready" : p.state==1 ? "▶ Running" : "✓ Done";
        QColor  stateCol = p.state==0 ? QColor(Theme::BLUE) :
                           p.state==1 ? QColor(Theme::GREEN) : QColor(Theme::TEXT_MUTED);

        processTable->setItem(row, 0, item(QString::number(p.pid)));
        processTable->setItem(row, 1, item(p.name, p.color));
        processTable->setItem(row, 2, item(QString::number(p.priority)));
        processTable->setItem(row, 3, item(QString::number(std::max(0, p.burstLeft))));
        processTable->setItem(row, 4, item(stateStr, stateCol));
    }
}

void AlgorithmStepper::toggleAutoPlay() {
    if (autoTimer->isActive()) {
        autoTimer->stop();
        playBtn->setText("⏵ Auto Play");
        playBtn->setStyleSheet(Theme::btnSuccess());
    } else {
        autoTimer->start(2000 - speedSlider->value() + 100);
        playBtn->setText("⏸ Pause");
        playBtn->setStyleSheet(Theme::btnWarning());
    }
}

void AlgorithmStepper::onAlgoChanged(const QString& algo) {
    resetScheduler();
    OSEvent ev; ev.type = OSEvent::SchedAlgoChanged;
    ev.detail = QString("Scheduler: %1").arg(algo.left(30));
    EventBus::get().fire(ev);
}

void AlgorithmStepper::onSpeedChanged(int val) {
    if (autoTimer->isActive())
        autoTimer->setInterval(2000 - val + 100);
}
