#include "ThreadLab.h"
#include "Theme.h"
#include <QHeaderView>
#include <QPainterPath>
#include <QTime>
#include <QScrollBar>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <dirent.h>

// ── Palette shared between timeline + table ───────────────────────────────────
static const QColor THREAD_PALETTE[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};
const QColor ThreadTimeline::PALETTE[] = {
    QColor("#4F6EF7"), QColor("#22C55E"), QColor("#F97316"),
    QColor("#A855F7"), QColor("#EF4444"), QColor("#14B8A6"),
    QColor("#EAB308"), QColor("#EC4899")
};

// ── ThreadTimeline ────────────────────────────────────────────────────────────

ThreadTimeline::ThreadTimeline(QWidget* p) : QWidget(p) {
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(QString(
        "background: #0F172A;"
        "border-radius: 10px;"
        "border: 1px solid %1;"
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

    // Dark terminal background
    QPainterPath bg;
    bg.addRoundedRect(rect(), 10, 10);
    p.fillPath(bg, QColor("#0F172A"));

    if (history.isEmpty()) {
        p.setPen(QColor("#475569"));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Thread timeline will appear when a worker is running");
        return;
    }

    int w = width(), h = height();
    int numT = tids.size();
    if (numT == 0) return;

    const int labelW = 78;
    const int topPad = 10;
    const int botPad = 10;
    int chartH  = h - topPad - botPad;
    int rowH    = std::max(16, chartH / numT);
    int chartW  = w - labelW - 12;
    float tickW = history.size() > 0 ? (float)chartW / history.size() : 8.f;

    // Draw subtle grid lines
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= numT; i++) {
        int y = topPad + i * rowH;
        p.drawLine(labelW, y, w - 6, y);
    }

    // Draw thread labels + row backgrounds
    for (int i = 0; i < numT; i++) {
        int y = topPad + i * rowH;
        // Subtle alternating row tint
        if (i % 2 == 0) {
            QRect rowBg(labelW, y, chartW, rowH);
            p.fillRect(rowBg, QColor(255, 255, 255, 5));
        }
        // TID label
        QColor lc = colors[tids[i]];
        p.setPen(lc.lighter(130));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(QRect(4, y, labelW - 6, rowH), Qt::AlignVCenter | Qt::AlignRight,
            QString("TID %1").arg(tids[i]));
    }

    // Draw tick bars
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
                p.fillPath(bp, colors[st.first]);
            } else {
                QColor c = colors[st.first];
                c.setAlpha(35);
                p.fillPath(bp, c);
            }
        }
    }

    // Legend at the bottom-right
    int legendX = labelW + 4;
    int legendY = h - botPad + 2;
    p.setFont(QFont("Segoe UI", 8));
    // Running swatch
    p.fillRect(legendX, legendY - 7, 10, 7, QColor("#4F6EF7"));
    p.setPen(QColor("#94A3B8"));
    p.drawText(legendX + 13, legendY, "Running");
    // Sleeping swatch
    QColor sleepC("#4F6EF7"); sleepC.setAlpha(35);
    p.fillRect(legendX + 70, legendY - 7, 10, 7, sleepC);
    p.drawText(legendX + 83, legendY, "Sleeping");
}

// ── ThreadLab ─────────────────────────────────────────────────────────────────

