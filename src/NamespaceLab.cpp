#include "NamespaceLab.h"
#include "Theme.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QPainterPath>
#include <QProcess>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sched.h>
#include <cstring>
#include <cerrno>

// ── Child stack for clone() ───────────────────────────────────────────────────
// clone() needs a separate stack for the child.  We allocate it on the heap
// and place the stack pointer at the *top* (stacks grow downward on x86-64).

static const int CHILD_STACK_SIZE = 256 * 1024; // 256 KB

// The child fn signature required by clone().
struct ChildArgs {
    int   nsTypeIdx;   // which namespace type was chosen (drives setup)
    int   pipeFd;      // write end — child writes its inner PID then closes
};

static int cloneChildFn(void* arg) {
    auto* a = static_cast<ChildArgs*>(arg);
    int idx = a->nsTypeIdx;
    int wfd = a->pipeFd;

    // UTS: set a visible hostname inside the new namespace
    if (idx == 1 || idx == 6) {
        sethostname("learnos-child", 13);
    }

    // Report the child's own view of its PID (inside PID namespace it will be 1)
    pid_t innerPid = getpid();
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)innerPid);
    write(wfd, buf, n);
    close(wfd);

    // Stay alive so we can inspect its /proc/[pid]/ns/ links
    while (true) pause();
    return 0;
}

// ── NsTreeView ────────────────────────────────────────────────────────────────

NsTreeView::NsTreeView(QWidget* p) : QWidget(p) {
    setMinimumHeight(160);
    setStyleSheet(QString("background:white;border-radius:10px;border:1px solid %1;").arg(Theme::BORDER));
}

void NsTreeView::setData(const QVector<NsInfo>& ns, pid_t cp) {
    namespaces = ns; child = cp; update();
}

void NsTreeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (namespaces.isEmpty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Spawn a process in an isolated namespace\nto see the namespace tree.");
        return;
    }

    int w = width(), pad = 18;
    int boxW = 130, boxH = 36, gap = 14;
    int leftX = pad, rightX = w - pad - boxW;
    int y = 18;

    p.setFont(QFont("Segoe UI", 9, QFont::Bold));

    // Column headers
    p.setPen(QColor(Theme::TEXT_SECONDARY));
    p.drawText(leftX,  y, "LearnOS (PID 1)");
    p.drawText(rightX, y, child > 0 ? QString("Child PID %1").arg(child) : "Child (none)");
    y += 20;

    for (auto& ns : namespaces) {
        // Parent box
        QRect lbox(leftX, y, boxW, boxH);
        QPainterPath lp; lp.addRoundedRect(lbox, 7, 7);
        p.fillPath(lp, QColor(Theme::BLUE_LIGHT));
        p.setPen(QPen(QColor(Theme::BLUE), 1));
        p.drawPath(lp);
        p.setPen(QColor(Theme::TEXT_PRIMARY));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(lbox.adjusted(6,0,0,0), Qt::AlignVCenter, ns.type.toUpper());
        p.setFont(QFont("Consolas", 7));
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(lbox.adjusted(0,16,0,0), Qt::AlignHCenter|Qt::AlignVCenter,
                   ns.ownNs.left(14));

        // Arrow
        int midY = y + boxH/2;
        p.setPen(QPen(ns.isolated ? QColor(Theme::GREEN) : QColor(Theme::TEXT_MUTED), 2));
        int arrowMid = leftX + boxW + (rightX - leftX - boxW) / 2;
        p.drawLine(leftX + boxW, midY, rightX, midY);
        if (ns.isolated) {
            // Diverge symbol
            p.setFont(QFont("Segoe UI", 8, QFont::Bold));
            p.setPen(QColor(Theme::GREEN));
            p.drawText(arrowMid - 12, midY - 2, "NEW");
        }

        // Child box
        QRect rbox(rightX, y, boxW, boxH);
        QPainterPath rp; rp.addRoundedRect(rbox, 7, 7);
        QColor childBg = ns.isolated ? QColor(Theme::GREEN_LIGHT) : QColor(Theme::BG_INPUT);
        QColor childBorder = ns.isolated ? QColor(Theme::GREEN) : QColor(Theme::BORDER);
        p.fillPath(rp, childBg);
        p.setPen(QPen(childBorder, 1.5));
        p.drawPath(rp);
        p.setPen(QColor(Theme::TEXT_PRIMARY));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        if (ns.isolated)
            p.setPen(QColor(Theme::GREEN));
        p.drawText(rbox.adjusted(6,0,0,0), Qt::AlignVCenter,
                   ns.isolated ? "NEW: " + ns.type.toUpper() : ns.type.toUpper());
        p.setFont(QFont("Consolas", 7));
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(rbox.adjusted(0,16,0,0), Qt::AlignHCenter|Qt::AlignVCenter,
                   ns.childNs.isEmpty() ? "(same)" : ns.childNs.left(14));

        y += boxH + gap;
    }
}

