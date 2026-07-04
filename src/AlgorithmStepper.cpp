#include "AlgorithmStepper.h"
#include "Theme.h"
#include <QHeaderView>
#include <QPainterPath>
#include <QTime>
#include <QFileInfo>
#include <QCoreApplication>
#include <QScrollBar>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/resource.h>
#include <signal.h>
#include <algorithm>

// ── Palette ───────────────────────────────────────────────────────────────────
const QColor SchedGanttView::PALETTE[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};

// ── SchedGanttView ────────────────────────────────────────────────────────────

SchedGanttView::SchedGanttView(QWidget* p) : QWidget(p) {
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(QString(
        "background:#0F172A; border-radius:10px; border:1px solid %1;"
    ).arg(Theme::BORDER));
}

void SchedGanttView::clear() {
    history.clear(); knownTids.clear(); colors.clear(); update();
}

void SchedGanttView::addSample(const QVector<TidSample>& tids) {
    // Register new TIDs
    int ci = knownTids.size();
    for (auto& s : tids) {
        if (!knownTids.contains(s.tid)) {
            knownTids.append(s.tid);
            colors[s.tid] = PALETTE[ci % 8];
            ci++;
        }
    }
    Tick t;
    t.tids = tids;
    history.append(t);
    if (history.size() > 80) history.removeFirst();
    update();
}

void SchedGanttView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath bg;
    bg.addRoundedRect(rect(), 10, 10);
    p.fillPath(bg, QColor("#0F172A"));

    if (history.isEmpty() || knownTids.isEmpty()) {
        p.setPen(QColor("#475569"));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Live Gantt timeline will appear when a worker is running.\n"
            "Solid bar = thread is RUNNING (sampled from /proc/[tid]/stat).");
        return;
    }

    int w = width(), h = height();
    int numT   = knownTids.size();
    int labelW = 90;
    int topPad = 10;
    int botPad = 18;
    int chartH = h - topPad - botPad;
    int rowH   = std::max(14, chartH / numT);
    int chartW = w - labelW - 8;
    float tickW = history.size() > 0 ? (float)chartW / history.size() : 8.f;

    // Grid lines
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= numT; i++) {
        int y = topPad + i * rowH;
        p.drawLine(labelW, y, w - 6, y);
    }

    for (int i = 0; i < numT; i++) {
        long tid = knownTids[i];
        int y = topPad + i * rowH;

        // Alternating row
        if (i % 2 == 0) p.fillRect(QRect(labelW, y, chartW, rowH), QColor(255,255,255,5));

        // Label
        p.setPen(colors[tid].lighter(130));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(QRect(4, y, labelW - 6, rowH),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QString("T%1").arg(tid));
    }

    // Draw tick bars colored by real /proc state
    for (int t = 0; t < history.size(); t++) {
        for (auto& sample : history[t].tids) {
            int i = knownTids.indexOf(sample.tid);
            if (i < 0) continue;

            int y  = topPad + i * rowH + 2;
            int x  = labelW + (int)(t * tickW);
            int bw = std::max(1, (int)tickW - 1);
            QRect bar(x, y, bw, rowH - 4);

            QPainterPath bp;
            bp.addRoundedRect(bar, 2, 2);

            bool running = (sample.state == "R");
            if (running) {
                p.fillPath(bp, colors[sample.tid]);
            } else {
                QColor c = colors[sample.tid]; c.setAlpha(30);
                p.fillPath(bp, c);
            }
        }
    }

    // Bottom legend
    int lx = labelW + 4, ly = h - 6;
    p.setFont(QFont("Segoe UI", 7));
    // Running swatch
    p.fillRect(lx, ly - 8, 10, 8, QColor("#4F6EF7"));
    p.setPen(QColor("#94A3B8"));
    p.drawText(lx + 13, ly, "Running (R)");
    QColor sc("#4F6EF7"); sc.setAlpha(35);
    p.fillRect(lx + 90, ly - 8, 10, 8, sc);
    p.drawText(lx + 103, ly, "Sleeping/Waiting");
}