ThreadLab::ThreadLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Title row ──────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🧵  Thread Lab — POSIX Threads & Synchronization");
    title->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));

    auto* chip = new QLabel("● REAL THREADS");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::PURPLE).arg(Theme::PURPLE_LIGHT));

    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    // ── Description banner ─────────────────────────────────────────────────────
    auto* banner = new QLabel(
        "Spawns a real worker process with multiple POSIX threads. Threads share "
        "the same PID but have unique TIDs in <b>/proc/[pid]/task/</b>. Watch them "
        "run, sleep, and contend for mutexes in real time.");
    banner->setWordWrap(true);
    banner->setStyleSheet(QString(
        "background:%1; color:%2; border:1px solid %3;"
        "border-radius:10px; padding:10px 14px; font-size:11px;"
    ).arg(Theme::BLUE_LIGHT, Theme::TEXT_PRIMARY, Theme::BORDER));
    outer->addWidget(banner);

    // ── Controls card ──────────────────────────────────────────────────────────
    auto* ctrl = new QWidget();
    ctrl->setStyleSheet(Theme::card());
    auto* cl = new QGridLayout(ctrl);
    cl->setContentsMargins(16, 12, 16, 12);
    cl->setSpacing(10);

    // Row 0 labels
    auto mkLabel = [&](const QString& txt) -> QLabel* {
        auto* l = new QLabel(txt);
        l->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
            .arg(Theme::TEXT_SECONDARY));
        return l;
    };
    cl->addWidget(mkLabel("Demo scenario"), 0, 0);
    cl->addWidget(mkLabel("Thread count"),  0, 1);
    cl->addWidget(mkLabel(""),              0, 2);
    cl->addWidget(mkLabel(""),              0, 3);

    // Row 1 controls
    demoBox = new QComboBox();
    demoBox->addItem("CPU Race — all threads burn CPU in parallel");
    demoBox->addItem("Mutex Contention — threads fight for a single lock");
    demoBox->addItem("Producer-Consumer — cooperate via condvar");
    demoBox->addItem("Reader-Writer — shared data, rwlock");
    demoBox->setStyleSheet(Theme::input());

    threadCountSpin = new QSpinBox();
    threadCountSpin->setRange(2, 8);
    threadCountSpin->setValue(4);
    threadCountSpin->setStyleSheet(Theme::input());

    spawnBtn = new QPushButton("▶  Spawn Worker");
    spawnBtn->setStyleSheet(Theme::btnPrimary());
    spawnBtn->setMinimumHeight(34);

    killBtn = new QPushButton("✕  Kill Worker");
    killBtn->setStyleSheet(Theme::btnDanger());
    killBtn->setMinimumHeight(34);
    killBtn->setEnabled(false);

    cl->addWidget(demoBox,          1, 0);
    cl->addWidget(threadCountSpin,  1, 1);
    cl->addWidget(spawnBtn,         1, 2);
    cl->addWidget(killBtn,          1, 3);
    cl->setColumnStretch(0, 3);
    cl->setColumnStretch(1, 1);
    cl->setColumnStretch(2, 1);
    cl->setColumnStretch(3, 1);
    outer->addWidget(ctrl);

    // ── Stats row (three mini-cards) ───────────────────────────────────────────
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(10);

    auto mkStatCard = [&](const QString& label, QLabel*& valueOut,
                          const char* accent) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet(QString(
            "background: white; border-radius: 10px; border: 1px solid %1;"
        ).arg(Theme::BORDER));
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(14, 10, 14, 10);
        vl->setSpacing(2);
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QString("color:%1; font-size:10px; font-weight:600;")
            .arg(Theme::TEXT_MUTED));
        valueOut = new QLabel("—");
        valueOut->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700;")
            .arg(accent));
        vl->addWidget(lbl);
        vl->addWidget(valueOut);
        return card;
    };

    statsRow->addWidget(mkStatCard("Worker PID",       statPid,     Theme::BLUE));
    statsRow->addWidget(mkStatCard("Live Threads",     statThreads, Theme::GREEN));
    statsRow->addWidget(mkStatCard("Running (R)",      statRunning, Theme::ORANGE));
    outer->addLayout(statsRow);

    // ── Thread table ───────────────────────────────────────────────────────────
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tl = new QVBoxLayout(tableCard);
    tl->setContentsMargins(14, 12, 14, 12);
    tl->setSpacing(6);

    auto* tableTitle = new QLabel("Live Threads  —  /proc/[pid]/task/");
    tableTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    tl->addWidget(tableTitle);

    threadTable = new QTableWidget(0, 5);
    threadTable->setHorizontalHeaderLabels(
        {"TID", "State", "Voluntary ctx", "Involuntary ctx", "RSS"});
    threadTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    threadTable->verticalHeader()->setVisible(false);
    threadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    threadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    threadTable->setAlternatingRowColors(true);
    threadTable->setShowGrid(false);
    threadTable->setMinimumHeight(120);
    threadTable->setMaximumHeight(200);
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
    timTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* timHint = new QLabel("Solid = Running · Faded = Sleeping");
    timHint->setStyleSheet(QString(
        "color:%1; font-size:10px;"
    ).arg(Theme::TEXT_MUTED));
    timHeader->addWidget(timTitle);
    timHeader->addStretch();
    timHeader->addWidget(timHint);
    timL->addLayout(timHeader);

    timeline = new ThreadTimeline();
    timeline->setMinimumHeight(160);
    timL->addWidget(timeline);
    outer->addWidget(timelineCard, 1);

    // ── Log card ───────────────────────────────────────────────────────────────
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* ll = new QVBoxLayout(logCard);
    ll->setContentsMargins(14, 12, 14, 12);
    ll->setSpacing(6);

    auto* logHeader = new QHBoxLayout();
    auto* logTitle = new QLabel("Event Log");
    logTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));

    clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setStyleSheet(Theme::btnGhost());
    clearLogBtn->setFixedHeight(26);
    clearLogBtn->setFixedWidth(60);

    logHeader->addWidget(logTitle);
    logHeader->addStretch();
    logHeader->addWidget(clearLogBtn);
    ll->addLayout(logHeader);

    logView = new QTextEdit();
    logView->setReadOnly(true);
    logView->setMinimumHeight(80);
    logView->setMaximumHeight(120);
    logView->setStyleSheet(Theme::termLog());
    ll->addWidget(logView);
    outer->addWidget(logCard);

    // ── Status bar ─────────────────────────────────────────────────────────────
    statusLabel = new QLabel("Ready — select a demo and spawn a worker process.");
    statusLabel->setStyleSheet(QString(
        "color:%1; font-size:11px; padding:2px 0;"
    ).arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    // ── Connections ────────────────────────────────────────────────────────────
    connect(spawnBtn,    &QPushButton::clicked, this, &ThreadLab::onSpawnThreads);
    connect(killBtn,     &QPushButton::clicked, this, &ThreadLab::onKillWorker);
    connect(clearLogBtn, &QPushButton::clicked, logView, &QTextEdit::clear);
    connect(demoBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThreadLab::onDemoChanged);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &ThreadLab::onRefresh);
    refreshTimer->start(1000);

    onDemoChanged(0);
}

