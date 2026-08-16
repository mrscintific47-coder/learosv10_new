#pragma once
#include <QObject>
#include <QProcess>
#include <QDateTime>
#include <deque>
#include <vector>
#include <string>

// ── Block ─────────────────────────────────────────────────────────────────────
struct ArenaBlock {
    int     id;
    long    offset;   // byte offset within the 4 MB sandbox
    long    size;
    bool    used;
    QString label;
    // Structure info (set when isStruct == true)
    bool    isStruct   = false;
    QString structType; // "LL", "ARRAY", "TREE", "HASH", ""
    int     elemCount  = 0;
    long    elemSize   = 0;
};

// ── Per-node record inside a structured allocation ────────────────────────────
struct StructNode {
    int    structId;
    int    nodeIdx;
    long   offset;       // byte offset within sandbox
    long   link1 = 0;   // for LL: next ptr offset; for TREE: left ptr offset
    long   link2 = 0;   // for TREE: right ptr offset
};

// ── Arena summary ─────────────────────────────────────────────────────────────
struct ArenaSummary {
    long    totalBytes  = 0;
    long    usedBytes   = 0;
    long    freeBytes   = 0;
    int     blockCount  = 0;
    QString modeName;   // "contiguous", "paged", "segmented", "framed"
};

// ── Page table entry ──────────────────────────────────────────────────────────
struct PageEntry {
    int  vpn;
    int  frame;
    bool present;
    int  blockId; // -1 = free
};

// ── Segment table entry ───────────────────────────────────────────────────────
struct SegEntry {
    QString name;
    long    base;
    long    limit;
    int     blockId; // -1 = free
};

// ── Frame table entry ─────────────────────────────────────────────────────────
struct FrameEntry {
    int  frame;
    bool free;
    int  blockId;
};

// ── Event log ─────────────────────────────────────────────────────────────────
struct AllocEvent {
    enum class Op { Alloc, Free, Reset };
    qint64  timestampMs;
    Op      op;
    long    offset;
    long    size;
    int     blockId;
    QString label;
};

// ── Driver ───────────────────────────────────────────────────────────────────
class MemoryLabDriver : public QObject {
    Q_OBJECT
public:
    explicit MemoryLabDriver(QObject* parent = nullptr);
    ~MemoryLabDriver();

    void start();
    void stop();

    // Basic allocation
    void alloc(long size, const QString& label = {});
    void freeBlock(int id);
    void resetArena();
    void setMode(const QString& mode); // contiguous | paged | segmented | framed

    // Data structure allocation
    void allocLL   (int n,          const QString& label = {});
    void allocArray(int n, int esize, const QString& label = {});
    void allocTree (int n,          const QString& label = {});
    void allocHash (int buckets,    const QString& label = {});

    // Table queries
    void queryPageTable();
    void querySegTable();
    void queryFrameTable();

    bool isRunning() const;
    pid_t workerPid() const { return pidVal; }
    const std::deque<AllocEvent>& eventLog() const { return log_; }

signals:
    void arenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void structDefined(ArenaBlock block, std::vector<StructNode> nodes);
    void commandFailed(QString reason);
    void workerDied();
    void workerReady(long sandboxBytes);
    void workerPidKnown(pid_t pid);
    void eventLogged(AllocEvent ev);
    void pageTableReady(std::vector<PageEntry> entries);
    void segTableReady(std::vector<SegEntry> entries);
    void frameTableReady(std::vector<FrameEntry> entries);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError);
    void onProcessFinished(int, QProcess::ExitStatus);

private:
    QProcess*  proc      = nullptr;
    pid_t      pidVal    = -1;
    qint64     qpid      = -1;
    QByteArray buffer;

    std::vector<ArenaBlock> pendingBlocks;
    // Pending structured def being assembled from STRUCTDEF + NODE lines
    ArenaBlock              pendingStructBlock;
    std::vector<StructNode> pendingNodes;
    bool                    inStructDef = false;
    int                     expectedNodes = 0;

    // Page/seg/frame table accumulation
    std::vector<PageEntry>  pendingPageTable;
    std::vector<SegEntry>   pendingSegTable;
    std::vector<FrameEntry> pendingFrameTable;

    std::deque<AllocEvent>  log_;
    std::vector<ArenaBlock> prevBlocks_;
    QString pendingLabel_;

    void send(const QString& line);
    void processLine(const QString& line);
    void diffAndLog(const std::vector<ArenaBlock>& next);
};
