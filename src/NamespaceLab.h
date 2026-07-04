#pragma once
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QComboBox>
#include <QPainter>
#include <vector>
#include <unistd.h>
#include <sched.h>

struct NsInfo {
    QString type;       // pid, uts, net, mnt, ipc, user
    QString ownNs;      // our namespace inode
    QString childNs;    // child's namespace inode (if spawned)
    bool    isolated;   // child has a different ns from us
};

// Shows namespace tree: parent vs child namespaces side by side
class NsTreeView : public QWidget {
    Q_OBJECT
public:
    explicit NsTreeView(QWidget* parent = nullptr);
    void setData(const QVector<NsInfo>& ns, pid_t childPid);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<NsInfo> namespaces;
    pid_t child = -1;
};

class NamespaceLab : public QWidget {
    Q_OBJECT
public:
    explicit NamespaceLab(QWidget* parent = nullptr);
    ~NamespaceLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void onSpawnIsolated();
    void onKillChild();
    void onNsTypeChanged(int index);
    void onRefresh();
    void onRunInNamespace();

private:
    pid_t  childPid   = -1;
    void*  childStack = nullptr;  // heap-allocated stack for clone() child

    // UI
    QComboBox*    nsTypeBox;
    QPushButton*  spawnBtn;
    QPushButton*  killBtn;
    QLabel*       innerPidLabel;  // shows child's own getpid() result
    NsTreeView*   treeView;
    QTableWidget* nsTable;
    QTextEdit*    logView;
    QLineEdit*    cmdInput;
    QPushButton*  runInNsBtn;
    QLabel*       statusLabel;
    QTimer*       refreshTimer;

    void refreshTable();
    QVector<NsInfo> readNamespaces(pid_t pid);
    QString readNsInode(pid_t pid, const QString& type);
};
