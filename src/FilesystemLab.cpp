#include "FilesystemLab.h"
#include "EventBus.h"
#include "Theme.h"
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QPainterPath>
#include <QScrollBar>
#include <QDateTime>
#include <QStandardPaths>
#include <QIcon>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

// ── InotifyEventLog ───────────────────────────────────────────────────────────

InotifyEventLog::InotifyEventLog(QWidget* parent) : QTextEdit(parent) {
    setReadOnly(true);
    setStyleSheet(QString(
        "QTextEdit{background:#0f172a;border:1px solid %1;border-radius:8px;"
        "font-family:Consolas;font-size:11px;color:#e2e8f0;padding:6px;}"
    ).arg(Theme::BORDER));
}

void InotifyEventLog::addEvent(const InotifyEvent& ev) {
    static const QMap<QString,QString> colors = {
        {"CREATE",     "#22C55E"},
        {"DELETE",     "#EF4444"},
        {"MODIFY",     "#F97316"},
        {"OPEN",       "#4F6EF7"},
        {"CLOSE",      "#94A3B8"},
        {"CLOSE_W",    "#60A5FA"},
        {"MOVED_FROM", "#A855F7"},
        {"MOVED_TO",   "#14B8A6"},
        {"ATTRIB",     "#EAB308"},
    };
    QString color = colors.value(ev.eventType, "#94A3B8");
    QString name  = ev.name.isEmpty() ? QString() : ("/" + ev.name);
    QString html  = QString(
        "<span style='color:#64748B;'>%1</span>&nbsp;"
        "<span style='color:%2;font-weight:bold;'>%3</span>&nbsp;"
        "<span style='color:#e2e8f0;'>%4%5</span>"
    ).arg(ev.timestamp.toHtmlEscaped())
     .arg(color)
     .arg(ev.eventType.leftJustified(12, ' ').toHtmlEscaped())
     .arg(ev.path.toHtmlEscaped())
     .arg(name.toHtmlEscaped());

    moveCursor(QTextCursor::End);
    insertHtml(html + "<br>");

    eventCount++;
    if (eventCount > 500) {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, eventCount - 500);
        c.removeSelectedText();
        eventCount = 500;
    }
    moveCursor(QTextCursor::End);
}

// ── ProcEntry metadata table ──────────────────────────────────────────────────

const QVector<ProcEntry>& FilesystemLab::allEntries() {
    static const QVector<ProcEntry> table = {
        // path                                          writable  needsRoot  relevantTo
        {"/proc/sys/vm/swappiness",                      true,     true,  "Memory Lab"},
        {"/proc/sys/vm/overcommit_memory",               true,     true,  "Memory Lab"},
        {"/proc/sys/vm/dirty_ratio",                     true,     true,  "Memory Lab"},
        {"/proc/sys/vm/dirty_background_ratio",          true,     true,  "Memory Lab"},
        {"/proc/sys/kernel/hostname",                    true,     true,  ""},
        {"/proc/sys/kernel/pid_max",                     true,     true,  "Process Viewer"},
        {"/proc/sys/kernel/threads-max",                 true,     true,  "Thread Lab"},
        {"/proc/sys/kernel/randomize_va_space",          true,     true,  "Memory Lab"},
        {"/proc/sys/kernel/sched_latency_ns",            true,     true,  "Scheduler"},
        {"/proc/sys/kernel/sched_min_granularity_ns",    true,     true,  "Scheduler"},
        {"/proc/sys/kernel/sched_migration_cost_ns",     true,     true,  "Scheduler"},
        {"/proc/sys/net/core/somaxconn",                 true,     true,  "IPC Lab"},
        {"/proc/sys/net/ipv4/tcp_syncookies",            true,     true,  "IPC Lab"},
        {"/proc/version",                                false,    false, ""},
        {"/proc/uptime",                                 false,    false, ""},
        {"/proc/loadavg",                                false,    false, ""},
        {"/proc/meminfo",                                false,    false, "Memory Lab"},
        {"/proc/stat",                                   false,    false, ""},
        {"/proc/vmstat",                                 false,    false, "Memory Lab"},
        {"/proc/interrupts",                             false,    false, ""},
        {"/proc/net/dev",                                false,    false, "IPC Lab"},
        {"/proc/net/tcp",                                false,    false, "IPC Lab"},
        {"/proc/net/udp",                                false,    false, "IPC Lab"},
        {"/proc/net/if_inet6",                           false,    false, "IPC Lab"},
        {"/proc/net/arp",                                false,    false, "IPC Lab"},
        {"/proc/net/route",                              false,    false, "IPC Lab"},
        {"/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",  false, false, ""},
        {"/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",  false, false, ""},
        {"/sys/class/block",                             false,    false, ""},
        {"/sys/class/net",                               false,    false, "IPC Lab"},
    };
    return table;
}