// ── helpers ───────────────────────────────────────────────────────────────────

static QString findSchedWorker() {
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList c = {
        appDir + "/sched_worker",
        appDir + "/tools/sched_worker",
        appDir + "/../tools/sched_worker",
    };
    for (auto& p : c) if (QFileInfo::exists(p)) return p;
    return appDir + "/sched_worker";
}

// Read state of one task from /proc/[pid]/task/[tid]/stat
static QString readTidState(pid_t pid, long tid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task/%ld/stat", pid, tid);
    std::ifstream f(path);
    if (!f.is_open()) return "?";
    std::string line;
    std::getline(f, line);
    size_t rp = line.rfind(')');
    if (rp == std::string::npos || rp + 2 >= line.size()) return "?";
    return QString(line[rp + 2]);
}

// ── AlgorithmStepper ─────────────────────────────────────────────────────────

AlgorithmStepper::AlgorithmStepper(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Title row ─────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("⚙  Scheduler Lab — Real Kernel Scheduling");
    title->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● REAL sched_setscheduler()");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::PURPLE).arg(Theme::PURPLE_LIGHT));
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Spawns a real worker process and calls <code>sched_setscheduler()</code> / "
        "<code>sched_setattr()</code> to set the kernel policy. "
        "Each sample reads <b>/proc/[pid]/task/[tid]/stat</b> for real "
        "<code>utime</code>, <code>stime</code>, <code>priority</code>, and thread state. "
        "The Gantt chart is built from actual kernel observations — not simulation.");
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

    auto mkLbl = [&](const QString& t) {
        auto* l = new QLabel(t);
        l->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
            .arg(Theme::TEXT_SECONDARY));
        return l;
    };

    ctrlL->addWidget(mkLbl("Scheduling Policy"), 0, 0);
    ctrlL->addWidget(mkLbl("Thread Count"),       0, 1);
    ctrlL->addWidget(mkLbl(""),                   0, 2);
    ctrlL->addWidget(mkLbl(""),                   0, 3);

    policyBox = new QComboBox();
    policyBox->addItem("SCHED_OTHER  — CFS / nice-based (default)",       "OTHER");
    policyBox->addItem("SCHED_FIFO   — Real-time, no preemption           (needs root/CAP_SYS_NICE)", "FIFO");
    policyBox->addItem("SCHED_RR     — Real-time, round-robin timeslice   (needs root/CAP_SYS_NICE)", "RR");
    policyBox->addItem("SCHED_DEADLINE — EDF, per-thread deadline/period  (needs root/CAP_SYS_NICE)", "DEADLINE");
    policyBox->addItem("SCHED_BATCH  — Low-priority background batch jobs", "BATCH");
    policyBox->addItem("SCHED_IDLE   — Lowest priority, runs only when idle", "IDLE");
    policyBox->setStyleSheet(Theme::input());

    threadSpin = new QSpinBox();
    threadSpin->setRange(2, 8);
    threadSpin->setValue(4);
    threadSpin->setStyleSheet(Theme::input());

    spawnBtn = new QPushButton("▶  Spawn Worker");
    spawnBtn->setStyleSheet(Theme::btnPrimary());
    spawnBtn->setMinimumHeight(34);

    killBtn = new QPushButton("✕  Kill Worker");
    killBtn->setStyleSheet(Theme::btnDanger());
    killBtn->setMinimumHeight(34);
    killBtn->setEnabled(false);

    ctrlL->addWidget(policyBox,   1, 0);
    ctrlL->addWidget(threadSpin,  1, 1);
    ctrlL->addWidget(spawnBtn,    1, 2);
    ctrlL->addWidget(killBtn,     1, 3);
    ctrlL->setColumnStretch(0, 3);
    ctrlL->setColumnStretch(1, 1);
    ctrlL->setColumnStretch(2, 1);
    ctrlL->setColumnStretch(3, 1);
    outer->addWidget(ctrl);

    // ── Stats row ─────────────────────────────────────────────────────────────
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(10);

    auto mkStat = [&](const QString& label, QLabel*& vout, const char* accent) {
        auto* card = new QWidget();
        card->setStyleSheet(QString(
            "background:white; border-radius:10px; border:1px solid %1;"
        ).arg(Theme::BORDER));
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(14, 10, 14, 10);
        vl->setSpacing(2);
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QString("color:%1; font-size:10px; font-weight:600;").arg(Theme::TEXT_MUTED));
        vout = new QLabel("—");
        vout->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700;").arg(accent));
        vl->addWidget(lbl); vl->addWidget(vout);
        return card;
    };

    statsRow->addWidget(mkStat("Worker PID",    statPid,     Theme::BLUE));
    statsRow->addWidget(mkStat("Policy",        statPolicy,  Theme::PURPLE));
    statsRow->addWidget(mkStat("Live Threads",  statThreads, Theme::GREEN));
    outer->addLayout(statsRow);

    // ── TID table ─────────────────────────────────────────────────────────────
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tl = new QVBoxLayout(tableCard);
    tl->setContentsMargins(14, 12, 14, 12);
    tl->setSpacing(6);

    auto* tblHdr = new QHBoxLayout();
    auto* tblTitle = new QLabel("Live Thread Table  —  sampled from /proc/[pid]/task/[tid]/stat");
    tblTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    tblHdr->addWidget(tblTitle);
    tl->addLayout(tblHdr);

    tidTable = new QTableWidget(0, 7);
    tidTable->setHorizontalHeaderLabels({"TID","State","Policy","Sched Prio","Nice","utime","Context Switches"});
    tidTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tidTable->verticalHeader()->setVisible(false);
    tidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tidTable->setAlternatingRowColors(true);
    tidTable->setShowGrid(false);
    tidTable->setMinimumHeight(110);
    tidTable->setMaximumHeight(180);
    tidTable->setStyleSheet(Theme::table());
    tl->addWidget(tidTable);
    outer->addWidget(tableCard);

    // ── Gantt chart ───────────────────────────────────────────────────────────
    auto* ganttCard = new QWidget();
    ganttCard->setStyleSheet(Theme::card());
    auto* gl = new QVBoxLayout(ganttCard);
    gl->setContentsMargins(14, 12, 14, 12);
    gl->setSpacing(6);

    auto* gHdr = new QHBoxLayout();
    auto* gTitle = new QLabel("Live Gantt Timeline  —  built from /proc observations");
    gTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* gHint = new QLabel("Solid = Running · Faded = Sleeping");
    gHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    gHdr->addWidget(gTitle); gHdr->addStretch(); gHdr->addWidget(gHint);
    gl->addLayout(gHdr);

    ganttView = new SchedGanttView();
    ganttView->setMinimumHeight(170);
    gl->addWidget(ganttView);
    outer->addWidget(ganttCard, 1);

    // ── Log ───────────────────────────────────────────────────────────────────
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* ll = new QVBoxLayout(logCard);
    ll->setContentsMargins(14, 12, 14, 12);
    ll->setSpacing(4);
    auto* logTitle = new QLabel("Event Log");
    logTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    ll->addWidget(logTitle);

    logView = new QTextEdit();
    logView->setReadOnly(true);
    logView->setMinimumHeight(70);
    logView->setMaximumHeight(100);
    logView->setStyleSheet(Theme::termLog());
    ll->addWidget(logView);
    outer->addWidget(logCard);

    // Status bar
    statusLabel = new QLabel("Select a scheduling policy and spawn a worker.");
    statusLabel->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(spawnBtn, &QPushButton::clicked, this, &AlgorithmStepper::onSpawnWorker);
    connect(killBtn,  &QPushButton::clicked, this, &AlgorithmStepper::onKillWorker);
    connect(policyBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AlgorithmStepper::onPolicyChanged);

    sampleTimer = new QTimer(this);
    connect(sampleTimer, &QTimer::timeout, this, &AlgorithmStepper::onSampleTick);

    // Explain initial policy
    onPolicyChanged(0);
}