ThreadLab::~ThreadLab() {
    if (workerPid > 0) { kill(workerPid, SIGKILL); waitpid(workerPid, nullptr, WNOHANG); }
}

void ThreadLab::onDemoChanged(int idx) {
    static const char* exps[] = {
        "<b>CPU Race</b><br><br>"
        "All threads run the same tight CPU loop simultaneously. "
        "Because they share the same process, the OS scheduler gives each thread "
        "its own time slice on a different core. You'll see all TIDs show state 'R' — "
        "they're genuinely running in parallel.<br><br>"
        "Watch the thread timeline: all bars solid = real parallelism.",

        "<b>Mutex Contention</b><br><br>"
        "Threads try to acquire the same <code>pthread_mutex_t</code>. "
        "Only one succeeds — the others block with <code>pthread_mutex_lock()</code> "
        "and enter the 'S' (sleeping) state. The winner runs, releases the lock, "
        "and the kernel wakes one waiter.<br><br>"
        "Watch: only one thread is 'R' at a time despite multiple threads existing.",

        "<b>Producer-Consumer</b><br><br>"
        "Producer threads fill a shared buffer, consumer threads drain it. "
        "Coordination via <code>pthread_cond_t</code>. When the buffer is full, "
        "producers wait. When empty, consumers wait.<br><br>"
        "This is how thread pools, event loops, and pipelines work.",

        "<b>Reader-Writer Lock</b><br><br>"
        "Multiple reader threads hold <code>pthread_rwlock_rdlock()</code> simultaneously "
        "— reads are parallel. A writer thread calls <code>pthread_rwlock_wrlock()</code> "
        "which waits for all readers to finish, then gets exclusive access.<br><br>"
        "Readers don't block each other. Writers block everyone.",
    };
    if (idx >= 0 && idx < 4) emit explanationNeeded(exps[idx]);
}