const ProcEntry* FilesystemLab::entryFor(const QString& path) const {
    for (const auto& e : allEntries())
        if (e.path == path) return &e;
    return nullptr;
}

// Fix 2: icons representing read/write status
QIcon FilesystemLab::iconForEntry(const ProcEntry& e) {
    // We use colored text labels instead of real icons for portability,
    // but we also set a tooltip (see tooltipForEntry). Qt's QIcon supports
    // pixmap-less icons via QPixmap — we create tiny colored squares.
    QPixmap px(12, 12);
    if (e.writable)
        px.fill(QColor("#16A34A"));   // green = writable
    else
        px.fill(QColor("#94A3B8"));   // grey  = read-only
    return QIcon(px);
}

QString FilesystemLab::tooltipForEntry(const ProcEntry& e) {
    QString tip = e.writable
        ? "✏  Writable — double-click to set a new value (requires root)"
        : "🔒  Read-only — kernel exports live data here";
    if (!e.relevantTo.isEmpty())
        tip += "\n🔗  Relevant to: " + e.relevantTo;
    return tip;
}

// Fix 3: explanation with cross-reference banner
QString FilesystemLab::explanationForProcPath(const QString& path,
                                               const QString& content,
                                               const ProcEntry* entry) const {
    QString accessLine;
    QString relatedLine;
    if (entry) {
        if (entry->writable)
            accessLine = "<span style='color:#16A34A;font-weight:bold;'>✏ Writable</span>"
                         " — double-click to write a new value (requires root / CAP_SYS_ADMIN)";
        else
            accessLine = "<span style='color:#64748B;'>🔒 Read-only</span>"
                         " — kernel populates this automatically";

        if (!entry->relevantTo.isEmpty())
            relatedLine = QString(
                "<div style='background:#EEF2FF;border-left:3px solid #4F6EF7;"
                "border-radius:4px;padding:6px 8px;margin:6px 0;"
                "font-size:11px;'>"
                "🔗 <b>Relevant to:</b> %1"
                "</div>"
            ).arg(entry->relevantTo);
    }

    return QString(
        "<b>%1</b><br><br>"
        "%2"
        "%3"
        "<pre style='font-family:Consolas;font-size:11px;background:#f7f8fa;"
        "border-radius:6px;padding:8px;'>%4</pre>"
        "This is a real kernel interface. Reading it queries the kernel directly. "
        "Writing to it (if writable) changes live kernel behavior — no restart needed.<br><br>"
        "<code>cat %1</code>"
    ).arg(path)
     .arg(relatedLine)
     .arg(accessLine.isEmpty() ? QString() : accessLine + "<br><br>")
     .arg(content.toHtmlEscaped());
}

// ── FilesystemLab constructor ─────────────────────────────────────────────────

