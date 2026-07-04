#include "SandboxManager.h"
#include "EventBus.h"
#include "Theme.h"
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <QTimer>

// ── seccomp-bpf ───────────────────────────────────────────────────────────────
// We include the kernel seccomp headers directly. These are available on any
// Linux system with linux-libc-dev / kernel-headers installed.
#ifdef __linux__
#  include <linux/seccomp.h>
#  include <linux/filter.h>
#  include <linux/audit.h>
// AUDIT_ARCH_X86_64 might need this on older headers:
#  ifndef AUDIT_ARCH_X86_64
#    define AUDIT_ARCH_X86_64 (EM_X86_64 | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE)
#  endif
#endif

// Helper: install a minimal seccomp-bpf filter that blocks the write() syscall.
// Returns 0 on success, -1 on error.
static int installSeccompBlockWrite() {
#ifdef __linux__
    // BPF program:
    //   1. Load arch word
    //   2. If not x86-64 → kill (safety)
    //   3. Load syscall NR
    //   4. If NR == SYS_write → return ERRNO(EPERM)
    //   5. Otherwise → allow
    struct sock_filter filter[] = {
        // Validate architecture
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (offsetof(struct seccomp_data, arch))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
        // Check syscall number
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (offsetof(struct seccomp_data, nr))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 0, 1),
        BPF_STMT(BPF_RET | BPF_K,
                 SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = {
        .len    = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };
    // No-new-privs is required before SECCOMP_SET_MODE_FILTER
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return -1;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) return -1;
    return 0;
#else
    return -1;
#endif
}

