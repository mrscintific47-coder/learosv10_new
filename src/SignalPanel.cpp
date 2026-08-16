#include "SignalPanel.h"
#include "EventBus.h"
#include "Theme.h"
#include <QHeaderView>
#include <QDateTime>
#include <QSplitter>
#include <QScrollArea>
#include <QFrame>
#include <sys/wait.h>
#include <fstream>
#include <sstream>
#include <cmath>

// ── Signal definitions ────────────────────────────────────────────────────

const std::vector<SignalInfo> SignalPanel::SIGNALS = {
    {SIGHUP,  "SIGHUP",  "Hangup",           "Terminal disconnected or process group leader died.",                          "Terminate"},
    {SIGINT,  "SIGINT",  "Interrupt",         "Sent by Ctrl+C. Asks process to stop gracefully.",                           "Terminate"},
    {SIGQUIT, "SIGQUIT", "Quit",              "Sent by Ctrl+\\. Like SIGINT but also dumps core.",                          "Core"},
    {SIGILL,  "SIGILL",  "Illegal instruction","CPU executed an invalid instruction. Usually a bug.",                        "Core"},
    {SIGABRT, "SIGABRT", "Abort",             "Sent by abort(). Process terminates and dumps core.",                        "Core"},
    {SIGFPE,  "SIGFPE",  "FP exception",      "Arithmetic error: divide by zero or overflow.",                              "Core"},
    {SIGKILL, "SIGKILL", "Kill (unblockable)","Cannot be caught, ignored, or blocked. Instant death.",                      "Terminate"},
    {SIGSEGV, "SIGSEGV", "Segfault",          "Invalid memory access. The classic C++ crash.",                              "Core"},
    {SIGPIPE, "SIGPIPE", "Broken pipe",       "Write to pipe with no reader. Default: terminate.",                          "Terminate"},
    {SIGALRM, "SIGALRM", "Alarm",             "Timer set by alarm() expired.",                                              "Terminate"},
    {SIGTERM, "SIGTERM", "Terminate",         "Polite kill request. Process can catch and clean up.",                       "Terminate"},
    {SIGUSR1, "SIGUSR1", "User signal 1",     "Application-defined. Can do anything the app wants.",                       "Terminate"},
    {SIGUSR2, "SIGUSR2", "User signal 2",     "Application-defined. Can do anything the app wants.",                       "Terminate"},
    {SIGCHLD, "SIGCHLD", "Child changed",     "Sent to parent when child stops, continues, or terminates.",                 "Ignore"},
    {SIGCONT, "SIGCONT", "Continue",          "Resume a stopped process. Cannot be blocked.",                               "Continue"},
    {SIGSTOP, "SIGSTOP", "Stop (unblockable)","Freeze process. Cannot be caught or ignored. Like a debugger breakpoint.",   "Stop"},
    {SIGTSTP, "SIGTSTP", "Terminal stop",     "Sent by Ctrl+Z. Like SIGSTOP but can be caught.",                           "Stop"},
    {SIGTTIN, "SIGTTIN", "BG read",           "Background process tried to read from terminal.",                            "Stop"},
    {SIGTTOU, "SIGTTOU", "BG write",          "Background process tried to write to terminal.",                             "Stop"},
    {SIGURG,  "SIGURG",  "Urgent socket data","Out-of-band data arrived on socket.",                                        "Ignore"},
    {SIGXCPU, "SIGXCPU", "CPU time limit",    "Process exceeded CPU time limit set by setrlimit().",                        "Core"},
    {SIGXFSZ, "SIGXFSZ", "File size limit",   "Process tried to create file larger than allowed.",                          "Core"},
    {SIGWINCH,"SIGWINCH","Window resize",     "Terminal window was resized. Used by ncurses apps.",                         "Ignore"},
};

// ── SignalPanel ───────────────────────────────────────────────────────────