FilesystemLab::FilesystemLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("📄  Filesystem Lab — inotify + /proc & /sys browser + Sandbox");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● LIVE");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::TEAL).arg(Theme::TEAL_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Watch real filesystem events via <b>inotify</b>, browse <b>/proc</b> and <b>/sys</b> as live "
        "kernel interfaces, and build your own filesystem structures in a safe sandbox.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    // ── Tab widget — "Watch & Browse" vs "Sandbox Builder" ───────────────────
    auto* tabs = new QTabWidget();
    tabs->setStyleSheet(Theme::tabs());

    // ════════════════════════════════════════════════════════════════════════
    // Tab 1: inotify watcher + /proc browser (existing, now improved)
    // ════════════════════════════════════════════════════════════════════════
    auto* tab1 = new QWidget();
    tab1->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* mainRow = new QHBoxLayout(tab1);
    mainRow->setContentsMargins(8,8,8,8);
    mainRow->setSpacing(10);

    // LEFT: inotify watcher
    auto* watchCol = new QVBoxLayout();
    watchCol->setSpacing(10);

    auto* watchCard = new QWidget(); watchCard->setStyleSheet(Theme::card());
    auto* wl = new QVBoxLayout(watchCard); wl->setContentsMargins(12,10,12,10); wl->setSpacing(8);
    auto* wTitle = new QLabel("inotify Watch");
    wTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    wl->addWidget(wTitle);

    auto* pathRow = new QHBoxLayout();
    pathInput = new QLineEdit();
    pathInput->setText("/tmp");
    pathInput->setPlaceholderText("Directory to watch...");
    pathInput->setStyleSheet(Theme::input());
    watchBtn = new QPushButton("👁 Watch");
    watchBtn->setStyleSheet(Theme::btnPrimary());
    stopBtn  = new QPushButton("■ Stop");
    stopBtn->setStyleSheet(Theme::btnDanger());
    stopBtn->setEnabled(false);
    pathRow->addWidget(pathInput, 2);
    pathRow->addWidget(watchBtn);
    pathRow->addWidget(stopBtn);
    wl->addLayout(pathRow);

    auto* fileRow = new QHBoxLayout();
    createBtn = new QPushButton("+ Create File");
    createBtn->setStyleSheet(Theme::btnSuccess());
    deleteBtn = new QPushButton("🗑 Delete File");
    deleteBtn->setStyleSheet(Theme::btnGhost());
    fileRow->addWidget(createBtn);
    fileRow->addWidget(deleteBtn);
    wl->addLayout(fileRow);

    statusLabel = new QLabel("Select a directory and click Watch.");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    wl->addWidget(statusLabel);
    watchCol->addWidget(watchCard);

    auto* logLabel = new QLabel("Live Filesystem Events");
    logLabel->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    watchCol->addWidget(logLabel);

    eventLog = new InotifyEventLog();
    eventLog->setMinimumHeight(200);
    watchCol->addWidget(eventLog, 1);

    mainRow->addLayout(watchCol, 2);

    // RIGHT: /proc & /sys browser
    auto* browserCol = new QVBoxLayout();
    browserCol->setSpacing(6);

    auto* browseHeaderRow = new QHBoxLayout();
    auto* browseLabel = new QLabel("/proc & /sys — Virtual Filesystem Browser");
    browseLabel->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));

    // Fix 1: status showing last refresh time
    procStatusLabel = new QLabel("Auto-refreshing every 2s");
    procStatusLabel->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_MUTED));
    browseHeaderRow->addWidget(browseLabel);
    browseHeaderRow->addStretch();
    browseHeaderRow->addWidget(procStatusLabel);
    browserCol->addLayout(browseHeaderRow);

    auto* browseHint = new QLabel(
        "🔒 = read-only  ✏ = writable (double-click)  🔗 = linked to a lab tab");
    browseHint->setWordWrap(true);
    browseHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    browserCol->addWidget(browseHint);

    procTree = new QTreeWidget();
    procTree->setHeaderLabels({"Path", "Value (live)"});
    procTree->setColumnWidth(0, 220);
    procTree->setStyleSheet(Theme::table());
    procTree->setAlternatingRowColors(true);
    browserCol->addWidget(procTree, 1);

    mainRow->addLayout(browserCol, 1);

    tabs->addTab(tab1, "👁  Watch & Browse");

    // ════════════════════════════════════════════════════════════════════════
    // Tab 2: Sandbox filesystem builder
    // ════════════════════════════════════════════════════════════════════════
    auto* tab2 = new QWidget();
    tab2->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* sbLayout = new QVBoxLayout(tab2);
    sbLayout->setContentsMargins(8,8,8,8);
    sbLayout->setSpacing(8);

    // Sandbox description
    auto* sbHint = new QLabel(
        "Build real filesystem structures using genuine syscalls — "
        "<code>mkdir</code>, <code>open</code>, <code>link</code>, <code>symlink</code>, "
        "<code>unlink</code>, <code>rename</code>. "
        "Everything lives in <code>~/.learnos/fslab_sandbox/</code>. "
        "Nodes show real <b>inode numbers</b> and <b>link counts</b> from <code>stat()</code>. "
        "Solid edges = hard links (same inode). Dashed arrows = symlinks. "
        "A dashed red arrow = dangling symlink.");
    sbHint->setWordWrap(true);
    sbHint->setStyleSheet(QString(
        "color:%1;font-size:11px;padding:6px 8px;"
        "background:%2;border-left:3px solid %3;"
        "border-radius:4px;"
    ).arg(Theme::TEXT_SECONDARY).arg(Theme::BLUE_LIGHT).arg(Theme::BLUE));
    sbLayout->addWidget(sbHint);

    // Toolbar
    auto* toolbar = new QWidget();
    toolbar->setStyleSheet(Theme::card());
    auto* tbRow = new QHBoxLayout(toolbar);
    tbRow->setContentsMargins(8,6,8,6);
    tbRow->setSpacing(6);

    newFileBtn    = new QPushButton("📄 New File");
    newDirBtn     = new QPushButton("📁 New Folder");
    hardLinkBtn   = new QPushButton("🔗 Hard Link");
    symLinkBtn    = new QPushButton("↪ Sym Link");
    deleteNodeBtn = new QPushButton("🗑 Delete");
    renameNodeBtn = new QPushButton("✏ Rename");

    for (auto* b : {newFileBtn, newDirBtn}) b->setStyleSheet(Theme::btnSuccess());
    hardLinkBtn->setStyleSheet(Theme::btnPrimary());
    symLinkBtn->setStyleSheet(Theme::btnWarning());
    deleteNodeBtn->setStyleSheet(Theme::btnDanger());
    renameNodeBtn->setStyleSheet(Theme::btnGhost());

    tbRow->addWidget(newFileBtn);
    tbRow->addWidget(newDirBtn);
    tbRow->addSpacing(8);
    tbRow->addWidget(hardLinkBtn);
    tbRow->addWidget(symLinkBtn);
    tbRow->addSpacing(8);
    tbRow->addWidget(renameNodeBtn);
    tbRow->addWidget(deleteNodeBtn);
    tbRow->addStretch();

    sandboxStatusLabel = new QLabel("Sandbox ready.");
    sandboxStatusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    tbRow->addWidget(sandboxStatusLabel);
    sbLayout->addWidget(toolbar);

    // Canvas
    fsCanvas = new FsCanvas();
    sbLayout->addWidget(fsCanvas, 1);

    tabs->addTab(tab2, "🧱  Sandbox Builder");

    outer->addWidget(tabs, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(watchBtn,  &QPushButton::clicked, this, &FilesystemLab::onWatchPath);
    connect(stopBtn,   &QPushButton::clicked, this, &FilesystemLab::onStopWatching);
    connect(createBtn, &QPushButton::clicked, this, &FilesystemLab::onCreateFile);
    connect(deleteBtn, &QPushButton::clicked, this, &FilesystemLab::onDeleteFile);
    connect(procTree, &QTreeWidget::itemClicked,       this, &FilesystemLab::onTreeItemClicked);
    connect(procTree, &QTreeWidget::itemDoubleClicked, this, &FilesystemLab::onTreeItemDoubleClicked);

    // Sandbox toolbar
    connect(newFileBtn,    &QPushButton::clicked, fsCanvas, &FsCanvas::doNewFile);
    connect(newDirBtn,     &QPushButton::clicked, fsCanvas, &FsCanvas::doNewDir);
    connect(hardLinkBtn,   &QPushButton::clicked, fsCanvas, &FsCanvas::doHardLink);
    connect(symLinkBtn,    &QPushButton::clicked, fsCanvas, &FsCanvas::doSymLink);
    connect(deleteNodeBtn, &QPushButton::clicked, fsCanvas, &FsCanvas::doDelete);
    connect(renameNodeBtn, &QPushButton::clicked, fsCanvas, &FsCanvas::doRename);
    connect(fsCanvas, &FsCanvas::statusMessage, this, [this](const QString& msg){
        sandboxStatusLabel->setText(msg);
    });
    connect(fsCanvas, &FsCanvas::nodeClicked, this, [this](const FsNodeData& d){
        QString typeStr;
        switch (d.type) {
            case FsNodeData::Dir:     typeStr = "directory"; break;
            case FsNodeData::Symlink: typeStr = "symbolic link"; break;
            default:                  typeStr = "regular file"; break;
        }
        QString extra;
        if (d.type == FsNodeData::Symlink && !d.symlinkTarget.isEmpty())
            extra = QString("<br>Target: <code>%1</code>").arg(d.symlinkTarget.toHtmlEscaped());
        emit explanationNeeded(QString(
            "<b>%1</b><br><br>"
            "Type: <b>%2</b><br>"
            "Inode: <b>%3</b><br>"
            "Link count: <b>%4</b>%5<br><br>"
            "<b>What does link count mean?</b><br>"
            "Every directory entry pointing to this inode increments <code>st_nlink</code>. "
            "A link count &gt; 1 on a regular file means it has multiple hard links — "
            "they all share the same data blocks. Deleting one name does not free the data "
            "until the count reaches zero.<br><br>"
            "<b>Relevant syscalls:</b><br>"
            "• <code>stat(\"%1\", &st)</code> → inode, nlinks, size<br>"
            "• <code>link(old, new)</code> → create hard link<br>"
            "• <code>symlink(target, linkname)</code> → create symbolic link<br>"
            "• <code>unlink(path)</code> → remove name; data freed when nlinks==0"
        ).arg(d.absPath).arg(typeStr).arg(d.inode).arg(d.nlinks).arg(extra));
    });

    // inotify poll timer
    inotifyTimer = new QTimer(this);
    connect(inotifyTimer, &QTimer::timeout, this, &FilesystemLab::onInotifyReady);

    // Fix 1: /proc refresh timer — every 2.5 seconds
    procRefreshTimer = new QTimer(this);
    procRefreshTimer->setInterval(2500);
    connect(procRefreshTimer, &QTimer::timeout, this, &FilesystemLab::onProcRefreshTimer);

    buildProcTree();
    procRefreshTimer->start();

    // Set up sandbox
    QString sandboxPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                        + "/.learnos/fslab_sandbox";
    fsCanvas->setSandboxRoot(sandboxPath);
    sandboxStatusLabel->setText("Sandbox: " + sandboxPath);

    // Auto-start watching /tmp so events are visible immediately on first open.
    onWatchPath();
}