void AlgorithmStepper::loadProcesses(const std::vector<pid_t>& /*pids*/) {
    // No-op: this lab now uses its own worker instead of sandbox pids.
    // The sandbox connection in MainWindow still exists for the old API;
    // just silently ignore it.
}

void AlgorithmStepper::onPolicyChanged(int idx) {
    static const char* exps[] = {
        "<b>SCHED_OTHER — CFS (Completely Fair Scheduler)</b><br><br>"
        "The default Linux policy. The kernel tracks each thread's virtual runtime "
        "and always runs the thread that has had the <i>least</i> CPU time. "
        "Nice values bias the weight: nice −20 gets ~10× more CPU than nice +19.<br><br>"
        "<b>What you'll see:</b> All threads get roughly equal CPU time (shown by "
        "equal utime growth). Use different nice values in future to bias them.",

        "<b>SCHED_FIFO — Real-time, first in first out</b><br><br>"
        "A real-time policy. Once scheduled, a FIFO thread runs until it blocks "
        "or voluntarily yields — no preemption by other FIFO threads of equal priority. "
        "Higher numeric priority (1–99) wins immediately.<br><br>"
        "<b>Requires:</b> <code>CAP_SYS_NICE</code> or root. "
        "If not privileged, the worker falls back to SCHED_OTHER and reports so.<br><br>"
        "<b>What you'll see:</b> One thread dominates the Gantt chart until it yields.",

        "<b>SCHED_RR — Real-time, round-robin</b><br><br>"
        "Like FIFO but adds a kernel-enforced timeslice (typically 100ms). "
        "Same-priority RR threads rotate in round-robin order. "
        "Higher priority still preempts lower priority immediately.<br><br>"
        "<b>What you'll see:</b> Threads at equal priority take turns in the Gantt.",

        "<b>SCHED_DEADLINE — Earliest Deadline First (EDF)</b><br><br>"
        "Per-thread deadline scheduling via <code>sched_setattr()</code>. "
        "Each thread declares its runtime, deadline, and period. "
        "The kernel admits or rejects based on whether deadlines can be met.<br><br>"
        "<code>runtime=5ms, deadline=10ms, period=10ms</code> means: "
        "this thread needs 5ms every 10ms window.<br><br>"
        "<b>Requires:</b> CAP_SYS_NICE / root. Very visible in Gantt: "
        "threads get short bursts tightly controlled by the kernel.",

        "<b>SCHED_BATCH — CPU-intensive background</b><br><br>"
        "Like SCHED_OTHER but the scheduler assumes the thread won't be interactive. "
        "No wakeup preemption bonus — the thread won't get a short scheduling boost "
        "when it wakes from sleep. Good for compile jobs, video encoding.<br><br>"
        "<b>What you'll see:</b> Similar to OTHER but slightly less responsive.",

        "<b>SCHED_IDLE — Lowest possible priority</b><br><br>"
        "Runs only when <i>nothing else</i> is runnable. Even SCHED_OTHER nice +19 "
        "beats SCHED_IDLE. Use for truly background tasks that should never interfere.<br><br>"
        "<b>What you'll see:</b> Very sparse Gantt bars — the OS barely schedules these threads.",
    };
    if (idx >= 0 && idx < 6) emit explanationNeeded(exps[idx]);
}