SignalPanel::SignalPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* content = new QWidget();
    content->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    // Title
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("⚡  Signal Panel — Send Real Linux Signals");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● REAL SIGNALS");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::ORANGE).arg(Theme::ORANGE_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel("Select a process, pick a signal, fire it. Watch what happens.");
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    // Main split: left=process table, right=signal picker + log
    auto* mainRow = new QHBoxLayout();
    mainRow->setSpacing(10);

    // ── Process table ──
    auto* procCard = new QWidget();
    procCard->setStyleSheet(Theme::card());
    auto* procLayout = new QVBoxLayout(procCard);
    procLayout->setContentsMargins(12,10,12,10);
    procLayout->setSpacing(6);

    auto* procTitle = new QLabel("Target Process");
    procTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    procLayout->addWidget(procTitle);

    processTable = new QTableWidget(0, 4);
    processTable->setHorizontalHeaderLabels({"PID","Name","State","Type"});
    processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    processTable->verticalHeader()->setVisible(false);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable->setStyleSheet(Theme::table());
    procLayout->addWidget(processTable);

    auto* procBtnRow = new QHBoxLayout();
    spawnBtn = new QPushButton("+ Spawn Target");
    killBtn  = new QPushButton("✕ Kill Selected");
    spawnBtn->setStyleSheet(Theme::btnSuccess());
    killBtn->setStyleSheet(Theme::btnDanger());
    procBtnRow->addWidget(spawnBtn);
    procBtnRow->addWidget(killBtn);
    procLayout->addLayout(procBtnRow);
    mainRow->addWidget(procCard, 2);

    // ── Right panel: signal picker + description + log ──
    auto* rightPanel = new QWidget();
    rightPanel->setStyleSheet("background:transparent;");
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(10);

    // Signal selector card
    auto* sigCard = new QWidget();
    sigCard->setStyleSheet(Theme::card());
    auto* sigLayout = new QVBoxLayout(sigCard);
    sigLayout->setContentsMargins(12,10,12,10);
    sigLayout->setSpacing(8);

    auto* sigTitle = new QLabel("Signal");
    sigTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    sigLayout->addWidget(sigTitle);

    signalBox = new QComboBox();
    for (auto& s : SIGNALS)
        signalBox->addItem(QString("%1 (%2) — %3").arg(s.name).arg(s.number).arg(s.shortDesc));
    signalBox->setStyleSheet(Theme::input());
    sigLayout->addWidget(signalBox);

    signalDescLabel = new QLabel();
    signalDescLabel->setWordWrap(true);
    signalDescLabel->setStyleSheet(QString(
        "color:%1;font-size:11px;background:%2;border-radius:8px;"
        "padding:8px;border:1px solid %3;"
    ).arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    signalDescLabel->setMinimumHeight(60);
    sigLayout->addWidget(signalDescLabel);

    sendBtn = new QPushButton("⚡  Fire Signal");
    sendBtn->setStyleSheet(
        "QPushButton{background:#F97316;color:white;border:none;border-radius:8px;"
        "padding:10px;font-size:13px;font-weight:bold;}"
        "QPushButton:hover{background:#EA580C;}"
        "QPushButton:pressed{background:#C2410C;}");
    sigLayout->addWidget(sendBtn);

    statusLabel = new QLabel("Select a process and a signal, then fire.");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    sigLayout->addWidget(statusLabel);
    rightLayout->addWidget(sigCard);

    // Signal masks card — reads SigPnd/SigBlk/SigCgt from /proc
    auto* maskCard = new QWidget();
    maskCard->setStyleSheet(Theme::card());
    auto* maskLayout = new QVBoxLayout(maskCard);
    maskLayout->setContentsMargins(12,8,12,8);
    maskLayout->setSpacing(4);
    auto* maskTitle = new QLabel("Signal Masks  —  /proc/[pid]/status");
    maskTitle->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    maskLayout->addWidget(maskTitle);
    auto* maskHint = new QLabel("SigPnd=pending  SigBlk=blocked  SigCgt=caught by sigaction handler");
    maskHint->setStyleSheet(QString("color:%1;font-size:9px;").arg(Theme::TEXT_MUTED));
    maskLayout->addWidget(maskHint);
    signalMaskLabel = new QLabel("Select a process to read its signal bitmasks from /proc");
    signalMaskLabel->setWordWrap(true);
    signalMaskLabel->setStyleSheet(QString(
        "font-family:Consolas; font-size:10px; color:%1; background:%2; border-radius:6px; padding:5px;"
    ).arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT));
    maskLayout->addWidget(signalMaskLabel);
    rightLayout->addWidget(maskCard);

    // Async-signal-safety demo
    auto* asyncCard = new QWidget();
    asyncCard->setStyleSheet(Theme::card());
    auto* asyncL = new QVBoxLayout(asyncCard);
    asyncL->setContentsMargins(12,8,12,8);
    asyncL->setSpacing(4);
    auto* asyncTitle = new QLabel("Bug Demo: Async-Signal-Safety Violation");
    asyncTitle->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    asyncL->addWidget(asyncTitle);
    auto* asyncHint = new QLabel(
        "Spawns a target that calls non-reentrant <code>printf()</code> from a SIGALRM handler "
        "while the main thread is also in printf(). Corruption or deadlock results.");
    asyncHint->setWordWrap(true);
    asyncHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    asyncL->addWidget(asyncHint);
    asyncDemoBtn = new QPushButton("⚡  Run Async-Signal-Safety Demo");
    asyncDemoBtn->setStyleSheet(Theme::btnDanger());
    asyncL->addWidget(asyncDemoBtn);
    rightLayout->addWidget(asyncCard);

    // Signal log
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(12,10,12,10);
    logLayout->setSpacing(4);

    auto* logTitle = new QLabel("Signal Log");
    logTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    logLayout->addWidget(logTitle);

    signalLog = new QTextEdit();
    signalLog->setReadOnly(true);
    signalLog->setStyleSheet(QString(
        "QTextEdit{background:%1;border:1px solid %2;border-radius:8px;"
        "font-family:Consolas;font-size:11px;color:%3;padding:6px;}"
    ).arg(Theme::BG_INPUT).arg(Theme::BORDER).arg(Theme::TEXT_PRIMARY));
    logLayout->addWidget(signalLog);
    rightLayout->addWidget(logCard, 1);

    mainRow->addWidget(rightPanel, 3);
    outer->addLayout(mainRow, 1);

    // Connections
    connect(sendBtn,    &QPushButton::clicked,
            this, &SignalPanel::onSendSignal);
    connect(spawnBtn,   &QPushButton::clicked,
            this, &SignalPanel::onSpawnTarget);
    connect(killBtn,    &QPushButton::clicked,
            this, &SignalPanel::onKillTarget);
    connect(asyncDemoBtn, &QPushButton::clicked,
            this, &SignalPanel::onAsyncDemo);
    connect(signalBox,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SignalPanel::onSignalSelected);
    connect(processTable, &QTableWidget::cellClicked,
            this, &SignalPanel::onTargetSelected);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &SignalPanel::onRefreshTargets);
    refreshTimer->start(2000);

    // Init signal description
    onSignalSelected(0);
    onRefreshTargets();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}" + Theme::scrollbar());
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(scroll);
}

