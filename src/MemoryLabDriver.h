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
    void resetArena();

    bool isRunning() const;

signals:
    void arenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void commandFailed(QString reason);
    void workerDied();
    void workerReady(long capacityBytes);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess* proc = nullptr;
    QByteArray buffer;
    std::vector<ArenaBlock> pendingBlocks;

    void send(const QString& line);
    void processLine(const QString& line);
};
