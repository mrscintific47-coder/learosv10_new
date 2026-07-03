#include "NamespaceLab.h"
#include "Theme.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QPainterPath>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sched.h>

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
    auto* chip = new QLabel("● REAL NAMESPACES");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::GREEN).arg(Theme::GREEN_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Linux namespaces isolate OS resources per process — this is the foundation of Docker/containers. "
        "Spawn a child into a new namespace and see the difference live in /proc/[pid]/ns/.");
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

    spawnBtn = new QPushButton("🚀  Spawn Isolated Child");
    spawnBtn->setStyleSheet(Theme::btnPrimary());
    killBtn  = new QPushButton("✕  Kill Child");
    killBtn->setStyleSheet(Theme::btnDanger());
    killBtn->setEnabled(false);

    ctrlL->addWidget(nsLabel);
    ctrlL->addWidget(nsTypeBox, 1);
    ctrlL->addWidget(spawnBtn);
    ctrlL->addWidget(killBtn);
    outer->addWidget(ctrl);

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

    // Log
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
    outer->addWidget(logCard);

    statusLabel = new QLabel("Ready — choose a namespace type and spawn a child.");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    outer->addWidget(statusLabel);

    connect(spawnBtn,  &QPushButton::clicked, this, &NamespaceLab::onSpawnIsolated);
    connect(killBtn,   &QPushButton::clicked, this, &NamespaceLab::onKillChild);
    connect(nsTypeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NamespaceLab::onNsTypeChanged);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &NamespaceLab::onRefresh);
    refreshTimer->start(2000);

    onNsTypeChanged(0);
}

NamespaceLab::~NamespaceLab() {
    if (childPid > 0) { kill(childPid, SIGKILL); waitpid(childPid, nullptr, WNOHANG); }
}

void NamespaceLab::onNsTypeChanged(int idx) {
    static const char* exps[] = {
        "<b>PID Namespace</b><br><br>"
        "A new PID namespace gives the child process its own PID numbering. "
        "The child sees itself as PID 1. From inside the container it looks like "
        "a fresh system with just its own processes. <br><br>"
        "<code>clone(CLONE_NEWPID)</code> is the syscall. "
        "This is how Docker makes every container think it's PID 1.",

        "<b>UTS Namespace</b><br><br>"
        "UTS (Unix Time Sharing) namespace isolates hostname and NIS domain. "
        "The child can call <code>sethostname()</code> without affecting the host. "
        "<br><br>Run <code>hostname</code> inside and outside — different results!",

        "<b>Network Namespace</b><br><br>"
        "Full network isolation: the child gets its own loopback, routing table, "
        "firewall rules, and sockets. <br><br>"
        "Docker creates a veth pair to connect container namespaces. "
        "We use <code>clone(CLONE_NEWNET)</code>. "
        "The child's <code>/proc/net/dev</code> shows only <code>lo</code>.",

        "<b>Mount Namespace</b><br><br>"
        "The child can mount/unmount filesystems without the host seeing. "
        "This is what lets containers have their own filesystem roots. "
        "<code>clone(CLONE_NEWNS)</code> is the original namespace syscall (1999).",

        "<b>IPC Namespace</b><br><br>"
        "Isolates System V IPC (message queues, semaphores, shared memory) "
        "and POSIX message queues. The child can't see or interact with "
        "the host's <code>ipcs</code> resources at all.",

        "<b>User Namespace</b><br><br>"
        "The child can have a different UID/GID mapping — it can appear to be "
        "root inside but be an unprivileged user outside. "
        "This is how rootless containers (Podman) work. "
        "Most powerful and most complex namespace.",

        "<b>Full Isolation (All Namespaces)</b><br><br>"
        "All six namespace flags combined: <code>CLONE_NEWPID | CLONE_NEWUTS | "
        "CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUSER</code>. "
        "This is a container with nothing shared from the host except the kernel.",
    };
    if (idx >= 0 && idx < 7)
        emit explanationNeeded(exps[idx]);
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
    if (childPid > 0) { kill(childPid, SIGKILL); waitpid(childPid, nullptr, WNOHANG); childPid = -1; }

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

    childPid = fork();
    if (childPid == 0) {
        // Unshare into new namespace
        if (unshare(flags & ~SIGCHLD) == 0) {
            if (idx == 1) {
                // UTS: change hostname inside child
                sethostname("learnos-child", 13);
            }
        }
        // Keep alive
        while(true) sleep(1);
        _exit(0);
    }

    if (childPid > 0) {
        spawnBtn->setEnabled(false);
        killBtn->setEnabled(true);
        statusLabel->setText(QString("Child PID %1 spawned with %2").arg(childPid).arg(flagStr));
        logView->append(QString("[%1] Spawned PID %2 with %3")
            .arg(QTime::currentTime().toString("hh:mm:ss")).arg(childPid).arg(flagStr));
        onRefresh();

        emit explanationNeeded(QString(
            "<b>Child Spawned — PID %1</b><br><br>"
            "The child was forked and then called <code>unshare(%2)</code> "
            "to enter a new namespace.<br><br>"
            "<b>Check it yourself:</b><br>"
            "• <code>ls -la /proc/%1/ns/</code> — see its namespace inodes<br>"
            "• <code>ls -la /proc/%3/ns/</code> — compare to our own<br>"
            "• Different inode = different namespace = isolated<br><br>"
            "The tree above shows which namespaces match (grey) and which are new (green)."
        ).arg(childPid).arg(flagStr).arg(getpid()));
    } else {
        statusLabel->setText("⚠ fork() failed — may need CAP_SYS_ADMIN for some namespace types");
    }
}

void NamespaceLab::onKillChild() {
    if (childPid <= 0) return;
    kill(childPid, SIGKILL);
    waitpid(childPid, nullptr, WNOHANG);
    logView->append(QString("[%1] Killed PID %2")
        .arg(QTime::currentTime().toString("hh:mm:ss")).arg(childPid));
    childPid = -1;
    spawnBtn->setEnabled(true);
    killBtn->setEnabled(false);
    treeView->setData({}, -1);
    nsTable->setRowCount(0);
    statusLabel->setText("Child killed — namespaces destroyed.");
}

void NamespaceLab::onRefresh() {
    if (childPid > 0 && kill(childPid, 0) != 0) {
        // Child died on its own
        childPid = -1;
        spawnBtn->setEnabled(true);
        killBtn->setEnabled(false);
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