FilesystemLab::~FilesystemLab() {
    onStopWatching();
    procRefreshTimer->stop();
}

// ── inotify watch ─────────────────────────────────────────────────────────────

void FilesystemLab::onWatchPath() {
    onStopWatching();
    watchedPath = pathInput->text();
    if (!QFileInfo::exists(watchedPath)) {
        statusLabel->setText("⚠ Path doesn't exist");
        return;
    }

    inotifyFd = ::inotify_init1(IN_NONBLOCK);
    if (inotifyFd < 0) {
        statusLabel->setText("⚠ inotify_init1() failed");
        return;
    }

    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_OPEN |
                    IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
                    IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;
    watchFd = inotify_add_watch(inotifyFd, watchedPath.toLocal8Bit().constData(), mask);
    if (watchFd < 0) {
        statusLabel->setText("⚠ inotify_add_watch() failed");
        ::close(inotifyFd); inotifyFd = -1;
        return;
    }

    inotifyTimer->start(100);
    watchBtn->setEnabled(false);
    stopBtn->setEnabled(true);
    statusLabel->setText(QString("Watching %1 — events streaming below.").arg(watchedPath));

    emit explanationNeeded(QString(
        "<b>inotify — Filesystem Event Monitor</b><br><br>"
        "Watching: <code>%1</code><br><br>"
        "<code>inotify_init1()</code> creates a file descriptor in the kernel. "
        "<code>inotify_add_watch()</code> tells it which directory to monitor and which events.<br><br>"
        "<b>Events you'll see:</b><br>"
        "• <b>IN_CREATE</b> — file/dir created<br>"
        "• <b>IN_DELETE</b> — file/dir deleted<br>"
        "• <b>IN_MODIFY</b> — file content changed<br>"
        "• <b>IN_OPEN / IN_CLOSE</b> — file opened/closed<br>"
        "• <b>IN_MOVED_FROM/TO</b> — rename / move<br><br>"
        "The kernel writes events to the inotify fd. We read them with <code>read(fd, buf, ...)</code>. "
        "This is what file managers, <code>inotifywait</code>, and IDEs use for live reload."
    ).arg(watchedPath));
}