SandboxManager::SandboxManager(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16,16,16,16);
    layout->setSpacing(12);

    // Title
    auto* title = new QLabel("🧪  Experimental Sandbox");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* hint = new QLabel("Spawn real Linux processes and watch the system react. Click a process to inspect its memory.");
    hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_SECONDARY));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // Spawn buttons
    auto* spawnCard = new QWidget();
    spawnCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
    auto* spawnLayout = new QVBoxLayout(spawnCard);
    spawnLayout->setContentsMargins(14,12,14,12);
    spawnLayout->setSpacing(8);

    auto* spawnTitle = new QLabel("Spawn a Process");
    spawnTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    spawnLayout->addWidget(spawnTitle);

    auto* spawnRow = new QHBoxLayout();
    auto* cpuBtn = new QPushButton("🔥  CPU Burner");
    auto* memBtn = new QPushButton("💾  Memory Eater");
    auto* ioBtn  = new QPushButton("💿  I/O Worker");

    cpuBtn->setStyleSheet(
        "QPushButton { background: #FEF2F2; color: #EF4444; border: 1px solid #FECACA;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #FEE2E2; }");
    memBtn->setStyleSheet(
        "QPushButton { background: #EFF6FF; color: #3B82F6; border: 1px solid #BFDBFE;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #DBEAFE; }");
    ioBtn->setStyleSheet(
        "QPushButton { background: #F0FDF4; color: #22C55E; border: 1px solid #BBF7D0;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #DCFCE7; }");

    spawnRow->addWidget(cpuBtn);
    spawnRow->addWidget(memBtn);
    spawnRow->addWidget(ioBtn);
    spawnLayout->addLayout(spawnRow);
    layout->addWidget(spawnCard);

    // Process list card
    auto* listCard = new QWidget();
    listCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(14,12,14,12);
    listLayout->setSpacing(8);

    auto* listTitle = new QLabel("Active Sandbox Processes");
    listTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    listLayout->addWidget(listTitle);

    processList = new QListWidget();
    processList->setStyleSheet(QString(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 8px;"
        "  font-size: 12px; color: %3; }"
        "QListWidget::item { padding: 6px 10px; border-radius: 6px; margin: 1px; }"
        "QListWidget::item:selected { background: %4; color: %5; }"
        "QListWidget::item:hover { background: %6; }"
    ).arg(Theme::BG_INPUT).arg(Theme::BORDER).arg(Theme::TEXT_PRIMARY)
     .arg(Theme::BLUE_LIGHT).arg(Theme::BLUE).arg(Theme::BG_APP));
    processList->setFixedHeight(140);
    listLayout->addWidget(processList);

    // Control buttons
    auto* ctrlRow = new QHBoxLayout();
    auto* pauseBtn  = new QPushButton("⏸  Pause");
    auto* resumeBtn = new QPushButton("▶  Resume");
    auto* killBtn   = new QPushButton("✕  Kill");
    pauseBtn->setStyleSheet(Theme::btnGhost());
    resumeBtn->setStyleSheet(Theme::btnSuccess());
    killBtn->setStyleSheet(Theme::btnDanger());
    ctrlRow->addWidget(pauseBtn);
    ctrlRow->addWidget(resumeBtn);
    ctrlRow->addWidget(killBtn);
    listLayout->addLayout(ctrlRow);
    layout->addWidget(listCard);

    // Scheduling card
    auto* schedCard = new QWidget();
    schedCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
    auto* schedLayout = new QVBoxLayout(schedCard);
    schedLayout->setContentsMargins(14,12,14,12);
    schedLayout->setSpacing(8);

    auto* schedTitle = new QLabel("Apply Scheduling Algorithm");
    schedTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    schedLayout->addWidget(schedTitle);

    algorithmBox = new QComboBox();
    algorithmBox->addItems({
        "FCFS — First Come First Served",
        "SJF — Shortest Job First",
        "Round Robin — Equal time slices",
        "Priority — By nice value"
    });
    algorithmBox->setStyleSheet(Theme::input());
    schedLayout->addWidget(algorithmBox);

    auto* applyBtn = new QPushButton("▶  Apply to Sandbox Processes");
    applyBtn->setStyleSheet(Theme::btnPrimary());
    schedLayout->addWidget(applyBtn);
    layout->addWidget(schedCard);

    // ── seccomp-bpf demo card ─────────────────────────────────────────────────
    auto* seccompCard = new QWidget();
    seccompCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;").arg(Theme::BORDER));
    auto* seccompLayout = new QVBoxLayout(seccompCard);
    seccompLayout->setContentsMargins(14,12,14,12);
    seccompLayout->setSpacing(6);

    auto* seccompTitle = new QLabel("🔒  seccomp-bpf — Syscall Filter Demo");
    seccompTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    seccompLayout->addWidget(seccompTitle);

    auto* seccompHint = new QLabel(
        "Spawns a child with a BPF filter that blocks <code>write()</code>. "
        "The child tries <code>write()</code> and gets <b>EPERM</b>. "
        "This is exactly how Docker/gVisor restrict container syscalls via "
        "<code>prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)</code>.");
    seccompHint->setWordWrap(true);
    seccompHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    seccompLayout->addWidget(seccompHint);

    auto* seccompBtn = new QPushButton("▶  Run seccomp-bpf Demo");
    seccompBtn->setStyleSheet(Theme::btnPrimary());
    seccompLayout->addWidget(seccompBtn);

    seccompLog = new QTextEdit();
    seccompLog->setReadOnly(true);
    seccompLog->setFixedHeight(80);
    seccompLog->setStyleSheet(Theme::termLog());
    seccompLayout->addWidget(seccompLog);
    layout->addWidget(seccompCard);

    // ── cgroup v2 demo card ───────────────────────────────────────────────────
    auto* cgroupCard = new QWidget();
    cgroupCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;").arg(Theme::BORDER));
    auto* cgroupLayout = new QVBoxLayout(cgroupCard);
    cgroupLayout->setContentsMargins(14,12,14,12);
    cgroupLayout->setSpacing(6);

    auto* cgroupTitle = new QLabel("📊  cgroup v2 — Live Resource Accounting");
    cgroupTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    cgroupLayout->addWidget(cgroupTitle);

    auto* cgroupHint = new QLabel(
        "Reads <code>memory.current</code> and <code>cpu.stat</code> from the "
        "current process's cgroup. This is how the kernel tracks per-container "
        "memory and CPU usage in Docker / systemd-managed services.");
    cgroupHint->setWordWrap(true);
    cgroupHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    cgroupLayout->addWidget(cgroupHint);

    auto* cgroupBtn = new QPushButton("🔄  Read My cgroup Stats");
    cgroupBtn->setStyleSheet(Theme::btnPrimary());
    cgroupLayout->addWidget(cgroupBtn);

    cgroupLabel = new QLabel("Press the button to read live cgroup v2 data from /sys/fs/cgroup/");
    cgroupLabel->setWordWrap(true);
    cgroupLabel->setStyleSheet(QString(
        "font-family:Consolas;font-size:10px;color:%1;background:%2;"
        "border-radius:6px;padding:6px;border:1px solid %3;")
        .arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    cgroupLabel->setMinimumHeight(60);
    cgroupLayout->addWidget(cgroupLabel);
    layout->addWidget(cgroupCard);

    layout->addStretch();

    connect(cpuBtn,   &QPushButton::clicked, this, &SandboxManager::spawnCPU);
    connect(memBtn,   &QPushButton::clicked, this, &SandboxManager::spawnMemory);
    connect(ioBtn,    &QPushButton::clicked, this, &SandboxManager::spawnIO);
    connect(killBtn,  &QPushButton::clicked, this, &SandboxManager::killSelected);
    connect(pauseBtn, &QPushButton::clicked, this, &SandboxManager::pauseSelected);
    connect(resumeBtn,&QPushButton::clicked, this, &SandboxManager::resumeSelected);
    connect(applyBtn, &QPushButton::clicked, this, &SandboxManager::applyScheduling);
    connect(algorithmBox, &QComboBox::currentTextChanged, this, &SandboxManager::explainAlgorithm);
    connect(processList, &QListWidget::itemClicked, this, &SandboxManager::onRowClicked);
    connect(seccompBtn, &QPushButton::clicked, this, &SandboxManager::onSeccompDemo);
    connect(cgroupBtn,  &QPushButton::clicked, this, &SandboxManager::onCgroupDemo);

    // Periodically reap any zombies from signal-killed processes
    auto* zombieTimer = new QTimer(this);
    connect(zombieTimer, &QTimer::timeout, this, &SandboxManager::onZombieReap);
    zombieTimer->start(1500);
}

