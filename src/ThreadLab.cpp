#include "ThreadLab.h"
#include "CleanupRegistry.h"
#include "EventBus.h"
#include "Theme.h"
#include <QHeaderView>
#include <QPainterPath>
#include <QTime>
#include <QScrollBar>
#include <QCoreApplication>
#include <QFileInfo>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <dirent.h>
#include <algorithm>

// ── Palette (single definition — ThreadTimeline::PALETTE is the canonical one) ──
const QColor ThreadTimeline::PALETTE[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};

// ── ThreadTimeline ────────────────────────────────────────────────────────────

ThreadTimeline::ThreadTimeline(QWidget* p) : QWidget(p) {
    setMinimumHeight(130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Match the page background so the dark canvas reads as a deliberate
    // inset panel rather than a colour clash.
    setStyleSheet(QString(
        "background: #1E293B; border-radius: 10px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
}

void ThreadTimeline::clear() { history.clear(); tids.clear(); colors.clear(); update(); }

void ThreadTimeline::addTick(const QVector<ThreadInfo>& threads) {
    Tick t;
    int ci = 0;
    for (auto& th : threads) {
        if (!tids.contains(th.tid)) {
            tids.append(th.tid);
            colors[th.tid] = PALETTE[ci % 8];
        }
        ci++;
        t.states.append({th.tid, th.state});
    }
    history.append(t);
    if (history.size() > 80) history.removeFirst();
    update();
}

void ThreadTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath bg;
    bg.addRoundedRect(rect(), 10, 10);
    p.fillPath(bg, QColor("#1E293B"));

    if (history.isEmpty()) {
        p.setPen(QColor("#64748B"));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Thread timeline will appear when a worker is running");
        return;
    }

    int w = width(), h = height();
    int numT = tids.size();
    if (numT == 0) return;

    const int labelW = 78;
    const int topPad = 8;
    const int botPad = 22;   // enough room for the legend inside bounds
    int chartH  = h - topPad - botPad;
    int rowH    = std::max(14, chartH / numT);
    int chartW  = w - labelW - 12;
    float tickW = history.size() > 0 ? (float)chartW / history.size() : 8.f;

    // Grid lines
    p.setPen(QPen(QColor("#334155"), 1));
    for (int i = 0; i <= numT; i++) {
        int y = topPad + i * rowH;
        p.drawLine(labelW, y, w - 6, y);
    }

    for (int i = 0; i < numT; i++) {
        int y = topPad + i * rowH;
        if (i % 2 == 0) p.fillRect(QRect(labelW, y, chartW, rowH), QColor(255,255,255,8));

        QColor lc = colors.value(tids[i]);
        p.setPen(lc.lighter(140));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(QRect(4, y, labelW - 6, rowH), Qt::AlignVCenter | Qt::AlignRight,
            QString("TID %1").arg(tids[i]));
    }

    for (int t = 0; t < history.size(); t++) {
        for (auto& st : history[t].states) {
            int i = tids.indexOf(st.first);
            if (i < 0) continue;
            int y  = topPad + i * rowH + 3;
            int x  = labelW + (int)(t * tickW);
            int bw = std::max(1, (int)tickW - 1);
            QRect bar(x, y, bw, rowH - 6);
            bool running = (st.second == "R");

            QPainterPath bp;
            bp.addRoundedRect(bar, 2, 2);
            if (running) {
                p.fillPath(bp, colors.value(st.first));
            } else {
                QColor c = colors.value(st.first); c.setAlpha(40);
                p.fillPath(bp, c);
            }
        }
    }

    // Legend — drawn inside the bottom padding area, never past the widget edge
    const int legendY = h - botPad + 6;   // baseline inside botPad strip
    p.setFont(QFont("Segoe UI", 8));
    QColor runC("#4F6EF7");
    p.fillRect(labelW, legendY - 8, 10, 8, runC);
    p.setPen(QColor("#94A3B8"));
    p.drawText(labelW + 14, legendY, "Running");

    QColor sleepC("#4F6EF7"); sleepC.setAlpha(40);
    p.fillRect(labelW + 76, legendY - 8, 10, 8, sleepC);
    p.drawText(labelW + 90, legendY, "Sleeping (futex)");
}

// ── LockGraphView ─────────────────────────────────────────────────────────────

LockGraphView::LockGraphView(QWidget* p) : QWidget(p) {
    setMinimumHeight(160);
    setStyleSheet(QString(
        "background:white; border-radius:10px; border:1px solid %1;"
    ).arg(Theme::BORDER));
}

void LockGraphView::clear() {
    threads.clear(); deadlockDetected = false; deadTidA = -1; deadTidB = -1; update();
}

void LockGraphView::setThreads(const QVector<ThreadInfo>& t) {
    threads = t; update();
}

void LockGraphView::setDeadlockDetected(long a, long b) {
    deadlockDetected = true; deadTidA = a; deadTidB = b; update();
}

void LockGraphView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (threads.isEmpty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Lock graph appears when Deadlock demo is running.\n"
            "Thread A: lock A → try B.   Thread B: lock B → try A.\n"
            "ABBA inversion → neither can proceed.");
        return;
    }

    int w = width(), h = height();
    int boxW = 100, boxH = 40, pad = 20;

    // Draw two mutex boxes in the centre
    int midX = w / 2;
    int mutexY = h / 2 - boxH / 2;
    int mutexSpacing = 140;
    QRect mutA(midX - mutexSpacing - boxW / 2, mutexY, boxW, boxH);
    QRect mutB(midX + mutexSpacing - boxW / 2, mutexY, boxW, boxH);

    auto drawMutex = [&](QRect r, const QString& name, QColor bg, QColor border) {
        QPainterPath path; path.addRoundedRect(r, 8, 8);
        p.fillPath(path, bg);
        p.setPen(QPen(border, 2));
        p.drawPath(path);
        p.setPen(border);
        p.setFont(QFont("Consolas", 10, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, name);
    };

    drawMutex(mutA, "mutex_A", QColor("#EEF2FF"), QColor(Theme::BLUE));
    drawMutex(mutB, "mutex_B", QColor("#FAF5FF"), QColor(Theme::PURPLE));

    // Collect threads with lock involvement
    QVector<ThreadInfo> relevant;
    for (auto& t : threads) {
        if (t.holdsLock != "none" || t.waitsForLock != "none")
            relevant.append(t);
    }
    if (relevant.isEmpty()) {
        for (auto& t : threads)
            relevant.append(t);
    }

    int tidY_top = 30;
    for (int i = 0; i < std::min((int)relevant.size(), 4); i++) {
        auto& th = relevant[i];
        int tidY = tidY_top + i * (boxH + 10);
        int isLeft = (i % 2 == 0);
        int tidX = isLeft ? pad : w - pad - boxW;
        QRect tidBox(tidX, tidY, boxW, boxH);

        bool isDeadlocked = (th.tid == deadTidA || th.tid == deadTidB) && deadlockDetected;
        QColor bg = isDeadlocked ? QColor("#FEF2F2") : QColor(Theme::BG_INPUT);
        QColor border = isDeadlocked ? QColor(Theme::RED) : QColor(Theme::BORDER);
        QPainterPath tp; tp.addRoundedRect(tidBox, 6, 6);
        p.fillPath(tp, bg);
        p.setPen(QPen(border, isDeadlocked ? 2.5 : 1));
        p.drawPath(tp);
        p.setPen(isDeadlocked ? QColor(Theme::RED) : QColor(Theme::TEXT_PRIMARY));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(tidBox, Qt::AlignCenter,
            QString("TID %1\n%2").arg(th.tid)
                .arg(isDeadlocked ? "DEADLOCKED" : th.state));

        // "holds" arrow (solid green)
        QRect* holdTarget = nullptr;
        if (th.holdsLock == "A") holdTarget = &mutA;
        if (th.holdsLock == "B") holdTarget = &mutB;
        if (holdTarget) {
            QPoint from(isLeft ? tidBox.right() : tidBox.left(), tidBox.center().y());
            QPoint to(holdTarget->center().x(), holdTarget->center().y());
            p.setPen(QPen(QColor(Theme::GREEN), 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(from, to);
            QPoint mid((from.x() + to.x()) / 2, (from.y() + to.y()) / 2 - 8);
            p.setFont(QFont("Segoe UI", 7, QFont::Bold));
            p.setPen(QColor(Theme::GREEN));
            p.drawText(mid, "holds");
        }

        // "waits for" arrow (dashed orange)
        QRect* waitTarget = nullptr;
        if (th.waitsForLock == "A") waitTarget = &mutA;
        if (th.waitsForLock == "B") waitTarget = &mutB;
        if (waitTarget) {
            QPoint from(isLeft ? tidBox.right() : tidBox.left(), tidBox.center().y() + 8);
            QPoint to(waitTarget->center().x(), waitTarget->center().y() + 8);
            p.setPen(QPen(QColor(Theme::ORANGE), 2, Qt::DashLine, Qt::RoundCap));
            p.drawLine(from, to);
            QPoint mid((from.x() + to.x()) / 2, (from.y() + to.y()) / 2 - 8);
            p.setFont(QFont("Segoe UI", 7, QFont::Bold));
            p.setPen(QColor(Theme::ORANGE));
            p.drawText(mid, "waits for");
        }
    }

    // Deadlock banner
    if (deadlockDetected) {
        QRect banner(pad, h - 30, w - pad * 2, 22);
        QPainterPath bp; bp.addRoundedRect(banner, 6, 6);
        p.fillPath(bp, QColor("#FEF2F2"));
        p.setPen(QColor(Theme::RED));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(banner, Qt::AlignCenter,
            QString("⚠ DEADLOCK DETECTED: TID %1 ↔ TID %2 (neither can proceed)")
                .arg(deadTidA).arg(deadTidB));
    }
}

// ── helpers ───────────────────────────────────────────────────────────────────

static QString findThreadWorker() {
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/learnos_thread_worker",
        appDir + "/tools/learnos_thread_worker",
        appDir + "/../tools/learnos_thread_worker",
    };
    for (auto& c : candidates)
        if (QFileInfo::exists(c)) return c;
    return appDir + "/learnos_thread_worker";
}

// ── ThreadLab ─────────────────────────────────────────────────────────────────

ThreadLab::ThreadLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    // ── Title row ─────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🧵  Thread Lab — POSIX Threads, Synchronization & Deadlock");
    title->setStyleSheet(QString("color:%1; font-size:14px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● REAL THREADS + /proc FUTEX STATE");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::PURPLE).arg(Theme::PURPLE_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* banner = new QLabel(
        "Spawns a real worker process with multiple POSIX threads. Thread state is "
        "read from <b>/proc/[pid]/task/[tid]/status</b> — when a thread blocks on a "
        "mutex or condvar the kernel puts it in state <b>S</b> (sleeping in futex_wait). "
        "The <b>Deadlock</b> demo triggers a real ABBA lock inversion and "
        "shows the live lock graph. Toggle <b>With Lock</b> on/off to see a real data race.");
    banner->setWordWrap(true);
    banner->setStyleSheet(QString(
        "background:%1; color:%2; border:1px solid %3;"
        "border-radius:10px; padding:10px 14px; font-size:11px;"
    ).arg(Theme::BLUE_LIGHT, Theme::TEXT_PRIMARY, Theme::BORDER));
    outer->addWidget(banner);

    // ── Controls card — demo selector on top row, count + buttons on bottom ──
    auto* ctrl = new QWidget();
    ctrl->setStyleSheet(Theme::card());
    auto* cl = new QVBoxLayout(ctrl);
    cl->setContentsMargins(16, 12, 16, 12);
    cl->setSpacing(8);

    auto mkLabel = [&](const QString& txt) -> QLabel* {
        auto* l = new QLabel(txt);
        l->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;").arg(Theme::TEXT_SECONDARY));
        return l;
    };

    // Row 1: demo selector (full width)
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(10);
    row1->addWidget(mkLabel("Demo scenario:"));
    demoBox = new QComboBox();
    demoBox->addItem("CPU Race — all threads burn CPU in parallel");
    demoBox->addItem("Mutex Contention — threads fight for a single lock");
    demoBox->addItem("Producer-Consumer — cooperate via condvar");
    demoBox->addItem("Reader-Writer — shared data, rwlock");
    demoBox->addItem("Race Condition — unsynchronized increment (corrupt output)");
    demoBox->addItem("Deadlock — ABBA lock inversion (real pthread deadlock)");
    demoBox->setStyleSheet(Theme::input());
    row1->addWidget(demoBox, 1);
    cl->addLayout(row1);

    // Row 2: thread count, lock toggle, spawn/kill
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(10);

    row2->addWidget(mkLabel("Threads:"));
    threadCountSpin = new QSpinBox();
    threadCountSpin->setRange(2, 8);
    threadCountSpin->setValue(4);
    threadCountSpin->setStyleSheet(Theme::input());
    threadCountSpin->setFixedWidth(90);
    row2->addWidget(threadCountSpin);

    lockToggle = new QCheckBox("With Lock");
    lockToggle->setChecked(true);
    lockToggle->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_PRIMARY));
    lockToggle->setToolTip(
        "Checked = threads increment with pthread_mutex (correct result).\n"
        "Unchecked = no lock — real data race (counter wrong).");
    row2->addWidget(lockToggle);

    row2->addStretch();

    spawnBtn = new QPushButton("▶  Spawn Worker");
    spawnBtn->setStyleSheet(Theme::btnPrimary());
    spawnBtn->setMinimumHeight(34);
    row2->addWidget(spawnBtn);

    killBtn = new QPushButton("✕  Kill Worker");
    killBtn->setStyleSheet(Theme::btnDanger());
    killBtn->setMinimumHeight(34);
    killBtn->setEnabled(false);
    row2->addWidget(killBtn);

    cl->addLayout(row2);
    outer->addWidget(ctrl);

    // ── Stats row — four KPI cards with accent left border ─────────────────
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(8);

    auto mkStatCard = [&](const QString& label, QLabel*& valueOut,
                          const char* accent, const char* bgColor) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet(QString(
            "background: %1; border-radius: 10px;"
            "border: 1px solid %2; border-left: 4px solid %3;"
        ).arg(bgColor).arg(Theme::BORDER).arg(accent));
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(14, 10, 14, 10);
        vl->setSpacing(2);
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QString("color:%1; font-size:10px; font-weight:600;").arg(Theme::TEXT_MUTED));
        valueOut = new QLabel("—");
        valueOut->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700;").arg(accent));
        vl->addWidget(lbl); vl->addWidget(valueOut);
        return card;
    };

    statsRow->addWidget(mkStatCard("Worker PID",   statPid,      Theme::BLUE,   Theme::BLUE_LIGHT));
    statsRow->addWidget(mkStatCard("Live Threads", statThreads,  Theme::GREEN,  Theme::GREEN_LIGHT));
    statsRow->addWidget(mkStatCard("Running (R)",  statRunning,  Theme::ORANGE, Theme::ORANGE_LIGHT));
    statsRow->addWidget(mkStatCard("Shared Counter", counterLabel, Theme::PURPLE, Theme::PURPLE_LIGHT));
    outer->addLayout(statsRow);

    // ── Thread table ───────────────────────────────────────────────────────────
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tl = new QVBoxLayout(tableCard);
    tl->setContentsMargins(14, 12, 14, 12);
    tl->setSpacing(6);

    auto* tableTitle = new QLabel("Live Threads  —  /proc/[pid]/task/[tid]/status");
    tableTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    tl->addWidget(tableTitle);

    threadTable = new QTableWidget(0, 6);
    threadTable->setHorizontalHeaderLabels(
        {"TID", "State", "Kernel wait (wchan)", "Vol ctx", "Involuntary ctx", "Holds / Waits"});
    threadTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    threadTable->verticalHeader()->setVisible(false);
    threadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    threadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    threadTable->setAlternatingRowColors(true);
    threadTable->setShowGrid(false);
    // No fixed max-height — let the table grow with thread count (up to ~8 rows)
    threadTable->setMinimumHeight(80);
    threadTable->setStyleSheet(Theme::table());
    tl->addWidget(threadTable);
    outer->addWidget(tableCard);

    // ── Timeline card ──────────────────────────────────────────────────────────
    auto* timelineCard = new QWidget();
    timelineCard->setStyleSheet(Theme::card());
    auto* timL = new QVBoxLayout(timelineCard);
    timL->setContentsMargins(14, 12, 14, 12);
    timL->setSpacing(6);
    auto* timHeader = new QHBoxLayout();
    auto* timTitle = new QLabel("Thread Timeline");
    timTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* timHint = new QLabel("Solid bar = Running (R)  ·  Faded = Sleeping in futex (S)");
    timHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    timHeader->addWidget(timTitle); timHeader->addStretch(); timHeader->addWidget(timHint);
    timL->addLayout(timHeader);
    timeline = new ThreadTimeline();
    timL->addWidget(timeline);
    outer->addWidget(timelineCard);

    // ── Lock Graph card — only shown for the Deadlock demo ────────────────────
    lockCard = new QWidget();
    lockCard->setStyleSheet(Theme::card());
    auto* lkL = new QVBoxLayout(lockCard);
    lkL->setContentsMargins(14, 12, 14, 12);
    lkL->setSpacing(6);
    auto* lkHdr = new QHBoxLayout();
    auto* lkTitle = new QLabel("Lock Graph  —  who holds / waits for which mutex");
    lkTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* lkHint = new QLabel("Green solid = holds  ·  Orange dashed = waiting for");
    lkHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    lkHdr->addWidget(lkTitle); lkHdr->addStretch(); lkHdr->addWidget(lkHint);
    lkL->addLayout(lkHdr);
    lockGraph = new LockGraphView();
    lkL->addWidget(lockGraph);
    lockCard->hide();   // only visible when Deadlock demo is selected
    outer->addWidget(lockCard);

    // ── Log card ───────────────────────────────────────────────────────────────
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* ll = new QVBoxLayout(logCard);
    ll->setContentsMargins(14, 12, 14, 12);
    ll->setSpacing(6);
    auto* logHeader = new QHBoxLayout();
    auto* logTitle = new QLabel("Event Log");
    logTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setStyleSheet(Theme::btnGhost());
    clearLogBtn->setFixedHeight(26); clearLogBtn->setFixedWidth(60);
    logHeader->addWidget(logTitle); logHeader->addStretch(); logHeader->addWidget(clearLogBtn);
    ll->addLayout(logHeader);
    logView = new QTextEdit();
    logView->setReadOnly(true);
    logView->setMinimumHeight(70);
    logView->setMaximumHeight(110);
    logView->setStyleSheet(Theme::termLog());
    ll->addWidget(logView);
    outer->addWidget(logCard);

    // ── Status ────────────────────────────────────────────────────────────────
    statusLabel = new QLabel("Ready — select a demo and spawn a worker process.");
    statusLabel->setStyleSheet(QString("color:%1; font-size:11px; padding:2px 0;").arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(spawnBtn,    &QPushButton::clicked, this, &ThreadLab::onSpawnThreads);
    connect(killBtn,     &QPushButton::clicked, this, &ThreadLab::onKillWorker);
    connect(clearLogBtn, &QPushButton::clicked, logView, &QTextEdit::clear);
    connect(demoBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThreadLab::onDemoChanged);
    connect(demoBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                // Lock toggle is useful for Mutex Contention (1) and Race Condition (4)
                lockToggle->setVisible(idx == 1 || idx == 4);
                // Lock graph is only relevant for the Deadlock demo (5)
                lockCard->setVisible(idx == 5);
            });

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &ThreadLab::onRefresh);
    refreshTimer->start(1000);

    onDemoChanged(0);
    lockToggle->setVisible(false);   // hidden on initial selection (CPU Race)
}

