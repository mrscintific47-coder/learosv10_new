#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QMap>
#include <QPainter>
#include <QTabWidget>
#include <vector>
#include <unistd.h>
#include <sys/inotify.h>
#include "FsCanvas.h"

struct InotifyEvent {
    QString timestamp;
    QString path;
    QString name;
    QString eventType;  // CREATE, DELETE, MODIFY, OPEN, CLOSE, MOVED_FROM, MOVED_TO
    uint32_t mask;
};

// Live event log widget with colored event types
class InotifyEventLog : public QTextEdit {
    Q_OBJECT
public:
    explicit InotifyEventLog(QWidget* parent = nullptr);
    void addEvent(const InotifyEvent& ev);
private:
    int eventCount = 0;
};

// Cross-reference: which lab is each /proc/sys entry relevant to?
struct ProcEntry {
    QString path;
    bool    writable   = false;  // can be written without root? (proc/sys only)
    bool    needsRoot  = false;  // write requires CAP_SYS_ADMIN
    QString relevantTo;          // e.g. "Memory Lab", "Scheduler", ""
};

class FilesystemLab : public QWidget {
    Q_OBJECT
public:
    explicit FilesystemLab(QWidget* parent = nullptr);
    ~FilesystemLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void onWatchPath();
    void onStopWatching();
    void onCreateFile();
    void onDeleteFile();
    void onInotifyReady();
    void onTreeItemClicked(QTreeWidgetItem* item, int col);
    void onTreeItemDoubleClicked(QTreeWidgetItem* item, int col);
    void onProcRefreshTimer();  // Fix 1: live value refresh

private:
    // ── inotify watcher (left panel) ─────────────────────────────────────────
    int inotifyFd = -1;
    int watchFd   = -1;
    QString watchedPath;
    QTimer* inotifyTimer  = nullptr;
    QTimer* procRefreshTimer = nullptr;  // Fix 1

    // /proc/sys write safety: remember the value at session start
    QMap<QString, QString> originalSysValues;

    // ── UI ────────────────────────────────────────────────────────────────────
    QLineEdit*       pathInput;
    QPushButton*     watchBtn;
    QPushButton*     stopBtn;
    QPushButton*     createBtn;
    QPushButton*     deleteBtn;
    InotifyEventLog* eventLog;
    QTreeWidget*     procTree;
    QLabel*          statusLabel;
    QLabel*          procStatusLabel;   // Fix 1: "Last refreshed: hh:mm:ss"

    // ── Sandbox canvas (right tab) ────────────────────────────────────────────
    FsCanvas*    fsCanvas  = nullptr;
    QPushButton* newFileBtn;
    QPushButton* newDirBtn;
    QPushButton* hardLinkBtn;
    QPushButton* symLinkBtn;
    QPushButton* deleteNodeBtn;
    QPushButton* renameNodeBtn;
    QLabel*      sandboxStatusLabel;

    // ── helpers ───────────────────────────────────────────────────────────────
    void buildProcTree();
    void refreshProcValues();  // Fix 1: update column 1 for all visible items
    QString maskToString(uint32_t mask);

    // Fix 2: icon + tooltip per entry
    static QIcon  iconForEntry(const ProcEntry& e);
    static QString tooltipForEntry(const ProcEntry& e);

    // Fix 3: explanation text with "relevant to" tag
    QString explanationForProcPath(const QString& path,
                                   const QString& content,
                                   const ProcEntry* entry) const;

    // Metadata table — indexed by path
    static const QVector<ProcEntry>& allEntries();
    const ProcEntry* entryFor(const QString& path) const;
};