SandboxManager::~SandboxManager() {
    if (seccompDemoPid > 0) {
        kill(seccompDemoPid, SIGKILL);
        waitpid(seccompDemoPid, nullptr, WNOHANG);
    }
    for (auto& p : processes) { kill(p.pid, SIGKILL); waitpid(p.pid, nullptr, WNOHANG); }
}

void SandboxManager::spawnCPU()    { spawn(WorkloadType::CPU); }
void SandboxManager::spawnMemory() { spawn(WorkloadType::MEMORY); }
void SandboxManager::spawnIO()     { spawn(WorkloadType::IO); }

void SandboxManager::spawn(WorkloadType type) {
    static int counter = 1;
    QString typeName = type==WorkloadType::CPU?"cpu_burn":type==WorkloadType::MEMORY?"mem_eat":"io_work";
    QString name = typeName + "_" + QString::number(counter++);

    pid_t pid = fork();
    if (pid == 0) {
        nice(19); // lowest priority — can't freeze system
        if (type == WorkloadType::CPU) {
            volatile double x = 1.0;
            while (true) x = std::sin(x)*std::cos(x)+1.0001;
        } else if (type == WorkloadType::MEMORY) {
            const size_t chunk = 50*1024*1024;
            while (true) {
                char* mem = (char*)malloc(chunk);
                if (mem) for (size_t i=0;i<chunk;i+=4096) mem[i]=(char)(i&0xFF);
                sleep(3); free(mem);
            }
        } else {
            char path[] = "/tmp/learnos_io_XXXXXX";
            int fd = mkstemp(path);
            if (fd<0) _exit(1);
            char buf[4096]; memset(buf,0xAB,sizeof(buf));
            while (true) {
                for (int i=0;i<256;i++) write(fd,buf,sizeof(buf));
                lseek(fd,0,SEEK_SET);
                while (read(fd,buf,sizeof(buf))>0) {}
                lseek(fd,0,SEEK_SET); fsync(fd);
            }
        }
        _exit(0);
    } else if (pid > 0) {
        SandboxProcess sp; sp.pid=pid; sp.name=name.toStdString();
        sp.type=type; sp.priority=0; sp.paused=false;
        processes.push_back(sp);
        refreshList();
        emitPids();
        EventBus::get().processSpawned(pid, QString::fromStdString(name.toStdString()), typeName);

        QString typeDesc = type==WorkloadType::CPU?"🔥 CPU Burner":
                           type==WorkloadType::MEMORY?"💾 Memory Eater":"💿 I/O Worker";
        QString impact   = type==WorkloadType::CPU?"running a tight math loop (sin/cos) forever":
                           type==WorkloadType::MEMORY?"allocating and touching 50MB of real RAM every 3s":
                           "writing and reading 1MB to disk in a loop";
        QString watch    = type==WorkloadType::CPU?"Watch a CPU core bar turn orange/red on the heatmap.":
                           type==WorkloadType::MEMORY?"Watch memory blocks fill up on the heatmap.":
                           "Watch this process flicker to disk-wait state in the process slots.";

        emit explanationNeeded(QString(
            "<b>%1 spawned!</b> &nbsp;"
            "<span style='background:%2;color:%3;border-radius:6px;padding:2px 8px;font-size:10px;'>PID %4</span>"
            "<br><br>This is a real Linux process %5.<br><br>%6<br><br>"
            "Pause it with ⏸ and watch the heatmap cool down immediately."
        ).arg(typeDesc).arg(Theme::GREEN_LIGHT).arg(Theme::GREEN).arg(pid)
         .arg(impact).arg(watch));
    }
}