ThreadLab::~ThreadLab() {
    if (workerProc) {
        if (workerPid > 0) LearnOSCleanup::unregisterPid(workerPid);
        workerProc->kill();
        workerProc->waitForFinished(500);
    }
}

void ThreadLab::onDemoChanged(int idx) {
    static const char* exps[] = {
        "<b>CPU Race</b><br><br>"
        "All threads run the same tight CPU loop simultaneously. "
        "They share the same PID but have unique TIDs in /proc/[pid]/task/. "
        "Watch all TIDs show state 'R' — real parallelism across cores.",

        "<b>Mutex Contention</b><br><br>"
        "Threads fight for a single <code>pthread_mutex_t</code>. "
        "Only one thread holds the lock at a time — the rest block in "
        "<code>futex(FUTEX_WAIT)</code> and the kernel puts them in state <b>S</b>.<br><br>"
        "Toggle <b>With Lock</b> off to switch to Race Condition mode and see "
        "the counter diverge from the expected value.",

        "<b>Producer-Consumer</b><br><br>"
        "Producer threads fill a shared buffer via <code>pthread_cond_t</code>. "
        "When full, producers wait on the condvar; when empty, consumers wait. "
        "State flips between R and S in the timeline.",

        "<b>Reader-Writer Lock</b><br><br>"
        "Multiple readers hold <code>pthread_rwlock_rdlock()</code> simultaneously. "
        "A writer calls <code>pthread_rwlock_wrlock()</code> — must wait for all readers. "
        "Readers don't block each other; the writer blocks everyone.",

        "<b>Race Condition — No Lock</b><br><br>"
        "Non-atomic read-modify-write on a shared counter. Increments get lost. "
        "The counter grows slower than nThreads × rate/s. "
        "Toggle <b>With Lock</b> on to add a mutex and watch the counter become correct.",

        "<b>Deadlock — ABBA Lock Inversion</b><br><br>"
        "Thread A acquires mutex_A, then tries mutex_B.<br>"
        "Thread B acquires mutex_B, then tries mutex_A.<br><br>"
        "After each holds one lock and waits for the other, "
        "<b>neither can ever proceed</b>. This is a real pthread deadlock.<br><br>"
        "Watch the Lock Graph: green arrows = holds, orange dashed = waiting for. "
        "When the cycle closes, <b>DEADLOCK DETECTED</b> appears.<br><br>"
        "The thread state in /proc shows <b>S</b> (sleeping in futex_wait). "
        "Solution: always acquire locks in the same order (A before B everywhere).",
    };
    if (idx >= 0 && idx < 6) emit explanationNeeded(exps[idx]);
}

