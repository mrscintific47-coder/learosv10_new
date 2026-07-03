#pragma once
#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QString>
#include <vector>
#include <unistd.h>

struct DSWorkerNode {
    QString address;
    int     value   = 0;
    QString nextAddr;
    QString leftAddr;
    QString rightAddr;
    int     bucket  = 0;
    int     hmKey   = 0;
    int     hmValue = 0;
    QString type;
};

struct DSWorkerSummary {
    QString dsType;
    int     size    = 0;
    long    rssKB   = 0;
    QString heapStart;
    QString heapEnd;
};

struct DSWorkerOp {
    QString operation;
    int     value   = 0;
    QString address;
    QString result;
};

struct DSStressResult {
    int  count = 0;
    long ms    = 0;
    long rssKB = 0;
};

class DSLabDriver : public QObject {
    Q_OBJECT

public:
    explicit DSLabDriver(QObject* parent = nullptr);
    ~DSLabDriver();
    void start();
    void stop();
    bool isRunning() const;
    pid_t workerPid() const { return pidVal; }

    void setDS(const QString& type);
    void push(int v);    void pop();
    void enqueue(int v); void dequeue();
    void insert(int v);  void remove(int v);
    void search(int v);  void stress(int n);
    void clear();        void status();

protected:
    void onReadyRead();
    void onError(QProcess::ProcessError);
    void onFinished(int, QProcess::ExitStatus);

signals:
    void stateUpdated(std::vector<DSWorkerNode> nodes, DSWorkerSummary summary, DSWorkerOp lastOp);
    void stressCompleted(DSStressResult result);
    void commandFailed(QString reason);
    void workerReady(pid_t pid);
    void workerDied();

private:
    QProcess*  proc;
    QByteArray buf;
    std::vector<DSWorkerNode> nodeBuffer;
    DSWorkerOp pendingOp;
    bool hasOp  = false;
    pid_t pidVal = -1;

    void send(const QString& line);
    void processLine(const QString& line);
    static QString findBinary();
};