void SandboxManager::killSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGKILL); waitpid(pid, nullptr, WNOHANG);
    EventBus::get().processKilled(pid, QString::fromStdString(
        [&]{ for(auto&p:processes) if(p.pid==pid) return p.name; return std::string("?"); }()
    ));
    processes.erase(std::remove_if(processes.begin(), processes.end(),
        [pid](const SandboxProcess& p){ return p.pid==pid; }), processes.end());
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process killed</b> (PID %1)<br><br>"
        "SIGKILL sent — the kernel forcibly removed it. "
        "All memory returned to the OS immediately.<br><br>"
        "Watch the heatmap — CPU load or memory usage should drop within one refresh."
    ).arg(pid));
}

void SandboxManager::pauseSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGSTOP);
    EventBus::get().processPaused(pid);
    for (auto& p : processes) if (p.pid==pid) p.paused=true;
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process paused</b> (PID %1)<br><br>"
        "SIGSTOP sent. The kernel freezes this process — zero CPU, holds memory.<br><br>"
        "<i>Watch a CPU core cool down on the heatmap if this was a CPU burner.</i>"
    ).arg(pid));
}

void SandboxManager::resumeSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGCONT);
    EventBus::get().processResumed(pid);
    for (auto& p : processes) if (p.pid==pid) p.paused=false;
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process resumed</b> (PID %1)<br><br>"
        "SIGCONT sent. Back on the run queue.<br><br>"
        "<i>Watch the heatmap heat up again.</i>"
    ).arg(pid));
}

void SandboxManager::applyScheduling() {
    if (processes.empty()) {
        emit explanationNeeded("<b>No sandbox processes.</b><br>Spawn some first.");
        return;
    }
    QString algo = algorithmBox->currentText();
    if (algo.startsWith("Round Robin")) {
        for (auto& p : processes) { setpriority(PRIO_PROCESS,p.pid,0); p.priority=0; }
    } else if (algo.startsWith("Priority")) {
        for (int i=0;i<(int)processes.size();i++) {
            int nice=i*4; setpriority(PRIO_PROCESS,processes[i].pid,nice);
            processes[i].priority=nice;
        }
    }
    refreshList(); explainAlgorithm(algo);
}

void SandboxManager::explainAlgorithm(const QString& algo) {
    QString explanation;
    if (algo.startsWith("FCFS")) {
        explanation="<b>FCFS — First Come First Served</b><br><br>"
            "Processes run in arrival order. No interruptions once started.<br><br>"
            "<b>Problem:</b> One slow process blocks everything behind it (convoy effect).<br>"
            "<b>Real use:</b> Batch jobs, print queues.";
    } else if (algo.startsWith("SJF")) {
        explanation="<b>SJF — Shortest Job First</b><br><br>"
            "Always run the process expected to finish fastest.<br><br>"
            "<b>Problem:</b> You can't know burst time in advance. Long jobs starve.<br>"
            "<b>Real use:</b> Approximated using past CPU burst history.";
    } else if (algo.startsWith("Round Robin")) {
        explanation="<b>Round Robin</b><br><br>"
            "Each process gets equal time slices. After each slice → back of queue.<br><br>"
            "All sandbox processes set to <b>nice=0</b> (equal priority).<br>"
            "<b>Real use:</b> Linux's CFS is based on this. Your system uses it right now.";
    } else if (algo.startsWith("Priority")) {
        explanation="<b>Priority Scheduling</b><br><br>"
            "Lowest nice value = highest priority = most CPU time.<br><br>"
            "Your processes are now nice 0, 4, 8, 12...<br>"
            "<b>Problem:</b> Low-priority processes can starve forever.";
    }
    emit explanationNeeded(explanation);
}