void ThreadLab::onSpawnThreads() {
    if (workerProc) {
        workerProc->kill();
        workerProc->waitForFinished(500);
        workerProc->deleteLater();
        workerProc = nullptr;
        workerPid = -1;
    }
    lockHolds.clear(); lockWaits.clear();
    deadTidA = -1; deadTidB = -1;
    lockGraph->clear();

    int demo     = demoBox->currentIndex();
    int nThreads = threadCountSpin->value();

    // If lock toggle is visible and unchecked, run the race-condition demo
    // regardless of which demo is nominally selected.
    bool raceMode = lockToggle->isVisible() && !lockToggle->isChecked();
    if (raceMode) demo = 4;

    workerProc = new QProcess(this);
    workerProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(workerProc, &QProcess::readyRead, this, &ThreadLab::onWorkerOutput);
    connect(workerProc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                logView->append(QString(
                    "<span style='color:#94A3B8;'>[%1]</span> "
                    "<span style='color:#FBBF24;'>Worker process exited.</span>")
                    .arg(QTime::currentTime().toString("hh:mm:ss")));
                workerPid = -1;
                spawnBtn->setEnabled(true);
                killBtn->setEnabled(false);
                demoBox->setEnabled(true);
                threadCountSpin->setEnabled(true);
                lockToggle->setEnabled(true);
                statPid->setText("—"); statThreads->setText("—");
                statRunning->setText("—"); counterLabel->setText("—");
            });

    QString binary = findThreadWorker();
    workerProc->start(binary, {QString::number(nThreads), QString::number(demo)});
    if (workerProc->state() != QProcess::NotRunning)
        LearnOSCleanup::registerPid(workerProc->processId());

    if (!workerProc->waitForStarted(2000)) {
        statusLabel->setText("⚠ Could not start learnos_thread_worker — rebuild first.");
        workerProc->deleteLater();
        workerProc = nullptr;
        return;
    }

    spawnBtn->setEnabled(false);
    killBtn->setEnabled(true);
    demoBox->setEnabled(false);
    threadCountSpin->setEnabled(false);
    lockToggle->setEnabled(false);
    timeline->clear();
    counterLabel->setText("0");

    QString demoName = raceMode ? "Race Condition (no lock)" : demoBox->currentText().left(30);
    statusLabel->setText(QString("Worker spawning — %1 threads, demo: %2")
        .arg(nThreads).arg(demoName));
    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#4ADE80;'>Spawned</span> worker — %2 threads, demo: <i>%3</i>")
        .arg(QTime::currentTime().toString("hh:mm:ss")).arg(nThreads).arg(demoName));
}

