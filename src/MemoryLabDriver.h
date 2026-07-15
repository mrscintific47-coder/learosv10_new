#pragma once
#include <QObject>
#include <QProcess>
#include <vector>
#include <string>

struct ArenaBlock {
    int    id;
    long   offset;
    long   size;
    bool   used;
    int    fillByte;
};

struct ArenaSummary {
    long totalBytes = 0;
    long usedBytes  = 0;
    long freeBytes  = 0;
    int  blockCount = 0;
    long largestFreeRun = 0;
};

// Owns the memlab_worker child process and speaks its line protocol.
// Emits signals when the arena state changes so the UI can redraw, and
// emits workerDied() if the process ever crashes — the UI is expected to
// offer a "Restart Worker" action when that happens (the safety net).
class MemoryLabDriver : public QObject {
    Q_OBJECT

public:
    explicit MemoryLabDriver(QObject* parent = nullptr);
    ~MemoryLabDriver();

    void start();          // launch (or relaunch) the worker process
    void stop();           // terminate it deliberately

    void alloc(long size, const QString& strategy);
    void freeBlock(int id);
    void writeBlock(int id, int byteVal);
    void mprotect(long id, const QString& perm);
    void madvise(long id, const QString& advice);
    void cowFork();
    void resetArena();

    bool isRunning() const;

    pid_t workerPid() const { return pidVal; }
    long  lastBlockId() const { return lastBlock; }  // last allocated/live block id

signals:
    void arenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void commandFailed(QString reason);
    void workerDied();
    void workerReady(long capacityBytes);
    void workerPidKnown(pid_t pid);  // emitted once after GETPID response

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess*  proc      = nullptr;
    pid_t      pidVal    = -1;
    qint64     qpid      = -1;  // QProcess::processId() — registered on start()
    long       lastBlock = -1;  // track last known live block id
    QByteArray buffer;
    std::vector<ArenaBlock> pendingBlocks;

    void send(const QString& line);
    void processLine(const QString& line);
};