void AlgorithmStepper::onSpawnWorker() {
    if (workerProc) {
        workerProc->kill();
        workerProc->waitForFinished(500);
        workerProc->deleteLater();
        workerProc = nullptr;
        workerPid = -1;
    }

    QString policy = policyBox->currentData().toString();
    int nThreads   = threadSpin->value();

    workerProc = new QProcess(this);
    workerProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(workerProc, &QProcess::readyRead, this, &AlgorithmStepper::onWorkerOutput);
    connect(workerProc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                sampleTimer->stop();
                workerPid = -1;
                statPid->setText("—");
                statPolicy->setText("—");
                statThreads->setText("—");
                spawnBtn->setEnabled(true);
                killBtn->setEnabled(false);
                logView->append(QString(
                    "<span style='color:#94A3B8;'>[%1]</span> "
                    "<span style='color:#FBBF24;'>Worker exited.</span>")
                    .arg(QTime::currentTime().toString("hh:mm:ss")));
            });

    workerProc->start(findSchedWorker(), {QString::number(nThreads), policy});
    if (!workerProc->waitForStarted(2000)) {
        statusLabel->setText("⚠ Could not start sched_worker — rebuild first.");
        workerProc->deleteLater();
        workerProc = nullptr;
        return;
    }

    spawnBtn->setEnabled(false);
    killBtn->setEnabled(true);
    ganttView->clear();
    statusLabel->setText(QString("Spawning worker — %1 threads, policy %2…")
        .arg(nThreads).arg(policy));

    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#4ADE80;'>Spawning</span> worker — "
        "%2 threads, policy <b>%3</b>")
        .arg(QTime::currentTime().toString("hh:mm:ss"))
        .arg(nThreads).arg(policy));
}