SignalPanel::~SignalPanel() {
    if (asyncDemoPid > 0) {
        kill(asyncDemoPid, SIGKILL);
        waitpid(asyncDemoPid, nullptr, WNOHANG);
    }
    for (pid_t pid : ownTargets) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, WNOHANG);
    }
}

void SignalPanel::setSandboxPids(const std::vector<pid_t>& pids) {
    sandboxPids = pids;
    onRefreshTargets();
}

void SignalPanel::onSignalSelected(int index) {
    if (index < 0 || index >= (int)SIGNALS.size()) return;
    const SignalInfo& s = SIGNALS[index];

    QColor actionColor =
        QString(s.defaultAction)=="Terminate" ? QColor(Theme::RED) :
        QString(s.defaultAction)=="Core"      ? QColor(Theme::RED) :
        QString(s.defaultAction)=="Stop"      ? QColor(Theme::ORANGE) :
        QString(s.defaultAction)=="Continue"  ? QColor(Theme::GREEN) :
                                                 QColor(Theme::TEXT_MUTED);

    signalDescLabel->setText(QString(
        "<b>%1</b> (signal %2)<br>"
        "Default action: <span style='color:%3;font-weight:bold;'>%4</span><br>"
        "%5"
    ).arg(s.name).arg(s.number).arg(actionColor.name()).arg(s.defaultAction).arg(s.fullDesc));

    emit explanationNeeded(QString(
        "<b>%1 — Signal %2</b><br><br>"
        "%3<br><br>"
        "<b>Default action:</b> <b style='color:%4;'>%5</b><br><br>"
        "%6"
    ).arg(s.name).arg(s.number).arg(s.fullDesc)
     .arg(actionColor.name()).arg(s.defaultAction)
     .arg(
        s.number==SIGKILL ?
            "SIGKILL is special — the kernel handles it directly. "
            "The process has no way to intercept or ignore it. "
            "Use it only when SIGTERM fails." :
        s.number==SIGSTOP ?
            "SIGSTOP is also unblockable like SIGKILL. "
            "It freezes the process in place. SIGCONT unfreezes it. "
            "This is exactly what a debugger does when you set a breakpoint." :
        s.number==SIGTERM ?
            "SIGTERM is the polite way to ask a process to stop. "
            "Well-behaved programs catch it, save state, and exit cleanly. "
            "Always try SIGTERM before SIGKILL." :
        s.number==SIGSEGV ?
            "You can actually send SIGSEGV manually to any process. "
            "It will crash as if it had a real memory error. "
            "This is useful for testing crash handlers." :
            "Fire this at one of your sandbox processes and observe the reaction in real time."
    ));
}

