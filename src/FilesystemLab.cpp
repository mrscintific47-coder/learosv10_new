#include "FilesystemLab.h"
#include "Theme.h"
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QPainterPath>
#include <QScrollBar>
#include <QDateTime>
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
        {"CREATE",  "#22C55E"},
        {"DELETE",  "#EF4444"},
        {"MODIFY",  "#F97316"},
        {"OPEN",    "#4F6EF7"},
        {"CLOSE",   "#94A3B8"},
        {"MOVED_FROM", "#A855F7"},
        {"MOVED_TO",   "#14B8A6"},
        {"ATTRIB",  "#EAB308"},
    };
    QString color = colors.value(ev.eventType, "#94A3B8");
    QString html = QString(
        "<span style='color:#64748B;'>%1</span> "
        "<span style='color:%2;font-weight:bold;'>%-12s</span> "
        "<span style='color:#e2e8f0;'>%3/%4</span>"
    ).arg(ev.timestamp)
     .arg(color)
     .arg(ev.path)
     .arg(ev.name.isEmpty() ? QString() : ev.name);
    // Use plain text for performance
    append(QString("[%1] %-12s %2/%3")
        .arg(ev.timestamp)
        .arg(ev.eventType.leftJustified(12, ' '))
        .arg(ev.path)
        .arg(ev.name));

    eventCount++;
    // Keep last 500 lines
    QStringList lines = toPlainText().split('\n');
    if (lines.size() > 500) {
        setPlainText(lines.mid(lines.size()-500).join('\n'));
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::End);
        setTextCursor(c);
    }
}

// ── FilesystemLab ─────────────────────────────────────────────────────────────

FilesystemLab::FilesystemLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("📄  Filesystem Lab — inotify + /proc & /sys browser");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● LIVE EVENTS");
    chip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;"
        "font-size:10px;font-weight:bold;").arg(Theme::TEAL).arg(Theme::TEAL_LIGHT));
    titleRow->addWidget(title); titleRow->addStretch(); titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel(
        "Watch real filesystem events via <b>inotify</b>, and browse <b>/proc</b> and <b>/sys</b> as the "
        "virtual filesystems they actually are — every file here is a live kernel interface.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    auto* mainRow = new QHBoxLayout();
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

    auto* browseLabel = new QLabel("/proc & /sys — Virtual Filesystem Browser");
    browseLabel->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    browserCol->addWidget(browseLabel);

    auto* browseHint = new QLabel("Click any entry to read its current kernel value. Double-click a writable entry to open it for editing.");
    browseHint->setWordWrap(true);
    browseHint->setStyleSheet(QString("color:%1;font-size:10px;").arg(Theme::TEXT_SECONDARY));
    browserCol->addWidget(browseHint);

    procTree = new QTreeWidget();
    procTree->setHeaderLabels({"Path", "Value (live)"});
    procTree->setColumnWidth(0, 200);
    procTree->setStyleSheet(Theme::table());
    procTree->setAlternatingRowColors(true);
    browserCol->addWidget(procTree, 1);

    mainRow->addLayout(browserCol, 1);
    outer->addLayout(mainRow, 1);

    connect(watchBtn,  &QPushButton::clicked, this, &FilesystemLab::onWatchPath);
    connect(stopBtn,   &QPushButton::clicked, this, &FilesystemLab::onStopWatching);
    connect(createBtn, &QPushButton::clicked, this, &FilesystemLab::onCreateFile);
    connect(deleteBtn, &QPushButton::clicked, this, &FilesystemLab::onDeleteFile);
    connect(procTree, &QTreeWidget::itemClicked, this, &FilesystemLab::onTreeItemClicked);

    inotifyTimer = new QTimer(this);
    connect(inotifyTimer, &QTimer::timeout, this, &FilesystemLab::onInotifyReady);

    buildProcTree();
}

FilesystemLab::~FilesystemLab() {
    onStopWatching();
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

    inotifyTimer->start(100); // poll every 100ms
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
    while (p < buf + n) {
        struct inotify_event* ev = (struct inotify_event*)p;
        InotifyEvent ie;
        ie.timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        ie.path      = watchedPath;
        ie.name      = ev->len > 0 ? QString::fromLocal8Bit(ev->name) : QString();
        ie.mask      = ev->mask;
        ie.eventType = maskToString(ev->mask);
        eventLog->addEvent(ie);
        p += sizeof(struct inotify_event) + ev->len;
    }
}

void FilesystemLab::onCreateFile() {
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
    // Delete the most recently created test file
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

static const QStringList INTERESTING_PROC_PATHS = {
    "/proc/version",
    "/proc/uptime",
    "/proc/loadavg",
    "/proc/cpuinfo",
    "/proc/meminfo",
    "/proc/sys/vm/swappiness",
    "/proc/sys/vm/overcommit_memory",
    "/proc/sys/vm/dirty_ratio",
    "/proc/sys/kernel/hostname",
    "/proc/sys/kernel/pid_max",
    "/proc/sys/kernel/threads-max",
    "/proc/sys/kernel/randomize_va_space",
    "/proc/sys/kernel/sched_latency_ns",
    "/proc/sys/kernel/sched_min_granularity_ns",
    "/proc/sys/kernel/sched_migration_cost_ns",
    "/proc/sys/net/core/somaxconn",
    "/proc/sys/net/ipv4/tcp_syncookies",
    "/proc/net/dev",
    "/proc/net/tcp",
    "/sys/block",
    "/sys/class/net",
    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
    "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
    "/sys/kernel/debug/tracing/events",
};

void FilesystemLab::buildProcTree() {
    procTree->clear();

    struct Section {
        QString label;
        QStringList paths;
    };

    QVector<Section> sections = {
        {"⚙ /proc/sys — Kernel Parameters", {
            "/proc/sys/vm/swappiness",
            "/proc/sys/vm/overcommit_memory",
            "/proc/sys/vm/dirty_ratio",
            "/proc/sys/kernel/hostname",
            "/proc/sys/kernel/pid_max",
            "/proc/sys/kernel/threads-max",
            "/proc/sys/kernel/randomize_va_space",
            "/proc/sys/kernel/sched_latency_ns",
            "/proc/sys/kernel/sched_min_granularity_ns",
            "/proc/sys/net/core/somaxconn",
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

    for (auto& section : sections) {
        auto* root = new QTreeWidgetItem(procTree);
        root->setText(0, section.label);
        root->setFont(0, QFont("Segoe UI", 10, QFont::Bold));
        root->setForeground(0, QColor(Theme::BLUE));

        for (auto& path : section.paths) {
            if (!QFileInfo::exists(path)) continue;
            auto* item = new QTreeWidgetItem(root);
            item->setText(0, path.split('/').last());
            item->setData(0, Qt::UserRole, path);
            item->setFont(0, QFont("Consolas", 9));
            item->setForeground(0, QColor(Theme::TEXT_SECONDARY));

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

void FilesystemLab::onTreeItemClicked(QTreeWidgetItem* item, int) {
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    // Read the full file content
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

    emit explanationNeeded(QString(
        "<b>%1</b><br><br>"
        "<pre style='font-family:Consolas;font-size:11px;background:#f7f8fa;"
        "border-radius:6px;padding:8px;'>%2</pre><br>"
        "This is a real kernel interface. Reading it queries the kernel directly. "
        "Writing to it (if writable) changes live kernel behavior — no restart needed.<br><br>"
        "<code>cat %1</code>"
    ).arg(path).arg(QString::fromStdString(content).toHtmlEscaped()));
}

void FilesystemLab::onBrowseProcSys() {
    buildProcTree();
}