void FilesystemLab::onStopWatching() {
    inotifyTimer->stop();
    if (watchFd >= 0 && inotifyFd >= 0)
        inotify_rm_watch(inotifyFd, watchFd);
    if (inotifyFd >= 0) { ::close(inotifyFd); inotifyFd = -1; }
    watchFd = -1;
    watchBtn->setEnabled(true);
    stopBtn->setEnabled(false);
    if (!watchedPath.isEmpty())
        statusLabel->setText("Stopped watching.");
}

void FilesystemLab::onInotifyReady() {
    if (inotifyFd < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n = read(inotifyFd, buf, sizeof(buf));
    if (n <= 0) return;

    char* p = buf;
    QString firstEventType;
    QString firstName;
    while (p < buf + n) {
        struct inotify_event* ev = (struct inotify_event*)p;
        InotifyEvent ie;
        ie.timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        ie.path      = watchedPath;
        ie.name      = ev->len > 0 ? QString::fromLocal8Bit(ev->name) : QString();
        ie.mask      = ev->mask;
        ie.eventType = maskToString(ev->mask);
        eventLog->addEvent(ie);

        OSEvent oe;
        oe.type   = OSEvent::FilesystemEvent;
        oe.detail = QString("inotify %1: %2%3")
            .arg(ie.eventType)
            .arg(ie.path)
            .arg(ie.name.isEmpty() ? QString() : "/" + ie.name);
        EventBus::get().fire(oe);

        if (firstEventType.isEmpty()) {
            firstEventType = ie.eventType;
            firstName = ie.name;
        }

        p += sizeof(struct inotify_event) + ev->len;
    }

    if (!firstEventType.isEmpty()) {
        static const QMap<QString,QString> descriptions = {
            {"CREATE",     "A new file or directory was <b>created</b> in the watched directory. "
                           "The kernel adds an entry to the directory inode and updates mtime."},
            {"DELETE",     "A file or directory was <b>deleted</b>. The kernel decrements the "
                           "link count; the data blocks are freed when the count reaches zero."},
            {"MODIFY",     "A file's <b>contents were modified</b> (a write() was made). "
                           "The kernel marks the page dirty; it will be flushed to disk by pdflush."},
            {"OPEN",       "A file was <b>opened</b> — a new file descriptor was allocated in "
                           "the process's fd table pointing to this inode."},
            {"CLOSE_W",    "A file that was open for <b>writing was closed</b>. Any buffered data "
                           "may now be flushed to the page cache."},
            {"CLOSE",      "A file was <b>closed</b> (read-only). The fd entry was freed."},
            {"MOVED_FROM", "A file was <b>renamed/moved away</b> from this directory."},
            {"MOVED_TO",   "A file was <b>renamed/moved into</b> this directory."},
            {"ATTRIB",     "A file's <b>metadata changed</b> — permissions, owner, timestamps, "
                           "or extended attributes were modified."},
        };
        QString desc = descriptions.value(firstEventType,
            QString("Kernel filesystem event: <b>%1</b>.").arg(firstEventType));
        QString target = firstName.isEmpty()
            ? watchedPath
            : watchedPath + "/" + firstName;
        emit explanationNeeded(QString(
            "<b>inotify event: %1</b><br><br>"
            "Path: <code>%2</code><br><br>"
            "%3<br><br>"
            "<b>How it works:</b> The kernel wrote this event to the inotify file "
            "descriptor via <code>inotify_add_watch()</code>. We read it with "
            "<code>read(inotifyFd, buf, sizeof(buf))</code> every 100ms."
        ).arg(firstEventType).arg(target).arg(desc));
    }
}

void FilesystemLab::onCreateFile() {
    if (inotifyFd < 0) onWatchPath();
    if (watchedPath.isEmpty()) watchedPath = "/tmp";
    QString path = watchedPath + "/learnos_test_" +
                   QString::number(QDateTime::currentMSecsSinceEpoch() % 10000);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("LearnOS filesystem lab test file\n");
        f.close();
        statusLabel->setText("Created: " + path);
    }
}