void SignalPanel::onSendSignal() {
    if (selectedPid <= 0) {
        statusLabel->setText("⚠ Select a target process first");
        return;
    }
    int idx = signalBox->currentIndex();
    if (idx < 0 || idx >= (int)SIGNALS.size()) return;
    const SignalInfo& s = SIGNALS[idx];

    // Check process exists before sending
    if (kill(selectedPid, 0) != 0) {
        statusLabel->setText(QString("⚠ PID %1 no longer exists").arg(selectedPid));
        onRefreshTargets();
        return;
    }

    int ret = kill(selectedPid, s.number);
    QString result = ret == 0 ? "delivered" : "failed (permission denied?)";
    EventBus::get().signalSent(selectedPid, s.number, s.name, result);
    logEvent(s.number, selectedPid, result);
    statusLabel->setText(QString("Sent %1 to PID %2 — %3").arg(s.name).arg(selectedPid).arg(result));

    // Wait a moment then refresh to show state change
    QTimer::singleShot(300, this, &SignalPanel::onRefreshTargets);

    // Emit explanation of what just happened
    QString stateAfter = processState(selectedPid);
    emit explanationNeeded(QString(
        "<b>%1 sent to PID %2</b><br><br>"
        "Result: <b>%3</b><br>"
        "Process state after: <b>%4</b><br><br>"
        "%5<br><br>"
        "<b>What the kernel did:</b> Found PID %2 in the process table, "
        "set the pending signal bit, and woke the process if it was sleeping. "
        "The process will handle the signal the next time it runs on a CPU core."
    ).arg(s.name).arg(selectedPid).arg(result).arg(stateAfter).arg(s.fullDesc));
}

void SignalPanel::onRefreshTargets() {
    // Collect all trackable pids: sandbox + own targets
    std::vector<pid_t> allPids;
    for (pid_t p : sandboxPids)  allPids.push_back(p);
    for (pid_t p : ownTargets)   allPids.push_back(p);

    // Remove dead own targets
    ownTargets.erase(
        std::remove_if(ownTargets.begin(), ownTargets.end(),
            [](pid_t p){ return kill(p,0)!=0; }),
        ownTargets.end());

    processTable->setRowCount(0);
    for (pid_t pid : allPids) {
        if (kill(pid, 0) != 0) continue; // already dead

        // Read name from /proc
        std::string name = "unknown";
        std::ifstream f("/proc/"+std::to_string(pid)+"/status");
        std::string line;
        while (std::getline(f,line))
            if (line.rfind("Name:",0)==0) { name=line.substr(6); break; }

        QString state = processState(pid);
        bool isOwn = std::find(ownTargets.begin(),ownTargets.end(),pid)!=ownTargets.end();

        int row = processTable->rowCount();
        processTable->insertRow(row);

        auto cell=[&](const QString& t,const char* c=nullptr){
            auto* i=new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            if(c) i->setForeground(QColor(c));
            i->setData(Qt::UserRole, pid);
            return i;
        };

        processTable->setItem(row,0,cell(QString::number(pid)));
        processTable->setItem(row,1,cell(QString::fromStdString(name)));
        processTable->setItem(row,2,cell(state,
            state.contains("Run")?Theme::GREEN:
            state.contains("Stop")?Theme::ORANGE:
            state.contains("Zombie")?Theme::RED:Theme::TEXT_SECONDARY));
        processTable->setItem(row,3,cell(isOwn?"🎯 Target":"🧪 Sandbox",
            isOwn?Theme::ORANGE:Theme::BLUE));
    }
}