// ── seccomp-bpf demo ──────────────────────────────────────────────────────────

void SandboxManager::onSeccompDemo() {
    // Kill any previous demo child
    if (seccompDemoPid > 0) {
        kill(seccompDemoPid, SIGKILL);
        waitpid(seccompDemoPid, nullptr, WNOHANG);
        seccompDemoPid = -1;
    }

    // Pipe: child sends us its result string before dying
    int pipefd[2];
    if (pipe(pipefd) != 0) { seccompLog->append("pipe() failed"); return; }

    pid_t pid = fork();
    if (pid == 0) {
        // ── child ───────────────────────────────────────────────────────────
        ::close(pipefd[0]);
        int wfd = pipefd[1];

        // Install seccomp-bpf filter that returns EPERM on write()
        if (installSeccompBlockWrite() != 0) {
            const char* msg = "SECCOMP_INSTALL_FAILED\n";
            // Use the raw syscall — write() itself is about to be blocked
            syscall(SYS_write, wfd, msg, strlen(msg));
            ::close(wfd);
            _exit(1);
        }

        // Now try write() — the filter should block it with EPERM
        const char* testMsg = "hello via write()";
        ssize_t ret = write(wfd, testMsg, strlen(testMsg));

        // If we reach here, write was not blocked
        char result[128];
        if (ret < 0) {
            snprintf(result, sizeof(result),
                     "write() returned %zd, errno=%d (%s) — BLOCKED by seccomp-bpf\n",
                     ret, errno, strerror(errno));
        } else {
            snprintf(result, sizeof(result),
                     "write() unexpectedly succeeded, ret=%zd\n", ret);
        }
        syscall(SYS_write, wfd, result, strlen(result));
        ::close(wfd);
        _exit(0);
    }

    // ── parent ───────────────────────────────────────────────────────────────
    ::close(pipefd[1]);
    if (pid < 0) {
        ::close(pipefd[0]);
        seccompLog->append("fork() failed");
        return;
    }
    seccompDemoPid = pid;

    // Read child's report
    char buf[512] = {};
    ssize_t nr = read(pipefd[0], buf, sizeof(buf)-1);
    ::close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    seccompDemoPid = -1;

    QString output = (nr > 0) ? QString::fromLocal8Bit(buf, (int)nr).trimmed()
                               : "(no output — process was killed by SIGSYS)";

    // Check if child was killed by SIGSYS (seccomp default action = KILL)
    bool killedBySigsys = WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS;
    if (killedBySigsys) {
        output = "Process killed by SIGSYS — seccomp SECCOMP_RET_KILL in effect\n"
                 "(filter was: kill the process on blocked syscall)";
    }

    seccompLog->append(QString(
        "<span style='color:#94A3B8;'>[seccomp demo PID %1]</span><br>"
        "<span style='color:#4ADE80;'>%2</span>"
    ).arg(pid).arg(output.toHtmlEscaped()));

    emit explanationNeeded(QString(
        "<b>seccomp-bpf Demo — PID %1</b><br><br>"
        "A child process was forked and given a BPF filter via:<br>"
        "<code>prctl(PR_SET_NO_NEW_PRIVS, 1)</code> — required first<br>"
        "<code>prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)</code><br><br>"
        "The BPF program:<br>"
        "1. Checks the CPU architecture (safety check)<br>"
        "2. If syscall NR == <code>__NR_write</code> → return <code>SECCOMP_RET_ERRNO(EPERM)</code><br>"
        "3. Otherwise → <code>SECCOMP_RET_ALLOW</code><br><br>"
        "Result: <b>%2</b><br><br>"
        "This is exactly how Docker's seccomp profiles work — a JSON list of allowed "
        "syscalls gets compiled to a BPF program and attached to every container process. "
        "Kubernetes also supports per-pod seccomp profiles."
    ).arg(pid).arg(output.toHtmlEscaped()));
}