void FilesystemLab::onDeleteFile() {
    if (inotifyFd < 0) onWatchPath();
    QDir dir(watchedPath.isEmpty() ? "/tmp" : watchedPath);
    QStringList files = dir.entryList({"learnos_test_*"}, QDir::Files, QDir::Time);
    if (!files.isEmpty()) {
        dir.remove(files.first());
        statusLabel->setText("Deleted: " + files.first());
    } else {
        statusLabel->setText("No learnos_test_* file found to delete.");
    }
}

QString FilesystemLab::maskToString(uint32_t mask) {
    if (mask & IN_CREATE)       return "CREATE";
    if (mask & IN_DELETE)       return "DELETE";
    if (mask & IN_MODIFY)       return "MODIFY";
    if (mask & IN_OPEN)         return "OPEN";
    if (mask & IN_CLOSE_WRITE)  return "CLOSE_W";
    if (mask & IN_CLOSE_NOWRITE)return "CLOSE";
    if (mask & IN_MOVED_FROM)   return "MOVED_FROM";
    if (mask & IN_MOVED_TO)     return "MOVED_TO";
    if (mask & IN_ATTRIB)       return "ATTRIB";
    if (mask & IN_ISDIR)        return "DIR";
    return QString("0x%1").arg(mask, 0, 16);
}

// ── /proc + /sys browser ──────────────────────────────────────────────────────