void ThreadLab::onWorkerOutput() {
    if (!workerProc) return;
    QByteArray data = workerProc->readAll();
    for (auto& rawLine : data.split('\n')) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("READY ")) {
            workerPid = line.mid(6).toLong();
            LearnOSCleanup::registerPid(workerPid);
            statPid->setText(QString::number(workerPid));
            statusLabel->setText(QString("Worker PID %1 running").arg(workerPid));
            EventBus::get().processSpawned(workerPid,
                QString("thread-worker (%1t)").arg(threadCountSpin->value()),
                "thread-worker");
            logView->append(QString(
                "<span style='color:#94A3B8;'>[%1]</span> "
                "<span style='color:#4ADE80;'>Worker ready</span> — PID <b>%2</b>")
                .arg(QTime::currentTime().toString("hh:mm:ss")).arg(workerPid));

            emit explanationNeeded(QString(
                "<b>Worker spawned — PID %1</b><br><br>"
                "Threads created with <code>pthread_create()</code>. "
                "All share the same PID but have unique TIDs.<br><br>"
                "Thread state is read from:<br>"
                "<code>/proc/%1/task/[tid]/status → State: S/R/D</code><br><br>"
                "<b>S (sleeping)</b> = blocked in <code>futex_wait()</code> "
                "— this is what mutex lock / cond_wait / rwlock look like at the kernel level.<br>"
                "<b>R (running)</b> = on a CPU right now.<br>"
                "<b>D (disk wait)</b> = uninterruptible I/O sleep."
            ).arg(workerPid));

        } else if (line.startsWith("STATUS ")) {
            auto parts = line.mid(7).split(' ');
            for (auto& part : parts)
                if (part.startsWith("counter="))
                    counterLabel->setText(part.mid(8));

        } else if (line.startsWith("LOCKSTATE ")) {
            auto parts = line.mid(10).split(' ');
            long tid = -1;
            QString holds = "none", waits = "none";
            for (auto& p : parts) {
                if (p.startsWith("tid="))        tid   = p.mid(4).toLong();
                if (p.startsWith("holds="))      holds = p.mid(6);
                if (p.startsWith("waits_for="))  waits = p.mid(10);
            }
            if (tid > 0) {
                lockHolds[tid] = holds;
                lockWaits[tid] = waits;
            }

        } else if (line.startsWith("DEADLOCK_DETECTED ")) {
            auto parts = line.mid(18).split(' ');
            for (auto& p : parts) {
                if (p.startsWith("tid_a=")) deadTidA = p.mid(6).toLong();
                if (p.startsWith("tid_b=")) deadTidB = p.mid(6).toLong();
            }
            lockGraph->setDeadlockDetected(deadTidA, deadTidB);
            logView->append(QString(
                "<span style='color:#94A3B8;'>[%1]</span> "
                "<span style='color:#EF4444; font-weight:bold;'>⚠ DEADLOCK</span> "
                "— TID %2 ↔ TID %3 (neither can proceed)")
                .arg(QTime::currentTime().toString("hh:mm:ss"))
                .arg(deadTidA).arg(deadTidB));

            emit explanationNeeded(QString(
                "<b>⚠ DEADLOCK DETECTED</b><br><br>"
                "TID %1 holds <code>mutex_A</code>, waiting for <code>mutex_B</code><br>"
                "TID %2 holds <code>mutex_B</code>, waiting for <code>mutex_A</code><br><br>"
                "Both threads are stuck in <code>futex_wait()</code> — their /proc state "
                "shows <b>S</b>. The kernel has no built-in deadlock detector; they will "
                "stay blocked until the process is killed.<br><br>"
                "<b>Cause:</b> ABBA lock ordering — each thread acquires locks in opposite order.<br>"
                "<b>Fix:</b> Always acquire mutexes in a globally consistent order "
                "(e.g., always A before B). pthread_mutex_trylock() can also break deadlocks "
                "at the cost of retry logic."
            ).arg(deadTidA).arg(deadTidB));
        }
    }
}

