#include "EbpfLab.h"
#include "EventBus.h"
#include "Theme.h"
#include <QHeaderView>
#include <QPainterPath>
#include <QFile>
#include <QFileInfo>
#include <QTextCursor>
#include <QRegularExpression>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>

// ── PerfChart ─────────────────────────────────────────────────────────────────

PerfChart::PerfChart(QWidget* p) : QWidget(p) {
    setMinimumHeight(100);
    setStyleSheet(QString("background:white;border-radius:10px;border:1px solid %1;").arg(Theme::BORDER));
}

void PerfChart::clear() { series.clear(); maxVal = 1; update(); }

void PerfChart::addSample(const QString& name, long delta) {
    for (auto& s : series) {
        if (s.name == name) {
            s.samples.append(delta);
            if (s.samples.size() > 60) s.samples.removeFirst();
            if (delta > maxVal) maxVal = delta;
            update();
            return;
        }
    }
    static const QColor pal[] = {
        QColor("#4F6EF7"),QColor("#22C55E"),QColor("#F97316"),QColor("#A855F7"),
        QColor("#EF4444"),QColor("#14B8A6"),QColor("#EAB308"),QColor("#EC4899")
    };
    Series s;
    s.name = name;
    s.samples.append(delta);
    s.color = pal[series.size() % 8];
    series.append(s);
    update();
}

void PerfChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (series.isEmpty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI",10));
        p.drawText(rect(), Qt::AlignCenter, "Start perf counters to see data here.");
        return;
    }

    int w = width(), h = height(), pad = 8;
    int chartH = h - 24 - pad;

    for (auto& s : series) {
        if (s.samples.isEmpty()) continue;
        int n = s.samples.size();
        float xStep = (float)(w - pad*2) / std::max(n-1, 1);
        QPainterPath path;
        for (int i=0;i<n;i++) {
            float x = pad + i * xStep;
            float y = pad + chartH - (float)s.samples[i]/maxVal * chartH;
            if (i==0) path.moveTo(x,y); else path.lineTo(x,y);
        }
        p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(path);
    }

    // Legend
    int lx = pad, ly = h - 14;
    for (auto& s : series) {
        p.setPen(s.color);
        p.setFont(QFont("Segoe UI",7,QFont::Bold));
        QRect r(lx, ly, 8, 8);
        QPainterPath rp; rp.addRoundedRect(r,2,2);
        p.fillPath(rp, s.color);
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(lx+10, ly+8, s.name.left(12));
        lx += 14 + p.fontMetrics().horizontalAdvance(s.name.left(12)) + 10;
    }
}

// ── EbpfLab ───────────────────────────────────────────────────────────────────