void FilesystemLab::buildProcTree() {
    procTree->clear();

    struct Section {
        QString label;
        QStringList paths;
    };

    const QVector<Section> sections = {
        {"⚙ /proc/sys — Kernel Parameters", {
            "/proc/sys/vm/swappiness",
            "/proc/sys/vm/overcommit_memory",
            "/proc/sys/vm/dirty_ratio",
            "/proc/sys/vm/dirty_background_ratio",
            "/proc/sys/kernel/hostname",
            "/proc/sys/kernel/pid_max",
            "/proc/sys/kernel/threads-max",
            "/proc/sys/kernel/randomize_va_space",
            "/proc/sys/kernel/sched_latency_ns",
            "/proc/sys/kernel/sched_min_granularity_ns",
            "/proc/sys/kernel/sched_migration_cost_ns",
            "/proc/sys/net/core/somaxconn",
            "/proc/sys/net/ipv4/tcp_syncookies",
        }},
        {"💾 /proc — System Info", {
            "/proc/version",
            "/proc/uptime",
            "/proc/loadavg",
            "/proc/meminfo",
            "/proc/stat",
            "/proc/vmstat",
            "/proc/interrupts",
            "/proc/net/dev",
        }},
        {"🌐 /proc/net — Network", {
            "/proc/net/tcp",
            "/proc/net/udp",
            "/proc/net/if_inet6",
            "/proc/net/arp",
            "/proc/net/route",
        }},
        {"🔧 /sys — Hardware & Drivers", {
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
            "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
            "/sys/class/block",
            "/sys/class/net",
        }},
    };

    for (const auto& section : sections) {
        auto* root = new QTreeWidgetItem(procTree);
        root->setText(0, section.label);
        root->setFont(0, QFont("Segoe UI", 10, QFont::Bold));
        root->setForeground(0, QColor(Theme::BLUE));

        for (const auto& path : section.paths) {
            if (!QFileInfo::exists(path)) continue;
            auto* item = new QTreeWidgetItem(root);

            const ProcEntry* entry = entryFor(path);

            // Fix 2: icon and color by access type
            if (entry) {
                item->setIcon(0, iconForEntry(*entry));
                item->setToolTip(0, tooltipForEntry(*entry));
                // Fix 3: show "relevant to" label in the name column
                QString label = path.split('/').last();
                if (!entry->relevantTo.isEmpty())
                    label += "  🔗";
                item->setText(0, label);
                item->setToolTip(1, entry->relevantTo.isEmpty()
                    ? QString() : "Relevant to: " + entry->relevantTo);
                // Color writable entries in green tint, read-only in normal grey
                if (entry->writable)
                    item->setForeground(0, QColor(Theme::GREEN));
                else
                    item->setForeground(0, QColor(Theme::TEXT_SECONDARY));
            } else {
                item->setText(0, path.split('/').last());
                item->setForeground(0, QColor(Theme::TEXT_SECONDARY));
            }

            item->setData(0, Qt::UserRole, path);
            item->setFont(0, QFont("Consolas", 9));

            // Read first line as preview
            std::ifstream f(path.toStdString());
            std::string line;
            if (std::getline(f, line) && !line.empty()) {
                QString preview = QString::fromStdString(line).left(40);
                item->setText(1, preview);
                item->setForeground(1, QColor(Theme::TEXT_PRIMARY));
            } else {
                item->setText(1, "[directory or binary]");
                item->setForeground(1, QColor(Theme::TEXT_MUTED));
            }
        }
        root->setExpanded(true);
    }
}

// ── Fix 1: Timer-driven live value refresh ────────────────────────────────────

void FilesystemLab::onProcRefreshTimer() {
    refreshProcValues();
    procStatusLabel->setText(
        "Refreshed: " + QDateTime::currentDateTime().toString("hh:mm:ss"));
}

void FilesystemLab::refreshProcValues() {
    // Walk every leaf item in the tree and update column 1
    QTreeWidgetItemIterator it(procTree, QTreeWidgetItemIterator::NoChildren);
    while (*it) {
        QTreeWidgetItem* item = *it;
        QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) {
            std::ifstream f(path.toStdString());
            std::string line;
            if (std::getline(f, line) && !line.empty()) {
                QString preview = QString::fromStdString(line).trimmed().left(40);
                item->setText(1, preview);
                item->setForeground(1, QColor(Theme::TEXT_PRIMARY));
            }
        }
        ++it;
    }
}

// ── /proc tree click ──────────────────────────────────────────────────────────

void FilesystemLab::onTreeItemClicked(QTreeWidgetItem* item, int) {
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    std::ifstream f(path.toStdString());
    if (!f.is_open()) {
        emit explanationNeeded(QString(
            "<b>%1</b><br><br>Cannot read — may require root or doesn't exist.").arg(path));
        return;
    }
    std::string content;
    std::string line;
    int lines = 0;
    while (std::getline(f, line) && lines < 30) {
        content += line + "\n";
        lines++;
    }
    if (!f.eof()) content += "... (truncated)";

    // Update the value column live
    QString firstLine = QString::fromStdString(content).split('\n').first().trimmed();
    item->setText(1, firstLine.left(40));

    const ProcEntry* entry = entryFor(path);
    emit explanationNeeded(
        explanationForProcPath(path, QString::fromStdString(content), entry));
}

// ── Double-click to write writable /proc/sys entries ─────────────────────────