void ThreadLab::onSpawnThreads() {
    if (workerPid > 0) {
        kill(workerPid, SIGKILL);
        waitpid(workerPid, nullptr, WNOHANG);
        workerPid = -1;
    }

    int demo    = demoBox->currentIndex();
    int nThreads = threadCountSpin->value();

    workerPid = fork();
    if (workerPid == 0) {
        // Child: try the dedicated worker binary first
        char countStr[8]; snprintf(countStr, 8, "%d", nThreads);
        char demoStr[8];  snprintf(demoStr,  8, "%d", demo);
        execlp("learnos_thread_worker", "learnos_thread_worker", countStr, demoStr, nullptr);

        // Fallback inline worker
        nice(19);
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        auto threadFunc = [](void* arg) -> void* {
            pthread_mutex_t* m = (pthread_mutex_t*)arg;
            while (true) {
                pthread_mutex_lock(m);
                volatile double x = 1.0;
                for (int i = 0; i < 100000; i++) x += i;
                pthread_mutex_unlock(m);
                usleep(1000);
            }
            return nullptr;
        };
        std::vector<pthread_t> threads(nThreads);
        for (int i = 0; i < nThreads; i++)
            pthread_create(&threads[i], nullptr, threadFunc, &mutex);
        for (auto& t : threads) pthread_join(t, nullptr);
        _exit(0);
    }

    if (workerPid > 0) {
        spawnBtn->setEnabled(false);
        killBtn->setEnabled(true);
        demoBox->setEnabled(false);
        threadCountSpin->setEnabled(false);
        timeline->clear();

        statPid->setText(QString::number(workerPid));
        statThreads->setText("…");
        statRunning->setText("…");

        statusLabel->setText(QString(
            "Worker PID %1 running — %2 threads").arg(workerPid).arg(nThreads));

        logView->append(QString(
            "<span style='color:#94A3B8;'>[%1]</span> "
            "<span style='color:#4ADE80;'>Spawned</span> worker PID <b>%2</b> — "
            "%3 thread(s), demo: <i>%4</i>")
            .arg(QTime::currentTime().toString("hh:mm:ss"))
            .arg(workerPid).arg(nThreads)
            .arg(demoBox->currentText().left(24)));

        emit explanationNeeded(QString(
            "<b>Worker spawned — PID %1</b><br><br>"
            "The worker created <b>%2 threads</b> using <code>pthread_create()</code>. "
            "They all share:<br>"
            "• Same PID in <code>/proc</code><br>"
            "• Same virtual address space (heap, globals)<br>"
            "• Same open file descriptors<br><br>"
            "But each has its own:<br>"
            "• Thread ID (TID) visible in <code>/proc/%1/task/</code><br>"
            "• Stack (mapped privately)<br>"
            "• CPU registers and program counter<br><br>"
            "This is why threading is fast: no address space copy (unlike fork)."
        ).arg(workerPid).arg(nThreads));
    }
}

void ThreadLab::onKillWorker() {
    if (workerPid <= 0) return;
    kill(workerPid, SIGKILL);
    waitpid(workerPid, nullptr, WNOHANG);

    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#F87171;'>Killed</span> worker PID <b>%2</b>")
        .arg(QTime::currentTime().toString("hh:mm:ss")).arg(workerPid));

    workerPid = -1;
    spawnBtn->setEnabled(true);
    killBtn->setEnabled(false);
    demoBox->setEnabled(true);
    threadCountSpin->setEnabled(true);
    threadTable->setRowCount(0);
    statPid->setText("—");
    statThreads->setText("—");
    statRunning->setText("—");
    statusLabel->setText("Worker killed — ready to spawn again.");
}

void ThreadLab::onRefresh() {
    if (workerPid > 0 && kill(workerPid, 0) != 0) {
        logView->append(QString(
            "<span style='color:#94A3B8;'>[%1]</span> "
            "<span style='color:#FBBF24;'>Worker PID %2 exited on its own.</span>")
            .arg(QTime::currentTime().toString("hh:mm:ss")).arg(workerPid));
        workerPid = -1;
        spawnBtn->setEnabled(true);
        killBtn->setEnabled(false);
        demoBox->setEnabled(true);
        threadCountSpin->setEnabled(true);
        statPid->setText("—");
        statThreads->setText("—");
        statRunning->setText("—");
    }
    if (workerPid > 0) refreshTable();
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

        // Color dot + TID
        QColor tc = THREAD_PALETTE[ci % 8];
        auto* tidItem = new QTableWidgetItem(QString("● %1").arg(t.tid));
        tidItem->setTextAlignment(Qt::AlignCenter);
        tidItem->setForeground(tc);
        QFont bold = tidItem->font(); bold.setBold(true);
        tidItem->setFont(bold);

        threadTable->setItem(row, 0, tidItem);
        threadTable->setItem(row, 1, cell(stateLabel, stateColor));
        threadTable->setItem(row, 2, cell(QString::number(t.voluntarySwitches)));
        threadTable->setItem(row, 3, cell(QString::number(t.involuntarySwitches)));
        threadTable->setItem(row, 4, cell("shared"));
        ci++;
    }

    statusLabel->setText(QString(
        "Worker PID %1  ·  %2 threads  ·  %3 running")
        .arg(workerPid).arg(threads.size()).arg(runCount));
}