void ThreadLab::onKillWorker() {
    if (!workerProc) return;
    if (workerPid > 0) {
        EventBus::get().processKilled(workerPid, "thread-worker");
        LearnOSCleanup::unregisterPid(workerPid);
    }
    workerProc->kill();
    workerProc->waitForFinished(500);
    workerProc->deleteLater();
    workerProc = nullptr;

    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#F87171;'>Killed</span> worker PID <b>%2</b>")
        .arg(QTime::currentTime().toString("hh:mm:ss")).arg(workerPid));

    workerPid = -1;
    spawnBtn->setEnabled(true);
    killBtn->setEnabled(false);
    demoBox->setEnabled(true);
    threadCountSpin->setEnabled(true);
    lockToggle->setEnabled(true);
    threadTable->setRowCount(0);
    statPid->setText("—"); statThreads->setText("—");
    statRunning->setText("—"); counterLabel->setText("—");
    statusLabel->setText("Worker killed — ready to spawn again.");
    lockHolds.clear(); lockWaits.clear();
    lockGraph->clear();
}

void ThreadLab::onRefresh() {
    if (workerPid > 0 && kill(workerPid, 0) != 0) workerPid = -1;
    if (workerPid > 0) refreshTable();
}

// Read the kernel function the thread is sleeping in from /proc/[pid]/task/[tid]/wchan
QString ThreadLab::readFutexState(pid_t pid, long tid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task/%ld/wchan", pid, tid);
    std::ifstream f(path);
    std::string s;
    if (std::getline(f, s) && !s.empty()) {
        if (s == "futex_wait_queue" || s == "futex_wait")   return "futex_wait (mutex/cond)";
        if (s == "do_wait")                                  return "waitpid()";
        if (s == "pipe_read" || s == "pipe_wait")           return "pipe_read";
        if (s == "ep_poll")                                  return "epoll_wait";
        if (s == "0" || s == "-")                            return "";
        return QString::fromStdString(s);
    }
    return "";
}