void SignalPanel::onTargetSelected(int row, int) {
    auto* item = processTable->item(row, 0);
    if (!item) return;
    selectedPid = item->data(Qt::UserRole).toInt();
    statusLabel->setText(QString("Target: PID %1 — ready to fire").arg(selectedPid));
    readSignalMasks(selectedPid);
}

void SignalPanel::onSpawnTarget() {
    pid_t pid = fork();
    if (pid == 0) {
        // A signal-aware target process
        // Catches SIGTERM and SIGUSR1, ignores SIGHUP
        signal(SIGTERM, [](int){ /* caught — will show state change */ });
        signal(SIGUSR1, [](int){ /* caught */ });
        signal(SIGHUP,  SIG_IGN);
        // Just loop — visible in process table
        while (true) { sleep(1); }
        _exit(0);
    }
    if (pid > 0) {
        ownTargets.push_back(pid);
        onRefreshTargets();
        emit explanationNeeded(QString(
            "<b>Target process spawned — PID %1</b><br><br>"
            "This process:<br>"
            "• <b>Catches</b> SIGTERM (won't die from it)<br>"
            "• <b>Catches</b> SIGUSR1 (custom handler)<br>"
            "• <b>Ignores</b> SIGHUP<br>"
            "• <b>Cannot block</b> SIGKILL or SIGSTOP<br><br>"
            "Try firing different signals and watch its state change.<br>"
            "SIGSTOP will freeze it. SIGCONT unfreezes. SIGKILL always kills."
        ).arg(pid));
    }
}

void SignalPanel::onKillTarget() {
    if (selectedPid <= 0) return;
    kill(selectedPid, SIGKILL);
    waitpid(selectedPid, nullptr, WNOHANG);
    ownTargets.erase(
        std::remove(ownTargets.begin(),ownTargets.end(),selectedPid),
        ownTargets.end());
    selectedPid = -1;
    QTimer::singleShot(300, this, &SignalPanel::onRefreshTargets);
}

void SignalPanel::logEvent(int signum, pid_t pid, const QString& result) {
    const char* name = "SIG?";
    for (auto& s : SIGNALS) if (s.number==signum) { name=s.name; break; }

    QString entry = QString("[%1] %2 → PID %3 : %4")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(name).arg(pid).arg(result);

    signalLog->append(entry);
    eventLog.push_back({QDateTime::currentDateTime().toString("hh:mm:ss"),
                        signum, pid, result});
}

QString SignalPanel::processState(pid_t pid) {
    std::ifstream f("/proc/"+std::to_string(pid)+"/status");
    std::string line;
    while (std::getline(f,line)) {
        if (line.rfind("State:",0)==0) {
            char st = line.size()>7 ? line[7] : '?';
            switch(st) {
                case 'R': return "Running";
                case 'S': return "Sleeping";
                case 'T': return "Stopped";
                case 'Z': return "Zombie";
                case 'D': return "Disk Wait";
                default:  return QString(st);
            }
        }
    }
    return "Gone";
}

// ── Signal mask reading + async-signal-safety demo ───────────────────────────