void FilesystemLab::onTreeItemDoubleClicked(QTreeWidgetItem* item, int) {
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    if (!path.startsWith("/proc/sys/")) {
        const ProcEntry* entry = entryFor(path);
        QString relLine;
        if (entry && !entry->relevantTo.isEmpty())
            relLine = QString(
                "<div style='background:#EEF2FF;border-left:3px solid #4F6EF7;"
                "border-radius:4px;padding:6px 8px;margin:6px 0;'>"
                "🔗 <b>Relevant to:</b> %1</div>"
            ).arg(entry->relevantTo);
        emit explanationNeeded(QString(
            "<b>%1</b><br><br>"
            "%2"
            "🔒 This entry is <b>read-only</b> — only <code>/proc/sys/</code> kernel parameters "
            "can be written from here.<br><br>"
            "Writable examples: <code>swappiness</code>, <code>randomize_va_space</code>, "
            "<code>somaxconn</code>."
        ).arg(path).arg(relLine));
        return;
    }

    std::ifstream f(path.toStdString());
    std::string cur;
    std::getline(f, cur);
    QString current = QString::fromStdString(cur).trimmed();

    if (!originalSysValues.contains(path))
        originalSysValues.insert(path, current);
    QString original = originalSysValues.value(path);

    bool ok = false;
    QString label = QString("Current value: %1").arg(current);
    if (current != original)
        label += QString("  (original: %1)").arg(original);
    label += QString("\n\nNew value for %1:").arg(path);

    QString newVal = QInputDialog::getText(
        this,
        QString("Write kernel parameter — %1").arg(path.split('/').last()),
        label,
        QLineEdit::Normal, current, &ok);

    if (!ok || newVal.trimmed().isEmpty() || newVal.trimmed() == current)
        return;

    QString paramName = path.split('/').last();
    auto answer = QMessageBox::warning(
        this,
        "Confirm kernel parameter write",
        QString(
            "You are about to write a live kernel parameter:\n\n"
            "  %1\n\n"
            "  Current value : %2\n"
            "  New value     : %3\n"
            "  Original value: %4\n\n"
            "The change takes effect immediately. Some parameters can destabilise\n"
            "the running system if set to invalid values.\n\n"
            "Proceed?"
        ).arg(path).arg(current).arg(newVal.trimmed()).arg(original),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Reset,
        QMessageBox::No);

    if (answer == QMessageBox::Reset) {
        newVal = original;
        if (newVal == current) {
            statusLabel->setText(QString("Already at original value (%1)").arg(original));
            return;
        }
    } else if (answer != QMessageBox::Yes) {
        return;
    }

    QFile wf(path);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusLabel->setText(QString("⚠ Cannot write %1 — need root or CAP_SYS_ADMIN").arg(path));
        emit explanationNeeded(QString(
            "<b>Write failed: %1</b><br><br>"
            "Writing to <code>/proc/sys/</code> requires <b>root</b> or "
            "<code>CAP_SYS_ADMIN</code>.<br><br>"
            "To grant it: <code>sudo setcap cap_sys_admin+ep build/LearnOS</code><br>"
            "Or run with: <code>sudo build/LearnOS</code>"
        ).arg(path));
        return;
    }
    wf.write(newVal.trimmed().toUtf8());
    wf.close();

    bool isRevert = (newVal.trimmed() == original && current != original);
    item->setText(1, newVal.trimmed().left(40));
    statusLabel->setText(isRevert
        ? QString("Reverted %1 → %2 (original)").arg(paramName).arg(newVal.trimmed())
        : QString("Wrote '%1' → %2").arg(newVal.trimmed()).arg(path));

    emit explanationNeeded(isRevert
        ? QString(
            "<b>Reverted kernel parameter</b><br><br>"
            "<code>%1</code><br><br>"
            "Restored to original value: <b>%2</b><br>"
            "(was: <b>%3</b>)<br><br>"
            "The kernel is using the original value again."
          ).arg(path).arg(newVal.trimmed()).arg(current)
        : QString(
            "<b>Wrote to kernel parameter</b><br><br>"
            "<code>%1</code><br><br>"
            "Old value: <b>%2</b><br>"
            "New value: <b>%3</b><br>"
            "Original (session start): <b>%4</b><br><br>"
            "This was a real write to the kernel's parameter interface. "
            "The change takes effect immediately — no reboot needed.<br><br>"
            "To revert: double-click this entry again and click <b>Reset</b>.<br>"
            "To verify: <code>cat %1</code><br><br>"
            "<b>Note:</b> Most parameters reset to defaults on reboot. "
            "To make permanent, add to <code>/etc/sysctl.conf</code>."
          ).arg(path).arg(current).arg(newVal.trimmed()).arg(original));
}