QVector<ThreadInfo> ThreadLab::readThreads(pid_t pid) {
    QVector<ThreadInfo> result;
    QString taskPath = QString("/proc/%1/task").arg(pid);
    DIR* dir = opendir(taskPath.toLocal8Bit().constData());
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        QString name(ent->d_name);
        bool ok = false;
        long tid = name.toLong(&ok);
        if (!ok) continue;

        ThreadInfo t;
        t.tid   = tid;
        t.alive = true;
        t.holdsLock    = lockHolds.value(tid, "none");
        t.waitsForLock = lockWaits.value(tid, "none");

        std::ifstream sf(
            QString("/proc/%1/task/%2/status").arg(pid).arg(tid).toStdString());
        std::string line;
        while (std::getline(sf, line)) {
            if (line.rfind("State:", 0) == 0)
                t.state = QString(line.size() > 7 ? line[7] : '?');
            if (line.rfind("voluntary_ctxt_switches:", 0) == 0) {
                std::istringstream ss(line.substr(24)); ss >> t.voluntarySwitches;
            }
            if (line.rfind("nonvoluntary_ctxt_switches:", 0) == 0) {
                std::istringstream ss(line.substr(27)); ss >> t.involuntarySwitches;
            }
        }

        if (t.state == "S")
            t.futexState = readFutexState(pid, tid);

        result.append(t);
    }
    closedir(dir);
    return result;
}