static long perf_event_open(struct perf_event_attr* hw_event, pid_t pid,
                             int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

EbpfLab::EbpfLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🔬  Observability Lab — perf counters & ftrace");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● KERNEL INTERFACE");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::TEAL).arg(Theme::TEAL_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Uses <b>perf_event_open()</b> for hardware/software counters and "
        "<b>/sys/kernel/debug/tracing</b> for kernel function tracing. "
        "No external tools needed — direct syscall and filesystem interfaces.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    // Top row: perf + ftrace controls side by side
    auto* topRow = new QHBoxLayout(); topRow->setSpacing(10);

    // Perf counters card
    auto* perfCard = new QWidget(); perfCard->setStyleSheet(Theme::card());
    auto* perfL = new QVBoxLayout(perfCard); perfL->setContentsMargins(12,10,12,10);
    auto* perfTitle = new QLabel("Hardware & Software Counters");
    perfTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    perfL->addWidget(perfTitle);

    auto* perfHint = new QLabel("Reads CPU instruction counts, cache misses, context switches, and page faults via <b>perf_event_open()</b> syscall. System-wide (pid=-1).");
    perfHint->setWordWrap(true);
    perfHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    perfL->addWidget(perfHint);

    auto* perfBtnRow = new QHBoxLayout();
    startPerfBtn = new QPushButton("▶ Start Counters");
    startPerfBtn->setStyleSheet(Theme::btnPrimary());
    stopPerfBtn = new QPushButton("■ Stop");
    stopPerfBtn->setStyleSheet(Theme::btnDanger());
    stopPerfBtn->setEnabled(false);
    perfBtnRow->addWidget(startPerfBtn); perfBtnRow->addWidget(stopPerfBtn);
    perfL->addLayout(perfBtnRow);
    topRow->addWidget(perfCard);

    // Ftrace card
    auto* ftraceCard = new QWidget(); ftraceCard->setStyleSheet(Theme::card());
    auto* ftraceL = new QVBoxLayout(ftraceCard); ftraceL->setContentsMargins(12,10,12,10);
    auto* ftraceTitle = new QLabel("Kernel Function Tracer (ftrace)");
    ftraceTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    ftraceL->addWidget(ftraceTitle);

    probeBox = new QComboBox();
    probeBox->addItem("sched_switch — every context switch");
    probeBox->addItem("sys_enter_read — every read() syscall");
    probeBox->addItem("sys_enter_write — every write() syscall");
    probeBox->addItem("sys_enter_mmap — every mmap() syscall");
    probeBox->addItem("kmalloc — every kernel malloc");
    probeBox->addItem("raw_syscalls:sys_enter — ALL syscalls (flamegraph aggregation)");
    probeBox->setStyleSheet(Theme::input());
    ftraceL->addWidget(probeBox);

    auto* ftraceBtnRow = new QHBoxLayout();
    startFtraceBtn = new QPushButton("▶ Start Trace");
    startFtraceBtn->setStyleSheet(Theme::btnSuccess());
    stopFtraceBtn = new QPushButton("■ Stop Trace");
    stopFtraceBtn->setStyleSheet(Theme::btnDanger());
    stopFtraceBtn->setEnabled(false);
    ftraceBtnRow->addWidget(startFtraceBtn); ftraceBtnRow->addWidget(stopFtraceBtn);
    ftraceL->addLayout(ftraceBtnRow);
    topRow->addWidget(ftraceCard);
    outer->addLayout(topRow);

    // Counter table
    auto* counterCard = new QWidget(); counterCard->setStyleSheet(Theme::card());
    auto* ctL = new QVBoxLayout(counterCard); ctL->setContentsMargins(12,10,12,10);
    auto* ctTitle = new QLabel("Live Counter Values");
    ctTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    ctL->addWidget(ctTitle);

    counterTable = new QTableWidget(0, 3);
    counterTable->setHorizontalHeaderLabels({"Counter","Total","Rate/sec"});
    counterTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    counterTable->verticalHeader()->setVisible(false);
    counterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    counterTable->setFixedHeight(140);
    counterTable->setStyleSheet(Theme::table());
    ctL->addWidget(counterTable);
    outer->addWidget(counterCard);

    // Perf chart
    auto* chartCard = new QWidget(); chartCard->setStyleSheet(Theme::card());
    auto* chL = new QVBoxLayout(chartCard); chL->setContentsMargins(12,10,12,10);
    auto* chTitle = new QLabel("Counter Rate Chart (delta per second)");
    chTitle->setStyleSheet(QString("color:%1;font-size:11px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    chL->addWidget(chTitle);
    perfChart = new PerfChart();
    chL->addWidget(perfChart);
    outer->addWidget(chartCard);

    // Trace log
    auto* logCard = new QWidget(); logCard->setStyleSheet(Theme::card());
    auto* ll = new QVBoxLayout(logCard); ll->setContentsMargins(12,10,12,10);
    auto* logTitle = new QLabel("Kernel Trace Events");
    logTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    ll->addWidget(logTitle);
    traceLog = new QTextEdit();
    traceLog->setReadOnly(true);
    traceLog->setMinimumHeight(110);
    traceLog->setMaximumHeight(160);
    traceLog->setStyleSheet(Theme::termLog());
    ll->addWidget(traceLog);
    outer->addWidget(logCard);

    statusLabel = new QLabel("Ready — start perf counters or a trace probe above.");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    connect(startPerfBtn,   &QPushButton::clicked, this, &EbpfLab::onStartPerfCounters);
    connect(stopPerfBtn,    &QPushButton::clicked, this, &EbpfLab::onStopPerfCounters);
    connect(startFtraceBtn, &QPushButton::clicked, this, &EbpfLab::onStartFtrace);
    connect(stopFtraceBtn,  &QPushButton::clicked, this, &EbpfLab::onStopFtrace);
    connect(probeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EbpfLab::onProbeChanged);

    onProbeChanged(0);
}

EbpfLab::~EbpfLab() {
    closePerfCounters();
    disableFtrace();
}

// ── Perf counters ─────────────────────────────────────────────────────────────

bool EbpfLab::openPerfCounters() {
    closePerfCounters();
    counters.clear();

    struct CounterDef { uint32_t type; uint64_t config; const char* name; };
    static const CounterDef defs[] = {
        {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,  "instructions"},
        {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,  "cache_misses"},
        {PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, "ctx_switches"},
        {PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS,   "page_faults"},
        {PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,     "cpu_clock_ns"},
    };

    bool anyOk = false;
    for (auto& def : defs) {
        struct perf_event_attr attr = {};
        attr.type           = def.type;
        attr.size           = sizeof(attr);
        attr.config         = def.config;
        attr.disabled       = 1;
        attr.exclude_kernel = 0;
        attr.exclude_hv     = 1;

        int fd = (int)perf_event_open(&attr, -1, 0, -1, 0);
        if (fd < 0) {
            // Try with exclude_kernel=1 (unprivileged)
            attr.exclude_kernel = 1;
            fd = (int)perf_event_open(&attr, -1, 0, -1, 0);
        }

        PerfCounter c;
        c.name  = def.name;
        c.value = 0;
        c.delta = 0;
        c.fd    = fd;
        if (fd >= 0) {
            ioctl(fd, PERF_EVENT_IOC_RESET,  0);
            ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
            anyOk = true;
        }
        counters.append(c);
    }
    return anyOk;
}

void EbpfLab::closePerfCounters() {
    for (auto& c : counters) if (c.fd >= 0) { ::close(c.fd); c.fd = -1; }
    if (perfTimer) { perfTimer->stop(); delete perfTimer; perfTimer = nullptr; }
}

void EbpfLab::onStartPerfCounters() {
    if (!openPerfCounters()) {
        QString paranoid = "unknown";
        std::ifstream pf("/proc/sys/kernel/perf_event_paranoid");
        if (pf) { std::string v; pf >> v; paranoid = QString::fromStdString(v); }

        statusLabel->setText(QString("⚠ Could not open perf events (perf_event_paranoid=%1). "
            "Try: sudo setcap cap_perfmon+ep build/LearnOS").arg(paranoid));
        emit explanationNeeded(QString(
            "<b>perf_event_open() failed</b><br><br>"
            "Current <code>/proc/sys/kernel/perf_event_paranoid</code> = <b>%1</b>. "
            "This lab requests system-wide counters (pid=-1), which most distros "
            "block above paranoid level 1.<br><br>"
            "Two fixes, either works:<br>"
            "• <code>sudo setcap cap_perfmon+ep build/LearnOS</code> — grant just this binary the capability<br>"
            "• <code>sudo sysctl kernel.perf_event_paranoid=1</code> — loosen system-wide (resets on reboot)"
            ).arg(paranoid));
        return;
    }
    startPerfBtn->setEnabled(false);
    stopPerfBtn->setEnabled(true);
    perfChart->clear();
    statusLabel->setText("Perf counters active — reading system-wide hardware events.");

    perfTimer = new QTimer(this);
    connect(perfTimer, &QTimer::timeout, this, &EbpfLab::onPerfTick);
    perfTimer->start(1000);

    emit explanationNeeded(
        "<b>perf_event_open() — Hardware Performance Counters</b><br><br>"
        "The CPU has built-in registers that count low-level events: instructions executed, "
        "cache misses, branch mispredictions. <code>perf_event_open()</code> is the Linux "
        "syscall to read them.<br><br>"
        "<b>Instructions:</b> Total CPU instructions retired<br>"
        "<b>Cache misses:</b> L3 cache misses (expensive — goes to RAM)<br>"
        "<b>Context switches:</b> How often the scheduler switches processes<br>"
        "<b>Page faults:</b> Virtual memory pages not yet in RAM<br><br>"
        "This is the same data <code>perf stat</code> shows — done via raw syscall.");
}

void EbpfLab::onStopPerfCounters() {
    closePerfCounters();
    startPerfBtn->setEnabled(true);
    stopPerfBtn->setEnabled(false);
    statusLabel->setText("Perf counters stopped.");
}

void EbpfLab::onPerfTick() {
    readCounters();
    refreshCounterTable();

    // Summarise all active counters into a single feed event.
    // valueLong = total instructions (most fundamental counter); extra = summary string.
    long instTotal = 0;
    QStringList parts;
    for (auto& c : counters) {
        if (c.fd < 0) continue;
        parts << QString("%1+%2/s").arg(c.name).arg(c.delta);
        if (c.name == "instructions") instTotal = c.value;
    }
    if (!parts.isEmpty()) {
        OSEvent e;
        e.type      = OSEvent::PerfCounterTick;
        e.detail    = "perf: " + parts.join("  ");
        e.valueLong = instTotal;
        EventBus::get().fire(e);
    }
}

void EbpfLab::readCounters() {
    for (auto& c : counters) {
        if (c.fd < 0) continue;
        uint64_t val = 0;
        if (read(c.fd, &val, sizeof(val)) == sizeof(val)) {
            c.delta = (long)val - c.value;
            c.value = (long)val;
            perfChart->addSample(c.name, c.delta);
        }
    }
}

void EbpfLab::refreshCounterTable() {
    counterTable->setRowCount(0);
    for (auto& c : counters) {
        int row = counterTable->rowCount();
        counterTable->insertRow(row);
        auto cell = [&](const QString& t, const char* col=nullptr){
            auto* i = new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            if (col) i->setForeground(QColor(col));
            return i;
        };
        const char* avail = c.fd >= 0 ? Theme::TEXT_PRIMARY : Theme::TEXT_MUTED;
        counterTable->setItem(row, 0, cell(c.name, avail));
        counterTable->setItem(row, 1, cell(c.fd >= 0 ? QString::number(c.value) : "N/A (no permission)", avail));
        counterTable->setItem(row, 2, cell(c.fd >= 0 ? QString("+%1/s").arg(c.delta) : "—",
                                          c.delta > 0 ? Theme::GREEN : Theme::TEXT_MUTED));
    }
}

// ── Ftrace ────────────────────────────────────────────────────────────────────

QString EbpfLab::enableFtrace(const QString& probe) {
    // Find tracefs
    static const QStringList tracefsRoots = {
        "/sys/kernel/debug/tracing",
        "/sys/kernel/tracing"
    };
    QString tracefsRoot;
    for (auto& r : tracefsRoots) {
        if (QFileInfo::exists(r + "/trace_pipe")) { tracefsRoot = r; break; }
    }
    if (tracefsRoot.isEmpty()) {
        return "debugfs/tracefs isn't mounted. Run: "
               "sudo mount -t debugfs none /sys/kernel/debug";
    }

    // Map probe selection to event filter.
    // IMPORTANT: /sys/kernel/tracing/set_event expects "subsystem:event"
    // (colon) — NOT "subsystem/event" (slash). The slash form is only used
    // for filesystem paths under events/<subsystem>/<event>/. Writing the
    // wrong separator here made the kernel silently reject every write,
    // leaving set_event empty with no error surfaced anywhere.
    static const char* events[] = {
        "sched:sched_switch",
        "syscalls:sys_enter_read",
        "syscalls:sys_enter_write",
        "syscalls:sys_enter_mmap",
        "kmem:kmalloc",
    };

    // Enable tracing — if this silently fails, tracing_on stays 0 and no
    // events will ever appear even though everything else "succeeds".
    QFile tracingOn(tracefsRoot + "/tracing_on");
    if (!tracingOn.open(QIODevice::WriteOnly)) {
        return QString("found %1 but can't write tracing_on (permission denied). "
                        "This process needs root or CAP_SYS_ADMIN — try running "
                        "with sudo, or: sudo setcap cap_sys_admin+ep build/LearnOS")
                        .arg(tracefsRoot);
    }
    tracingOn.write("1"); tracingOn.close();

    QFile setEvent(tracefsRoot + "/set_event");
    if (!setEvent.open(QIODevice::WriteOnly)) {
        return QString("found %1 but can't write set_event (permission denied). "
                        "Same fix as above — needs root or CAP_SYS_ADMIN.")
                        .arg(tracefsRoot);
    }

    int idx = probeBox->currentIndex();
    const char* ev = (idx >= 0 && idx < 6) ? events[idx] : events[0];
    qint64 written = setEvent.write(ev);
    setEvent.close();
    if (written <= 0) {
        return QString("wrote '%1' to set_event but the kernel rejected it (0 bytes accepted). "
                        "The event name/syntax may not exist on this kernel version.")
                        .arg(ev);
    }

    // Verify the write actually stuck — set_event can silently accept-but-drop
    // an event name the kernel doesn't recognize, so read it back rather than
    // trusting a non-zero write() return alone.
    QFile verifyEvent(tracefsRoot + "/set_event");
    if (verifyEvent.open(QIODevice::ReadOnly)) {
        QByteArray current = verifyEvent.readAll().trimmed();
        verifyEvent.close();
        if (current.isEmpty()) {
            return QString("wrote '%1' to set_event but it reads back empty — "
                            "the kernel didn't actually enable it. This event may "
                            "not exist on your kernel build.").arg(ev);
        }
    }

    // Open trace_pipe for non-blocking reads
    tracePipeFd = open((tracefsRoot + "/trace_pipe").toLocal8Bit().constData(),
                       O_RDONLY | O_NONBLOCK);
    if (tracePipeFd < 0) {
        return QString("set_event succeeded but trace_pipe won't open (errno %1: %2). "
                        "Needs root or CAP_SYS_ADMIN to read it.")
                        .arg(errno).arg(strerror(errno));
    }

    // Use QTimer to poll (pipe doesn't work well with QSocketNotifier on all kernels)
    auto* pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &EbpfLab::onFtraceReady);
    pollTimer->start(200);
    traceNotifier = (QSocketNotifier*)pollTimer; // store ref for cleanup

    return QString(); // success
}

void EbpfLab::disableFtrace() {
    if (traceNotifier) {
        auto* t = qobject_cast<QTimer*>(traceNotifier);
        if (t) { t->stop(); delete t; }
        traceNotifier = nullptr;
    }
    if (tracePipeFd >= 0) { ::close(tracePipeFd); tracePipeFd = -1; }

    // Disable tracing
    static const QStringList roots = {"/sys/kernel/debug/tracing","/sys/kernel/tracing"};
    for (auto& r : roots) {
        QString p = r + "/set_event";
        if (QFileInfo::exists(p)) {
            QFile f(p); if(f.open(QIODevice::WriteOnly)){f.write("");f.close();}
            break;
        }
    }
    ftraceActive = false;
}

void EbpfLab::onStartFtrace() {
    QString err = enableFtrace(probeBox->currentText());
    if (!err.isEmpty()) {
        statusLabel->setText("⚠ ftrace failed: " + err);
        emit explanationNeeded(QString(
            "<b>ftrace couldn't start</b><br><br>"
            "%1<br><br>"
            "This is a permission problem, not a missing feature — the kernel "
            "infrastructure is there, the process just isn't allowed to use it "
            "yet.").arg(err));
        return;
    }
    ftraceActive = true;
    startFtraceBtn->setEnabled(false);
    stopFtraceBtn->setEnabled(true);
    statusLabel->setText("ftrace active — kernel events streaming from trace_pipe.");
    emit explanationNeeded(QString(
        "<b>ftrace — Kernel Function Tracer</b><br><br>"
        "Tracing: <b>%1</b><br><br>"
        "ftrace writes to a ring buffer inside the kernel. "
        "We read from <code>/sys/kernel/debug/tracing/trace_pipe</code> — "
        "every kernel event matching the filter appears here in real time.<br><br>"
        "This is what <code>trace-cmd</code> and <code>perf trace</code> use internally. "
        "No agent, no overhead when disabled — pure kernel instrumentation."
    ).arg(probeBox->currentText()));
}

void EbpfLab::onStopFtrace() {
    disableFtrace();
    startFtraceBtn->setEnabled(true);
    stopFtraceBtn->setEnabled(false);
    statusLabel->setText("ftrace stopped.");
}

void EbpfLab::onFtraceReady() {
    if (tracePipeFd < 0) return;
    char buf[8192];
    ssize_t n = read(tracePipeFd, buf, sizeof(buf)-1);
    if (n <= 0) return;
    buf[n] = '\0';

    QString text = QString::fromLocal8Bit(buf);

    // For raw_syscalls mode: aggregate syscall IDs into a frequency table
    // and display as a flamegraph-style sorted bar list instead of raw lines.
    bool isRawSyscalls = (probeBox->currentIndex() == 5);
    if (isRawSyscalls) {
        // Parse lines like: ... sys_enter: NR=N ...
        // ftrace raw_syscalls format: "  proc-PID [CPU] ... sys_enter: NR N args..."
        static QMap<int, long> syscallFreq;
        static const char* x86_64_syscalls[] = {
            "read","write","open","close","stat","fstat","lstat","poll","lseek","mmap",
            "mprotect","munmap","brk","rt_sigaction","rt_sigprocmask","rt_sigreturn","ioctl",
            "pread64","pwrite64","readv","writev","access","pipe","select","sched_yield",
            "mremap","msync","mincore","madvise","shmget","shmat","shmctl","dup","dup2",
            "pause","nanosleep","getitimer","alarm","setitimer","getpid","sendfile","socket",
            "connect","accept","sendto","recvfrom","sendmsg","recvmsg","shutdown","bind",
            "listen","getsockname","getpeername","socketpair","setsockopt","getsockopt",
            "clone","fork","vfork","execve","exit","wait4","kill","uname","semget","semop",
            "semctl","shmdt","msgget","msgsnd","msgrcv","msgctl","fcntl","flock","fsync",
            "fdatasync","truncate","ftruncate","getdents","getcwd","chdir","fchdir","rename",
            "mkdir","rmdir","creat","link","unlink","symlink","readlink","chmod","fchmod",
            "chown","fchown","lchown","umask","gettimeofday","getrlimit","getrusage",
            "sysinfo","times","ptrace","getuid","syslog","getgid","setuid","setgid",
            "geteuid","getegid","setpgid","getppid","getpgrp","setsid","setreuid",
            "setregid","getgroups","setgroups","setresuid","getresuid","setresgid",
            "getresgid","getpgid","setfsuid","setfsgid","getsid","capget","capset",
            "rt_sigpending","rt_sigtimedwait","rt_sigqueueinfo","rt_sigsuspend",
            "sigaltstack","utime","mknod","uselib","personality","ustat","statfs",
            "fstatfs","sysfs","getpriority","setpriority","sched_setparam",
            "sched_getparam","sched_setscheduler","sched_getscheduler","sched_get_priority_max",
            "sched_get_priority_min","sched_rr_get_interval","mlock","munlock","mlockall",
            "munlockall","vhangup","modify_ldt","pivot_root","_sysctl","prctl","arch_prctrl",
            "adjtimex","setrlimit","chroot","sync","acct","settimeofday","mount","umount2",
            "swapon","swapoff","reboot","sethostname","setdomainname","iopl","ioperm",
            "create_module","init_module","delete_module","get_kernel_syms","query_module",
            "quotactl","nfsservctl","getpmsg","putpmsg","afs_syscall","tuxcall","security",
            "gettid","readahead","setxattr","lsetxattr","fsetxattr","getxattr","lgetxattr",
            "fgetxattr","listxattr","llistxattr","flistxattr","removexattr","lremovexattr",
            "fremovexattr","tkill","time","futex","sched_setaffinity","sched_getaffinity"
        };
        constexpr int N_SYSCALLS = sizeof(x86_64_syscalls)/sizeof(*x86_64_syscalls);

        for (auto& line : text.split('\n')) {
            // Look for "NR N" or "nr=N" or "id=N" in the line
            int nrIdx = line.indexOf(" NR=");
            if (nrIdx < 0) nrIdx = line.indexOf(" id=");
            if (nrIdx < 0) {
                // Try to find the NR field from sched raw format: "sys_enter: NR 59 ..."
                // ftrace format has "sys_enter: NR 59" etc.
                int seIdx = line.indexOf("sys_enter: NR ");
                if (seIdx >= 0) {
                    QString rest = line.mid(seIdx + 14);
                    bool ok; int nr = rest.split(' ').first().toInt(&ok);
                    if (ok) syscallFreq[nr]++;
                }
                continue;
            }
            QString rest = line.mid(nrIdx + 4);
            bool ok; int nr = rest.split(QRegularExpression("[^0-9]")).first().toInt(&ok);
            if (ok && nr >= 0) syscallFreq[nr]++;
        }

        // Build flamegraph-style HTML sorted by frequency
        if (!syscallFreq.isEmpty()) {
            // Sort by count descending
            QVector<QPair<int,long>> sorted;
            for (auto it = syscallFreq.begin(); it != syscallFreq.end(); ++it)
                sorted.append({it.key(), it.value()});
            std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.second > b.second; });
            long maxCount = sorted.first().second;

            QString html = "<b>Syscall Frequency (top 20)</b><br>";
            int shown = 0;
            for (auto& [nr, cnt] : sorted) {
                if (++shown > 20) break;
                int barW = (int)(200.0 * cnt / maxCount);
                const char* name = (nr >= 0 && nr < N_SYSCALLS) ? x86_64_syscalls[nr] : "unknown";
                html += QString("<code>%1</code> [%2]  ")
                    .arg(QString(name).leftJustified(20))
                    .arg(QString::number(cnt).rightJustified(8));
                html += QString("<span style='background:#4F6EF7;display:inline-block;width:%1px;height:8px;'>&nbsp;</span>").arg(barW);
                html += "<br>";
            }
            traceLog->setHtml(html);
            return;  // don't append raw lines for raw_syscalls mode
        }
    }

    // Fire a single aggregated event per read batch so the activity feed
    // shows ftrace activity without flooding it with one entry per line.
    {
        int lineCount = text.count('\n') + 1;
        OSEvent e;
        e.type   = OSEvent::FtraceEvent;
        e.detail = QString("ftrace [%1]: %2 events")
                       .arg(probeBox->currentText().section(' ', 0, 0))
                       .arg(lineCount);
        e.valueLong = lineCount;
        EventBus::get().fire(e);
    }

    // Normal mode: append raw lines
    traceLog->append(text.trimmed());
    QStringList lines = traceLog->toPlainText().split('\n');
    if (lines.size() > 200) {
        traceLog->setPlainText(lines.mid(lines.size()-200).join('\n'));
        auto cursor = traceLog->textCursor();
        cursor.movePosition(QTextCursor::End);
        traceLog->setTextCursor(cursor);
    }
}