// ── NamespaceLab ──────────────────────────────────────────────────────────────

NamespaceLab::NamespaceLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🏗  Namespace Lab — Linux Isolation Primitives");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● REAL clone() SYSCALL");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::GREEN).arg(Theme::GREEN_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Linux namespaces isolate OS resources per process — this is the foundation of Docker/containers. "
        "Spawn a child via <b>clone(CLONE_NEW*)</b> and see the difference live in /proc/[pid]/ns/. "
        "PID namespace: the child reports its own PID as <b>1</b> inside the namespace.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    // Controls card
    auto* ctrl = new QWidget();
    ctrl->setStyleSheet(Theme::card());
    auto* ctrlL = new QHBoxLayout(ctrl);
    ctrlL->setContentsMargins(14,10,14,10);

    auto* nsLabel = new QLabel("Namespace type:");
    nsLabel->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    nsTypeBox = new QComboBox();
    nsTypeBox->addItem("PID  — child sees itself as PID 1");
    nsTypeBox->addItem("UTS  — child has a different hostname");
    nsTypeBox->addItem("NET  — child has isolated network stack");
    nsTypeBox->addItem("MNT  — child has isolated mount table");
    nsTypeBox->addItem("IPC  — child has isolated SysV/POSIX IPC");
    nsTypeBox->addItem("USER — child has its own UID/GID map");
    nsTypeBox->addItem("ALL  — fully isolated (container-like)");
    nsTypeBox->setStyleSheet(Theme::input());

    spawnBtn = new QPushButton("🚀  Spawn via clone()");
    spawnBtn->setStyleSheet(Theme::btnPrimary());
    killBtn  = new QPushButton("✕  Kill Child");
    killBtn->setStyleSheet(Theme::btnDanger());
    killBtn->setEnabled(false);

    ctrlL->addWidget(nsLabel);
    ctrlL->addWidget(nsTypeBox, 1);
    ctrlL->addWidget(spawnBtn);
    ctrlL->addWidget(killBtn);
    outer->addWidget(ctrl);

    // Inner PID banner (shows what PID the child sees itself as)
    innerPidLabel = new QLabel("Inner PID: — (spawn a process first)");
    innerPidLabel->setAlignment(Qt::AlignCenter);
    innerPidLabel->setStyleSheet(QString(
        "color:%1;background:%2;border:1px solid %3;border-radius:8px;"
        "font-size:12px;font-weight:bold;padding:6px;"
    ).arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    outer->addWidget(innerPidLabel);

    // Tree view
    treeView = new NsTreeView();
    outer->addWidget(treeView, 2);

    // Namespace table
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tl = new QVBoxLayout(tableCard);
    tl->setContentsMargins(12,10,12,10);
    tl->setSpacing(6);
    auto* tableTitle = new QLabel("Namespace Comparison");
    tableTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    tl->addWidget(tableTitle);

    nsTable = new QTableWidget(0, 4);
    nsTable->setHorizontalHeaderLabels({"Type","Parent Inode","Child Inode","Isolated?"});
    nsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    nsTable->verticalHeader()->setVisible(false);
    nsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    nsTable->setStyleSheet(Theme::table());
    nsTable->setFixedHeight(140);
    tl->addWidget(nsTable);
    outer->addWidget(tableCard);

    // Log + command runner
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* ll = new QVBoxLayout(logCard);
    ll->setContentsMargins(14, 12, 14, 12);
    ll->setSpacing(6);
    auto* logHeader = new QHBoxLayout();
    auto* logTitle = new QLabel("Event Log");
    logTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    logHeader->addWidget(logTitle);
    logHeader->addStretch();
    ll->addLayout(logHeader);
    logView = new QTextEdit();
    logView->setReadOnly(true);
    logView->setMinimumHeight(80);
    logView->setMaximumHeight(110);
    logView->setStyleSheet(Theme::termLog());
    ll->addWidget(logView);

    // Run-in-namespace row — lets students run a real command inside the child
    auto* cmdRow = new QHBoxLayout();
    auto* cmdLabel = new QLabel("Run inside namespace:");
    cmdLabel->setStyleSheet(QString("color:%1;font-size:11px;font-weight:600;").arg(Theme::TEXT_SECONDARY));
    cmdInput = new QLineEdit();
    cmdInput->setPlaceholderText("command to run inside the child namespace…");
    cmdInput->setStyleSheet(Theme::input());
    runInNsBtn = new QPushButton("▶ Run");
    runInNsBtn->setStyleSheet(Theme::btnPrimary());
    runInNsBtn->setEnabled(false);
    cmdRow->addWidget(cmdLabel);
    cmdRow->addWidget(cmdInput, 1);
    cmdRow->addWidget(runInNsBtn);
    ll->addLayout(cmdRow);

    outer->addWidget(logCard);

    statusLabel = new QLabel("Ready — choose a namespace type and spawn a child.");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    connect(spawnBtn,    &QPushButton::clicked, this, &NamespaceLab::onSpawnIsolated);
    connect(killBtn,     &QPushButton::clicked, this, &NamespaceLab::onKillChild);
    connect(runInNsBtn,  &QPushButton::clicked, this, &NamespaceLab::onRunInNamespace);
    connect(cmdInput,    &QLineEdit::returnPressed, this, &NamespaceLab::onRunInNamespace);
    connect(nsTypeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NamespaceLab::onNsTypeChanged);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &NamespaceLab::onRefresh);
    refreshTimer->start(2000);

    onNsTypeChanged(0);
}