void ThreadLab::refreshTable() {
    auto threads = readThreads(workerPid);
    timeline->addTick(threads);

    int runCount = 0;
    for (auto& t : threads) if (t.state == "R") runCount++;
    statThreads->setText(QString::number(threads.size()));
    statRunning->setText(QString::number(runCount));

    lockGraph->setThreads(threads);

    threadTable->setRowCount(0);
    int ci = 0;
    for (auto& t : threads) {
        int row = threadTable->rowCount();
        threadTable->insertRow(row);

        auto cell = [&](const QString& tx, const char* c = nullptr) {
            auto* item = new QTableWidgetItem(tx);
            item->setTextAlignment(Qt::AlignCenter);
            if (c) item->setForeground(QColor(c));
            return item;
        };

        QString stateLabel =
            t.state == "R" ? "▶ Running" :
            t.state == "S" ? "◌ Sleeping" :
            t.state == "D" ? "⧖ DiskWait" : t.state;
        const char* stateColor =
            t.state == "R" ? Theme::GREEN :
            t.state == "S" ? Theme::BLUE  : Theme::ORANGE;

        QString futexDetail = t.futexState.isEmpty() ? "—" : t.futexState;

        QString lockSummary;
        if (t.holdsLock != "none")    lockSummary += "holds:" + t.holdsLock + " ";
        if (t.waitsForLock != "none") lockSummary += "waits:" + t.waitsForLock;

        auto* tidItem = new QTableWidgetItem(QString("● %1").arg(t.tid));
        tidItem->setTextAlignment(Qt::AlignCenter);
        tidItem->setForeground(ThreadTimeline::PALETTE[ci % 8]);
        QFont bold = tidItem->font(); bold.setBold(true); tidItem->setFont(bold);

        threadTable->setItem(row, 0, tidItem);
        threadTable->setItem(row, 1, cell(stateLabel, stateColor));
        threadTable->setItem(row, 2, cell(futexDetail, Theme::TEXT_SECONDARY));
        threadTable->setItem(row, 3, cell(QString::number(t.voluntarySwitches)));
        threadTable->setItem(row, 4, cell(QString::number(t.involuntarySwitches)));
        threadTable->setItem(row, 5, cell(lockSummary.trimmed(),
            !lockSummary.isEmpty() ? Theme::ORANGE : Theme::TEXT_MUTED));
        ci++;
    }

    statusLabel->setText(QString(
        "Worker PID %1  ·  %2 threads  ·  %3 running")
        .arg(workerPid).arg(threads.size()).arg(runCount));
}