void EbpfLab::onProbeChanged(int idx) {
    static const char* exps[] = {
        "<b>sched_switch</b><br><br>"
        "Fires every time the kernel scheduler switches from one process/thread to another. "
        "You'll see: <code>prev_comm -&gt; next_comm, pid, prio, state</code>. "
        "This reveals context switch rates — high rates = lots of switching overhead.",

        "<b>sys_enter_read</b><br><br>"
        "Fires every time any process calls the <code>read()</code> syscall. "
        "You'll see which process, which file descriptor, and how many bytes. "
        "Watch your I/O worker's read calls stream in real time.",

        "<b>sys_enter_write</b><br><br>"
        "Every <code>write()</code> syscall — process, fd, count. "
        "Your I/O sandbox process will light this up. "
        "Terminal keystrokes also appear here (fd=1, count=1).",

        "<b>sys_enter_mmap</b><br><br>"
        "Every <code>mmap()</code> call. Memory allocations, file mappings, "
        "shared memory, executable loading — all use mmap. "
        "Start a process and watch its startup mmap calls.",

        "<b>kmalloc</b><br><br>"
        "Every kernel memory allocation. Shows size and call site. "
        "This requires CAP_SYS_ADMIN and debugfs. High frequency = "
        "kernel is very busy allocating internal structures.",
    };
    if (idx >= 0 && idx < 5) emit explanationNeeded(exps[idx]);
}