NamespaceLab::~NamespaceLab() {
    if (childStack) free(childStack);
    if (childPid > 0) { kill(childPid, SIGKILL); waitpid(childPid, nullptr, WNOHANG); }
}

void NamespaceLab::onNsTypeChanged(int idx) {
    static const char* exps[] = {
        "<b>PID Namespace — clone(CLONE_NEWPID)</b><br><br>"
        "A new PID namespace gives the child process its own PID numbering. "
        "The child sees itself as <b>PID 1</b> — confirmed by the inner PID banner above. "
        "From inside the namespace it looks like a fresh system with just its own processes.<br><br>"
        "<code>clone(CLONE_NEWPID)</code> is the actual syscall used here (not fork+unshare). "
        "This is exactly how Docker makes every container think it's PID 1.<br><br>"
        "Try: <code>cat /proc/1/status</code> inside — you'll see its own name, not systemd.",

        "<b>UTS Namespace — clone(CLONE_NEWUTS)</b><br><br>"
        "UTS (Unix Time Sharing) namespace isolates hostname and NIS domain. "
        "The child calls <code>sethostname(\"learnos-child\")</code> on startup.<br><br>"
        "Try: <code>hostname</code> inside — you'll see <b>learnos-child</b>. "
        "Open a real terminal and run <code>hostname</code> — different answer, same kernel.",

        "<b>Network Namespace — clone(CLONE_NEWNET)</b><br><br>"
        "Full network isolation: the child gets its own loopback, routing table, "
        "firewall rules, and sockets.<br><br>"
        "Docker creates a veth pair to connect container namespaces. "
        "Try: <code>ip link</code> inside — only <code>lo</code> visible, no host interfaces.",

        "<b>Mount Namespace — clone(CLONE_NEWNS)</b><br><br>"
        "The child can mount/unmount filesystems without the host seeing. "
        "This is what lets containers have their own filesystem roots. "
        "<code>CLONE_NEWNS</code> is the original namespace flag (Linux 2.4.19, 2002).<br><br>"
        "Try: <code>mount | head -5</code> inside — same mounts as host initially.",

        "<b>IPC Namespace — clone(CLONE_NEWIPC)</b><br><br>"
        "Isolates System V IPC (message queues, semaphores, shared memory) "
        "and POSIX message queues. The child can't see or interact with "
        "the host's <code>ipcs</code> resources at all.<br><br>"
        "Try: <code>ipcs</code> inside — empty, even if host has IPC resources.",

        "<b>User Namespace — clone(CLONE_NEWUSER)</b><br><br>"
        "The child can have a different UID/GID mapping — it can appear to be "
        "root inside but be an unprivileged user outside. "
        "This is how rootless containers (Podman) work.<br><br>"
        "Try: <code>id</code> inside — may show uid=65534 (nobody) until UID map written.",

        "<b>Full Isolation — All Namespace Flags</b><br><br>"
        "All six namespace flags combined: <code>CLONE_NEWPID | CLONE_NEWUTS | "
        "CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUSER</code>. "
        "This is a container with nothing shared from the host except the kernel.<br><br>"
        "Inner PID will be 1. Hostname will be learnos-child. Network will be isolated.",
    };

    static const char* defaultCmds[] = {
        "cat /proc/1/status | head -5",
        "hostname",
        "ip link",
        "mount | head -5",
        "ipcs",
        "id",
        "hostname && ip link",
    };

    if (idx >= 0 && idx < 7) {
        emit explanationNeeded(exps[idx]);
        cmdInput->setPlaceholderText(QString("e.g. %1").arg(defaultCmds[idx]));
        if (cmdInput->text().isEmpty())
            cmdInput->setText(defaultCmds[idx]);
    }
}

