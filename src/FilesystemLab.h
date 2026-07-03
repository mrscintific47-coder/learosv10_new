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
#include <QPainter>
#include <vector>
#include <unistd.h>
#include <sys/inotify.h>

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
    void onBrowseProcSys();
    void onInotifyReady();
    void onTreeItemClicked(QTreeWidgetItem* item, int col);

private:
    // inotify
    int inotifyFd = -1;
    int watchFd   = -1;
    QString watchedPath;
    QTimer* inotifyTimer = nullptr;

    // UI
    QLineEdit*       pathInput;
    QPushButton*     watchBtn;
    QPushButton*     stopBtn;
    QPushButton*     createBtn;
    QPushButton*     deleteBtn;
    InotifyEventLog* eventLog;
    QTreeWidget*     procTree;
    QLabel*          statusLabel;

    void buildProcTree();
    void addProcNode(QTreeWidgetItem* parent, const QString& path, int depth);
    QString maskToString(uint32_t mask);
};