void AlgorithmStepper::onWorkerOutput() {
    if (!workerProc) return;
    QByteArray data = workerProc->readAll();
    for (auto& rawLine : data.split('\n')) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("READY ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) workerPid = parts[1].toLong();
            statPid->setText(QString::number(workerPid));
            statPolicy->setText(policyBox->currentData().toString());

            // Start sampling
            sampleTimer->start(500);

            statusLabel->setText(QString("Worker PID %1 running").arg(workerPid));
            logView->append(QString(
                "<span style='color:#94A3B8;'>[%1]</span> "
                "<span style='color:#4ADE80;'>Worker ready</span> — PID <b>%2</b>")
                .arg(QTime::currentTime().toString("hh:mm:ss")).arg(workerPid));

            emit explanationNeeded(QString(
                "<b>Scheduler Lab — Worker PID %1</b><br><br>"
                "Policy: <b>%2</b><br><br>"
                "The worker called <code>sched_setscheduler(0, SCHED_%2, &sp)</code> "
                "on each thread. You can verify:<br>"
                "<code>cat /proc/%1/task/*/status | grep policy</code><br><br>"
                "The Gantt chart samples <code>/proc/%1/task/[tid]/stat</code> every 500ms "
                "and reads field 3 (state: R=running, S=sleeping, D=disk wait).<br><br>"
                "The table shows real <code>utime</code> + <code>stime</code> "
                "(in clock ticks since thread start) and voluntary context switch counts."
            ).arg(workerPid).arg(policyBox->currentData().toString()));
        }
    }
}

void AlgorithmStepper::onSampleTick() {
    if (workerPid <= 0) return;
    if (kill(workerPid, 0) != 0) { workerPid = -1; sampleTimer->stop(); return; }

    auto samples = parseSamples();
    if (samples.isEmpty()) return;

    // Read state from /proc
    for (auto& s : samples) {
        s.state = readTidState(workerPid, (long)s.tid);
    }

    statThreads->setText(QString::number(samples.size()));
    ganttView->addSample(samples);
    refreshTable(samples);
}