void SignalPanel::readSignalMasks(pid_t pid) {
    if (pid <= 0) return;
    std::ifstream f("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    QString sigPnd, sigBlk, sigCgt;
    while (std::getline(f, line)) {
        if (line.rfind("SigPnd:", 0) == 0) sigPnd = QString::fromStdString(line.substr(7)).trimmed();
        if (line.rfind("SigBlk:", 0) == 0) sigBlk = QString::fromStdString(line.substr(7)).trimmed();
        if (line.rfind("SigCgt:", 0) == 0) sigCgt = QString::fromStdString(line.substr(7)).trimmed();
    }
    if (sigPnd.isEmpty()) {
        signalMaskLabel->setText(QString("Cannot read /proc/%1/status — process may be gone").arg(pid));
        return;
    }
    // Decode which signals are set in each mask
    auto decode = [](const QString& hex) -> QString {
        bool ok; unsigned long long mask = hex.toULongLong(&ok, 16);
        if (!ok) return hex;
        QStringList names;
        // Common signals (1-31)
        static const char* snames[] = {
            "", "HUP","INT","QUIT","ILL","TRAP","ABRT","BUS","FPE","KILL",
            "USR1","SEGV","USR2","PIPE","ALRM","TERM","STKFLT","CHLD","CONT",
            "STOP","TSTP","TTIN","TTOU","URG","XCPU","XFSZ","VTALRM","PROF",
            "WINCH","IO","PWR","SYS"
        };
        for (int i = 1; i <= 31; i++) {
            if (mask & (1ULL << (i - 1))) names.append(QString("SIG") + snames[i]);
        }
        if (names.isEmpty()) return "none";
        return names.join(", ");
    };

    signalMaskLabel->setText(QString(
        "PID %1\n"
        "SigPnd (pending): %2\n"
        "SigBlk (blocked): %3\n"
        "SigCgt (caught):  %4"
    ).arg(pid)
     .arg(decode(sigPnd))
     .arg(decode(sigBlk))
     .arg(decode(sigCgt)));
}

void SignalPanel::onAsyncDemo() {
    // Kill any existing demo
    if (asyncDemoPid > 0) {
        kill(asyncDemoPid, SIGKILL);
        waitpid(asyncDemoPid, nullptr, WNOHANG);
        asyncDemoPid = -1;
    }

    // Fork a child that demonstrates async-signal-safety violation:
    // It installs a SIGALRM handler that calls printf() (non-async-signal-safe)
    // while the main loop is also calling printf(). This can cause deadlock or
    // corruption because printf() uses an internal non-reentrant lock.
    pid_t pid = fork();
    if (pid == 0) {
        // Install SIGALRM handler that calls printf — NOT async-signal-safe
        signal(SIGALRM, [](int) {
            // printf uses a global lock — calling from signal handler while main
            // thread holds that lock = deadlock or corruption
            printf("[SIGALRM handler] called from signal — printf in handler!\n");
            fflush(stdout);
        });
        alarm(1); // fire SIGALRM in 1 second

        // Main loop also calls printf repeatedly
        for (int i = 0; ; i++) {
            printf("Main loop iteration %d (unsafe printf)\n", i);
            fflush(stdout);
            usleep(100000); // 100ms
        }
        _exit(0);
    }
    if (pid > 0) {
        asyncDemoPid = pid;
        ownTargets.push_back(pid);
        onRefreshTargets();
        signalLog->append(QString("[ASYNC DEMO] Spawned PID %1 — printf() from SIGALRM handler").arg(pid));

        emit explanationNeeded(QString(
            "<b>Async-Signal-Safety Violation Demo</b><br><br>"
            "PID %1 was spawned with:<br>"
            "1. A main loop calling <code>printf()</code> every 100ms<br>"
            "2. A SIGALRM handler that <b>also</b> calls <code>printf()</code><br><br>"
            "<code>printf()</code> uses an internal global lock (<code>flockfile()</code>). "
            "If the SIGALRM handler fires <b>while the main thread holds that lock</b>, "
            "the handler tries to re-acquire it — but the same thread can't acquire a "
            "non-reentrant lock it already holds → <b>deadlock</b>.<br><br>"
            "<b>The rule:</b> Signal handlers must only call <b>async-signal-safe</b> functions. "
            "The only safe I/O function is <code>write()</code> (syscall directly). "
            "Never call: <code>printf, malloc, free, mutex_lock</code>, or any libc I/O from a handler.<br><br>"
            "<b>Correct fix:</b> Set a volatile flag in the handler and check it in the main loop."
        ).arg(pid));
    }
}