// ── cgroup v2 demo ────────────────────────────────────────────────────────────

void SandboxManager::onCgroupDemo() {
    // Find our own cgroup path from /proc/self/cgroup
    // Format: "0::<cgroup-path>"
    std::string cgroupPath;
    {
        std::ifstream f("/proc/self/cgroup");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0::", 0) == 0) {
                cgroupPath = line.substr(3); // strip "0::"
                break;
            }
        }
    }
    if (cgroupPath.empty()) {
        cgroupLabel->setText("Could not find cgroup v2 path in /proc/self/cgroup\n"
                             "(system may use cgroup v1 or not be cgroup-namespaced)");
        return;
    }

    QString basePath = QString("/sys/fs/cgroup") + QString::fromStdString(cgroupPath);

    // Read memory.current
    auto readFile = [](const QString& path) -> QString {
        std::ifstream f(path.toLocal8Bit().constData());
        if (!f) return "(not available)";
        std::string val;
        std::getline(f, val);
        return QString::fromStdString(val).trimmed();
    };

    QString memoryCurrent = readFile(basePath + "/memory.current");
    QString memoryPeak    = readFile(basePath + "/memory.peak");
    QString memoryHigh    = readFile(basePath + "/memory.high");
    QString memoryMax     = readFile(basePath + "/memory.max");

    // cpu.stat — parse usage_usec and nr_periods
    QString cpuUsageUsec, cpuNrPeriods, cpuNrThrottled;
    {
        std::ifstream f((basePath + "/cpu.stat").toLocal8Bit().constData());
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("usage_usec", 0) == 0)
                cpuUsageUsec = QString::fromStdString(line.substr(11)).trimmed();
            else if (line.rfind("nr_periods", 0) == 0)
                cpuNrPeriods = QString::fromStdString(line.substr(11)).trimmed();
            else if (line.rfind("nr_throttled", 0) == 0)
                cpuNrThrottled = QString::fromStdString(line.substr(13)).trimmed();
        }
    }
    if (cpuUsageUsec.isEmpty()) cpuUsageUsec = "(not available)";
    if (cpuNrPeriods.isEmpty()) cpuNrPeriods = "(not available)";
    if (cpuNrThrottled.isEmpty()) cpuNrThrottled = "(not available)";

    // Convert memory.current to human-readable
    auto humanBytes = [](const QString& s) -> QString {
        bool ok; long long b = s.toLongLong(&ok);
        if (!ok || b < 0) return s;
        if (b >= 1024*1024*1024) return QString("%1 GiB").arg((double)b/(1024*1024*1024), 0,'f',2);
        if (b >= 1024*1024)      return QString("%1 MiB").arg((double)b/(1024*1024), 0,'f',1);
        if (b >= 1024)           return QString("%1 KiB").arg(b/1024);
        return QString("%1 B").arg(b);
    };

    // Convert cpu usage_usec to ms
    auto cpuUsageMs = [](const QString& s) -> QString {
        bool ok; long long us = s.toLongLong(&ok);
        if (!ok) return s;
        return QString("%1 ms").arg(us / 1000);
    };

    QString text = QString(
        "cgroup path: %1\n\n"
        "── memory ──────────────────\n"
        "memory.current:  %2  (%3)\n"
        "memory.peak:     %4  (%5)\n"
        "memory.high:     %6\n"
        "memory.max:      %7\n\n"
        "── cpu ─────────────────────\n"
        "usage_usec:      %8  (%9)\n"
        "nr_periods:      %10\n"
        "nr_throttled:    %11"
    ).arg(basePath)
     .arg(memoryCurrent).arg(humanBytes(memoryCurrent))
     .arg(memoryPeak).arg(humanBytes(memoryPeak))
     .arg(memoryHigh)
     .arg(memoryMax)
     .arg(cpuUsageUsec).arg(cpuUsageMs(cpuUsageUsec))
     .arg(cpuNrPeriods)
     .arg(cpuNrThrottled);

    cgroupLabel->setText(text);

    emit explanationNeeded(QString(
        "<b>cgroup v2 — Live Resource Accounting</b><br><br>"
        "Path: <code>%1</code><br><br>"
        "<b>memory.current</b> = %2 bytes (%3) — RAM currently used by this cgroup<br>"
        "<b>memory.peak</b> = %4 bytes (%5) — highest watermark since cgroup created<br>"
        "<b>memory.max</b> = %6 — hard limit (max means unlimited)<br><br>"
        "<b>cpu.stat usage_usec</b> = %7 µs (%8) — total CPU time consumed<br>"
        "<b>nr_throttled</b> = %9 — how many times this cgroup was CPU-throttled<br><br>"
        "Docker uses cgroup v2 to enforce <code>--memory</code> and <code>--cpus</code> "
        "limits. The kernel will OOM-kill processes in the cgroup if memory.current "
        "exceeds memory.max. Throttling happens when cpu.max quota is exhausted."
    ).arg(basePath)
     .arg(memoryCurrent).arg(humanBytes(memoryCurrent))
     .arg(memoryPeak).arg(humanBytes(memoryPeak))
     .arg(memoryMax)
     .arg(cpuUsageUsec).arg(cpuUsageMs(cpuUsageUsec))
     .arg(cpuNrThrottled));
}