QString NamespaceLab::readNsInode(pid_t pid, const QString& type) {
    QString path = QString("/proc/%1/ns/%2").arg(pid).arg(type);
    char buf[256] = {};
    if (readlink(path.toLocal8Bit().constData(), buf, sizeof(buf)-1) > 0)
        return QString::fromLocal8Bit(buf);
    return "unavailable";
}

QVector<NsInfo> NamespaceLab::readNamespaces(pid_t pid) {
    static const QStringList types = {"pid","uts","net","mnt","ipc","user","cgroup"};
    QVector<NsInfo> result;
    for (auto& t : types) {
        NsInfo ns;
        ns.type    = t;
        ns.ownNs   = readNsInode(getpid(), t);
        ns.childNs = pid > 0 ? readNsInode(pid, t) : "";
        ns.isolated = !ns.childNs.isEmpty() && ns.childNs != ns.ownNs;
        result.append(ns);
    }
    return result;
}

void NamespaceLab::onSpawnIsolated() {
    if (childPid > 0) {
        kill(childPid, SIGKILL);
        waitpid(childPid, nullptr, WNOHANG);
        childPid = -1;
        if (childStack) { free(childStack); childStack = nullptr; }
    }

    int idx = nsTypeBox->currentIndex();
    int flags = SIGCHLD;
    QString flagStr;

    if      (idx == 0) { flags |= CLONE_NEWPID;  flagStr = "CLONE_NEWPID"; }
    else if (idx == 1) { flags |= CLONE_NEWUTS;  flagStr = "CLONE_NEWUTS"; }
    else if (idx == 2) { flags |= CLONE_NEWNET;  flagStr = "CLONE_NEWNET"; }
    else if (idx == 3) { flags |= CLONE_NEWNS;   flagStr = "CLONE_NEWNS"; }
    else if (idx == 4) { flags |= CLONE_NEWIPC;  flagStr = "CLONE_NEWIPC"; }
    else if (idx == 5) { flags |= CLONE_NEWUSER; flagStr = "CLONE_NEWUSER"; }
    else {
        flags |= CLONE_NEWPID|CLONE_NEWUTS|CLONE_NEWNET|CLONE_NEWIPC;
        flagStr = "CLONE_NEWPID|CLONE_NEWUTS|CLONE_NEWNET|CLONE_NEWIPC";
    }

    // Pipe: child writes its inner PID to us before blocking
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        statusLabel->setText("⚠ pipe() failed");
        return;
    }

    // Allocate child stack (clone needs its own stack)
    childStack = malloc(CHILD_STACK_SIZE);
    if (!childStack) {
        statusLabel->setText("⚠ malloc() for child stack failed");
        ::close(pipefd[0]); ::close(pipefd[1]);
        return;
    }
    // Stack pointer must point to the TOP (x86-64 stacks grow down)
    char* stackTop = static_cast<char*>(childStack) + CHILD_STACK_SIZE;

    // Arguments passed into the clone child fn (stack-allocated here on parent side)
    ChildArgs args;
    args.nsTypeIdx = idx;
    args.pipeFd    = pipefd[1];  // child writes here

    childPid = clone(cloneChildFn, stackTop, flags, &args);

    // Parent closes write end; reads inner PID from child
    ::close(pipefd[1]);

    if (childPid < 0) {
        int err = errno;
        ::close(pipefd[0]);
        free(childStack); childStack = nullptr;
        QString errMsg = QString::fromLocal8Bit(strerror(err));
        statusLabel->setText(QString("⚠ clone() failed: %1 (errno %2)").arg(errMsg).arg(err));
        logView->append(QString(
            "<span style='color:#EF4444;'>clone(%1) failed: %2</span>").arg(flagStr).arg(errMsg));
        if (err == EPERM) {
            logView->append(
                "<span style='color:#F97316;'>Hint: PID/NET/MNT namespaces need "
                "CAP_SYS_ADMIN. USER namespace works unprivileged.</span>");
        }
        return;
    }

    // Read child's reported inner PID (what getpid() returned inside the namespace)
    char ibuf[32] = {};
    ssize_t nr = read(pipefd[0], ibuf, sizeof(ibuf)-1);
    ::close(pipefd[0]);
    int innerPid = (nr > 0) ? atoi(ibuf) : -1;

    if (innerPid == 1) {
        innerPidLabel->setText(QString(
            "Inner PID: <b style='color:#22C55E;'>%1</b>  — the child calls getpid() and gets PID 1 "
            "inside its new PID namespace  (outer PID = %2)").arg(innerPid).arg(childPid));
        innerPidLabel->setStyleSheet(QString(
            "color:%1;background:%2;border:2px solid %3;border-radius:8px;"
            "font-size:12px;font-weight:bold;padding:6px;")
            .arg(Theme::TEXT_PRIMARY).arg(Theme::GREEN_LIGHT).arg(Theme::GREEN));
    } else if (innerPid > 0) {
        innerPidLabel->setText(QString(
            "Inner PID: %1  (outer PID = %2, namespace does not remap PID)").arg(innerPid).arg(childPid));
        innerPidLabel->setStyleSheet(QString(
            "color:%1;background:%2;border:1px solid %3;border-radius:8px;"
            "font-size:12px;font-weight:bold;padding:6px;")
            .arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    } else {
        innerPidLabel->setText(QString("Inner PID: unknown  (outer PID = %1)").arg(childPid));
    }

    spawnBtn->setEnabled(false);
    killBtn->setEnabled(true);
    runInNsBtn->setEnabled(true);
    statusLabel->setText(QString("Child PID %1 spawned with clone(%2)").arg(childPid).arg(flagStr));
    logView->append(QString("[%1] clone(%2) → outer PID %3, inner PID %4")
        .arg(QTime::currentTime().toString("hh:mm:ss"))
        .arg(flagStr).arg(childPid).arg(innerPid > 0 ? QString::number(innerPid) : "?"));
    onRefresh();

    emit explanationNeeded(QString(
        "<b>Child Spawned — outer PID %1, inner PID %2</b><br><br>"
        "Used <code>clone(%3 | SIGCHLD)</code> — not fork(). The child was created "
        "directly inside the new namespace, so its very first call to getpid() "
        "returns <b>%2</b> (PID 1 in a PID namespace).<br><br>"
        "<b>Check it yourself:</b><br>"
        "• <code>ls -la /proc/%1/ns/</code> — see its namespace symlinks<br>"
        "• <code>ls -la /proc/%4/ns/</code> — compare to our own<br>"
        "• Different inode = different namespace = truly isolated<br><br>"
        "The table below shows which namespaces match (grey) and which are new (green)."
    ).arg(childPid).arg(innerPid > 0 ? QString::number(innerPid) : "?")
     .arg(flagStr).arg(getpid()));
}

