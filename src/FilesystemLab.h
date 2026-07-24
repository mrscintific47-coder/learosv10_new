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
#include <QCheckBox>
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
    bool    writable   = false;
    bool    needsRoot  = false;
    QString relevantTo;
};

class FilesystemLab : public QWidget {
    Q_OBJECT
public:
    explicit FilesystemLab(QWidget* parent = nullptr);
    ~FilesystemLab();

    // Called by MainWindow to hand sandbox pids in for /proc/fd edge scanning
    void setSandboxPids(const std::vector<pid_t>& pids);

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
    void onProcRefreshTimer();
    void onProcFdTimer();        // scan /proc/<pid>/fd and update canvas edges
    void onODirectToggled();     // toggle O_DIRECT on the canvas + update button label

private:
    // ── inotify watcher (left panel) ─────────────────────────────────────────
    int inotifyFd = -1;
    int watchFd   = -1;
    QString watchedPath;
    QTimer* inotifyTimer     = nullptr;
    QTimer* procRefreshTimer = nullptr;
    QTimer* procFdTimer      = nullptr;  // /proc/fd edge refresh every 1s

    std::vector<pid_t> m_sandboxPids;

    // /proc/sys write safety: remember the value at session start
    QMap<QString, QString> originalSysValues;

    // ── UI (Watch & Browse tab) ────────────────────────────────────────────────
    QLineEdit*       pathInput;
    QPushButton*     watchBtn;
    QPushButton*     stopBtn;
    QPushButton*     createBtn;
    QPushButton*     deleteBtn;
    InotifyEventLog* eventLog;
    QTreeWidget*     procTree;
    QLabel*          statusLabel;
    QLabel*          procStatusLabel;

    // ── Sandbox tab ────────────────────────────────────────────────────────────
    FsCanvas*    fsCanvas    = nullptr;
    QPushButton* newFileBtn;
    QPushButton* newDirBtn;
    QPushButton* hardLinkBtn;
    QPushButton* symLinkBtn;
    QPushButton* deleteNodeBtn;
    QPushButton* renameNodeBtn;
    QPushButton* chmodBtn;
    QPushButton* chownBtn;
    QPushButton* writeBtn;
    QPushButton* readBtn;
    QPushButton* oDirect;       // O_DIRECT toggle
    QPushButton* lockDemoBtn;
    QPushButton* sparseBtn;
    QPushButton* upDirBtn;
    QLabel*      sandboxStatusLabel;
    QLabel*      procFdLabel;   // "0 external FDs" indicator

    // ── helpers ───────────────────────────────────────────────────────────────
    void buildProcTree();
    void refreshProcValues();
    QString maskToString(uint32_t mask);

    static QIcon  iconForEntry(const ProcEntry& e);
    static QString tooltipForEntry(const ProcEntry& e);
    QString explanationForProcPath(const QString& path,
                                   const QString& content,
                                   const ProcEntry* entry) const;

    static const QVector<ProcEntry>& allEntries();
    const ProcEntry* entryFor(const QString& path) const;
};