void SandboxManager::refreshList() {
    processList->clear();
    for (const auto& p : processes) {
        QString icon = p.type==WorkloadType::CPU?"🔥":p.type==WorkloadType::MEMORY?"💾":"💿";
        QString label = QString("%1  [%2]  %3  %4")
            .arg(icon).arg(p.pid)
            .arg(QString::fromStdString(p.name))
            .arg(p.paused?"⏸ paused":"▶ running");
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, p.pid);
        if (p.paused) item->setForeground(QColor(Theme::ORANGE));
        else item->setForeground(QColor(Theme::TEXT_PRIMARY));
        processList->addItem(item);
    }
}

void SandboxManager::emitPids() {
    std::vector<pid_t> pids;
    for (auto& p : processes) pids.push_back(p.pid);
    emit processesChanged(pids);
}

void SandboxManager::onRowClicked(QListWidgetItem* item) {
    pid_t pid = item->data(Qt::UserRole).toInt();
    emit processSelected(pid);
    emit explanationNeeded(QString(
        "<b>Selected PID %1</b><br><br>"
        "Switch to 🧠 <b>Memory</b> tab to see this process's full memory map live.<br>"
        "Switch to ⚙ <b>Scheduler</b> tab to step through scheduling algorithms with this process."
    ).arg(pid));
}

std::vector<pid_t> SandboxManager::sandboxPids() const {
    std::vector<pid_t> pids;
    for (const auto& p : processes) pids.push_back(p.pid);
    return pids;
}

// Called by MainWindow when SignalPanel fires a signal at a PID.
// If that PID belongs to us and it's now dead, remove it from our list.
void SandboxManager::checkProcessAlive(pid_t pid) {
    auto it = std::find_if(processes.begin(), processes.end(),
        [pid](const SandboxProcess& p){ return p.pid == pid; });
    if (it == processes.end()) return; // not our process

    // Give the signal a short moment to be delivered, then check
    QTimer::singleShot(400, this, [this, pid]() {
        bool dead = (kill(pid, 0) != 0);
        if (dead) {
            waitpid(pid, nullptr, WNOHANG); // reap zombie
            processes.erase(std::remove_if(processes.begin(), processes.end(),
                [pid](const SandboxProcess& p){ return p.pid == pid; }), processes.end());
            refreshList();
            emitPids();
        }
    });
}

// Periodic zombie reaper: removes any sandbox processes that died from
// external signals (e.g. sent from SignalPanel or the OS).
void SandboxManager::onZombieReap() {
    bool changed = false;
    processes.erase(std::remove_if(processes.begin(), processes.end(),
        [&changed](const SandboxProcess& p) {
            if (kill(p.pid, 0) != 0) {
                waitpid(p.pid, nullptr, WNOHANG);
                changed = true;
                return true;
            }
            return false;
        }), processes.end());
    if (changed) {
        refreshList();
        emitPids();
    }
}