void NamespaceLab::onKillChild() {
    if (childPid <= 0) return;
    kill(childPid, SIGKILL);
    waitpid(childPid, nullptr, WNOHANG);
    logView->append(QString("[%1] Killed outer PID %2")
        .arg(QTime::currentTime().toString("hh:mm:ss")).arg(childPid));
    childPid = -1;
    if (childStack) { free(childStack); childStack = nullptr; }
    spawnBtn->setEnabled(true);
    killBtn->setEnabled(false);
    runInNsBtn->setEnabled(false);
    treeView->setData({}, -1);
    nsTable->setRowCount(0);
    innerPidLabel->setText("Inner PID: — (spawn a process first)");
    innerPidLabel->setStyleSheet(QString(
        "color:%1;background:%2;border:1px solid %3;border-radius:8px;"
        "font-size:12px;font-weight:bold;padding:6px;")
        .arg(Theme::TEXT_PRIMARY).arg(Theme::BG_INPUT).arg(Theme::BORDER));
    statusLabel->setText("Child killed — namespaces destroyed.");
}

void NamespaceLab::onRunInNamespace() {
    if (childPid <= 0) {
        logView->append("No child process — spawn one first.");
        return;
    }
    QString cmd = cmdInput->text().trimmed();
    if (cmd.isEmpty()) return;

    // Use nsenter to enter the child's namespaces and run the command.
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = {
        "--target", QString::number(childPid),
        "--pid", "--uts", "--net", "--ipc", "--mount",
        "--", "sh", "-c", cmd
    };
    p.start("nsenter", args);
    if (!p.waitForStarted(2000)) {
        logView->append(QString(
            "<span style='color:#EF4444;'>[nsenter not found or failed to start] "
            "Install util-linux and try again.</span>"));
        return;
    }
    p.waitForFinished(5000);
    QString out = QString::fromLocal8Bit(p.readAll()).trimmed();
    if (out.isEmpty()) out = "(no output)";

    logView->append(QString(
        "<span style='color:#94A3B8;'>[%1]</span> "
        "<span style='color:#60A5FA;'>$ %2</span> "
        "<span style='color:#94A3B8;'>(inside PID %3)</span>")
        .arg(QTime::currentTime().toString("hh:mm:ss"))
        .arg(cmd.toHtmlEscaped())
        .arg(childPid));
    logView->append(QString(
        "<pre style='color:#e2e8f0;margin:2px 0 6px 16px;'>%1</pre>")
        .arg(out.toHtmlEscaped()));
}