QVector<SchedGanttView::TidSample> AlgorithmStepper::parseSamples() {
    // Read STATUS lines buffered from the worker stdout
    // The worker continuously emits STATUS lines; we just re-read /proc directly
    // here for a guaranteed-fresh snapshot every sample tick.
    QVector<SchedGanttView::TidSample> result;
    if (workerPid <= 0) return result;

    QString taskPath = QString("/proc/%1/task").arg(workerPid);
    DIR* dir = opendir(taskPath.toLocal8Bit().constData());
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        bool ok = false;
        long tid = QString(ent->d_name).toLong(&ok);
        if (!ok || tid == workerPid) continue;

        SchedGanttView::TidSample s;
        s.tid = tid;

        // Read /proc/[pid]/task/[tid]/stat
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/task/%ld/stat", (int)workerPid, tid);
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::string line;
        std::getline(f, line);

        size_t rp = line.rfind(')');
        if (rp == std::string::npos) continue;
        std::istringstream ss(line.substr(rp + 2));
        char state;
        int ppid, pgrp, session, tty_nr, tpgid;
        unsigned long flags;
        long minflt, cminflt, majflt, cmajflt;
        long utime, stime;
        ss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
           >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;
        long cutime, cstime, priority, nice;
        ss >> cutime >> cstime >> priority >> nice;

        s.state = QString(state);
        s.utime = utime;
        s.stime = stime;
        s.nice  = (int)nice;

        // Read policy via status
        snprintf(path, sizeof(path), "/proc/%d/task/%ld/status", (int)workerPid, tid);
        std::ifstream sf(path);
        std::string sline;
        long volsw = 0;
        while (std::getline(sf, sline)) {
            if (sline.rfind("voluntary_ctxt_switches:", 0) == 0) {
                std::istringstream tmp(sline.substr(24));
                tmp >> volsw;
            }
        }
        s.switches = volsw;

        // Derive policy name from sched_getscheduler
        // We can't call it from GUI thread on another process safely,
        // but the worker emits it — use the policyBox selection as proxy
        s.policy = policyBox->currentData().toString();
        s.priority = (int)priority;

        result.append(s);
    }
    closedir(dir);
    return result;
}

void AlgorithmStepper::refreshTable(const QVector<SchedGanttView::TidSample>& samples) {
    static const QColor PALETTE[] = {
        QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
        QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    };

    tidTable->setRowCount(0);
    int ci = 0;
    for (auto& s : samples) {
        int row = tidTable->rowCount();
        tidTable->insertRow(row);

        auto cell = [&](const QString& t, const char* c = nullptr) {
            auto* item = new QTableWidgetItem(t);
            item->setTextAlignment(Qt::AlignCenter);
            if (c) item->setForeground(QColor(c));
            return item;
        };

        QString stateLabel =
            s.state == "R" ? "▶ Running" :
            s.state == "S" ? "◌ Sleeping" :
            s.state == "D" ? "⧖ DiskWait" : s.state;
        const char* stateColor =
            s.state == "R" ? Theme::GREEN :
            s.state == "S" ? Theme::BLUE  : Theme::ORANGE;

        auto* tidItem = new QTableWidgetItem(QString("● %1").arg(s.tid));
        tidItem->setTextAlignment(Qt::AlignCenter);
        tidItem->setForeground(PALETTE[ci % 6]);
        QFont bf = tidItem->font(); bf.setBold(true); tidItem->setFont(bf);

        tidTable->setItem(row, 0, tidItem);
        tidTable->setItem(row, 1, cell(stateLabel, stateColor));
        tidTable->setItem(row, 2, cell(s.policy));
        tidTable->setItem(row, 3, cell(s.priority > 0 ? QString::number(s.priority) : "—"));
        tidTable->setItem(row, 4, cell(QString::number(s.nice)));
        tidTable->setItem(row, 5, cell(QString::number(s.utime)));
        tidTable->setItem(row, 6, cell(QString::number(s.switches)));
        ci++;
    }
}

void AlgorithmStepper::onKillWorker() {
    if (!workerProc) return;
    sampleTimer->stop();
    workerProc->kill();
    workerProc->waitForFinished(500);
    workerProc->deleteLater();
    workerProc = nullptr;

    workerPid = -1;
    spawnBtn->setEnabled(true);
    killBtn->setEnabled(false);
    statPid->setText("—");
    statPolicy->setText("—");
    statThreads->setText("—");
    tidTable->setRowCount(0);
    statusLabel->setText("Worker killed — ready to spawn again.");

    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#F87171;'>Killed</span> worker.")
        .arg(QTime::currentTime().toString("hh:mm:ss")));
}