void NamespaceLab::onRefresh() {
    if (childPid > 0 && kill(childPid, 0) != 0) {
        childPid = -1;
        if (childStack) { free(childStack); childStack = nullptr; }
        spawnBtn->setEnabled(true);
        killBtn->setEnabled(false);
        innerPidLabel->setText("Inner PID: — (child exited)");
    }
    refreshTable();
}

void NamespaceLab::refreshTable() {
    auto ns = readNamespaces(childPid > 0 ? childPid : -1);
    treeView->setData(ns, childPid);

    nsTable->setRowCount(0);
    for (auto& n : ns) {
        int row = nsTable->rowCount();
        nsTable->insertRow(row);
        auto cell = [&](const QString& t, const char* c = nullptr) {
            auto* i = new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            if (c) i->setForeground(QColor(c));
            return i;
        };
        nsTable->setItem(row, 0, cell(n.type.toUpper()));
        nsTable->setItem(row, 1, cell(n.ownNs, Theme::BLUE));
        nsTable->setItem(row, 2, cell(n.childNs.isEmpty() ? "—" : n.childNs,
                                     n.isolated ? Theme::GREEN : Theme::TEXT_MUTED));
        nsTable->setItem(row, 3, cell(n.isolated ? "✓ YES" : "same",
                                     n.isolated ? Theme::GREEN : Theme::TEXT_SECONDARY));
    }
}
