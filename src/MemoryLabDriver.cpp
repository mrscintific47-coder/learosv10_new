#include "MemoryLabDriver.h"
#include "CleanupRegistry.h"
#include <QCoreApplication>
#include <QFileInfo>

MemoryLabDriver::MemoryLabDriver(QObject* parent) : QObject(parent) {
    proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, &MemoryLabDriver::onReadyRead);
    connect(proc, &QProcess::errorOccurred,           this, &MemoryLabDriver::onProcessError);
    connect(proc, &QProcess::finished,                this, &MemoryLabDriver::onProcessFinished);
}

MemoryLabDriver::~MemoryLabDriver() { stop(); }

static QString findWorker() {
    QString app = QCoreApplication::applicationDirPath();
    for (auto& c : {app + "/memlab_worker",
                    app + "/../tools/memlab_worker"})
        if (QFileInfo::exists(c)) return c;
    return app + "/memlab_worker";
}

void MemoryLabDriver::start() {
    if (proc->state() != QProcess::NotRunning) return;
    buffer.clear();
    pidVal = -1;
    log_.clear();
    prevBlocks_.clear();
    pendingLabel_.clear();
    inStructDef   = false;
    expectedNodes = 0;
    proc->start(findWorker(), {});
    if (proc->state() != QProcess::NotRunning) {
        qpid = proc->processId();
        if (qpid > 0) LearnOSCleanup::registerPid((pid_t)qpid);
    }
}

void MemoryLabDriver::stop() {
    if (proc->state() == QProcess::NotRunning) return;
    if (qpid > 0) { LearnOSCleanup::unregisterPid((pid_t)qpid); qpid = -1; }
    if (pidVal > 0) LearnOSCleanup::unregisterPid(pidVal);
    proc->kill();
    proc->waitForFinished(500);
    pidVal = -1;
    log_.clear();
    prevBlocks_.clear();
}

bool MemoryLabDriver::isRunning() const { return proc->state() == QProcess::Running; }

void MemoryLabDriver::send(const QString& line) {
    if (!isRunning()) { emit commandFailed("Worker not running — restart it."); return; }
    proc->write((line + "\n").toUtf8());
}

// ── public API ───────────────────────────────────────────────────────────────

void MemoryLabDriver::alloc(long size, const QString& label) {
    pendingLabel_ = label;
    send(QString("ALLOC %1 %2").arg(size).arg(label.isEmpty() ? "block" : label));
}

void MemoryLabDriver::freeBlock(int id)          { send(QString("FREE %1").arg(id)); }
void MemoryLabDriver::resetArena()               { send("RESET"); }
void MemoryLabDriver::setMode(const QString& m)  { send(QString("MODE %1").arg(m)); }
void MemoryLabDriver::queryPageTable()           { send("PAGETABLE"); }
void MemoryLabDriver::querySegTable()            { send("SEGTABLE"); }
void MemoryLabDriver::queryFrameTable()          { send("FRAMETABLE"); }

void MemoryLabDriver::allocLL(int n, const QString& label) {
    pendingLabel_ = label.isEmpty() ? "LinkedList" : label;
    send(QString("STRUCT LL %1 %2").arg(n).arg(pendingLabel_));
}
void MemoryLabDriver::allocArray(int n, int esize, const QString& label) {
    pendingLabel_ = label.isEmpty() ? "Array" : label;
    send(QString("STRUCT ARRAY %1 %2 %3").arg(n).arg(esize).arg(pendingLabel_));
}
void MemoryLabDriver::allocTree(int n, const QString& label) {
    pendingLabel_ = label.isEmpty() ? "BinTree" : label;
    send(QString("STRUCT TREE %1 %2").arg(n).arg(pendingLabel_));
}
void MemoryLabDriver::allocHash(int buckets, const QString& label) {
    pendingLabel_ = label.isEmpty() ? "HashMap" : label;
    send(QString("STRUCT HASH %1 %2").arg(buckets).arg(pendingLabel_));
}

// ── read loop ─────────────────────────────────────────────────────────────────

void MemoryLabDriver::onReadyRead() {
    buffer += proc->readAllStandardOutput();
    while (true) {
        int nl = buffer.indexOf('\n');
        if (nl < 0) break;
        QString line = QString::fromUtf8(buffer.left(nl)).trimmed();
        buffer.remove(0, nl + 1);
        if (!line.isEmpty()) processLine(line);
    }
}

void MemoryLabDriver::diffAndLog(const std::vector<ArenaBlock>& next) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // New blocks
    for (const auto& nb : next) {
        if (!nb.used) continue;
        bool existed = false;
        for (const auto& pb : prevBlocks_) if (pb.id == nb.id) { existed = true; break; }
        if (!existed) {
            AllocEvent ev;
            ev.timestampMs = now;
            ev.op          = AllocEvent::Op::Alloc;
            ev.offset      = nb.offset;
            ev.size        = nb.size;
            ev.blockId     = nb.id;
            ev.label       = nb.label;
            log_.push_back(ev);
            emit eventLogged(ev);
        }
    }
    // Freed blocks
    for (const auto& pb : prevBlocks_) {
        if (!pb.used) continue;
        bool stillLive = false;
        for (const auto& nb : next) if (nb.id == pb.id) { stillLive = true; break; }
        if (!stillLive) {
            AllocEvent ev;
            ev.timestampMs = now;
            ev.op          = AllocEvent::Op::Free;
            ev.offset      = pb.offset;
            ev.size        = pb.size;
            ev.blockId     = pb.id;
            ev.label       = pb.label;
            log_.push_back(ev);
            emit eventLogged(ev);
        }
    }
    pendingLabel_.clear();
}

void MemoryLabDriver::processLine(const QString& line) {

    if (line.startsWith("READY")) {
        long cap = line.section(' ', 1, 1).toLong();
        emit workerReady(cap);
        send("GETPID");
        return;
    }

    if (line.startsWith("WORKERPID")) {
        pidVal = (pid_t)line.section(' ', 1, 1).toLong();
        if (pidVal > 0 && pidVal != (pid_t)qpid)
            LearnOSCleanup::registerPid(pidVal);
        emit workerPidKnown(pidVal);
        return;
    }

    // ── STRUCTDEF <id> <type> <offset> <totalSize> <elemSize> <count> <label> ──
    if (line.startsWith("STRUCTDEF")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 8) {
            pendingStructBlock = ArenaBlock{};
            pendingStructBlock.id         = p[1].toInt();
            pendingStructBlock.structType = p[2];
            pendingStructBlock.offset     = p[3].toLong();
            pendingStructBlock.size       = p[4].toLong();
            pendingStructBlock.elemSize   = p[5].toLong();
            pendingStructBlock.elemCount  = p[6].toInt();
            pendingStructBlock.label      = QStringList(p.mid(7)).join(' ');
            pendingStructBlock.isStruct   = true;
            pendingStructBlock.used       = true;
            pendingNodes.clear();
            inStructDef   = true;
            expectedNodes = pendingStructBlock.elemCount;
        }
        return;
    }

    // ── NODE <structId> <nodeIdx> <offset> [link1] [link2] ──
    if (line.startsWith("NODE") && inStructDef) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 4) {
            StructNode n;
            n.structId = p[1].toInt();
            n.nodeIdx  = p[2].toInt();
            n.offset   = p[3].toLong();
            n.link1    = p.size() >= 5 ? p[4].toLong() : 0;
            n.link2    = p.size() >= 6 ? p[5].toLong() : 0;
            pendingNodes.push_back(n);
        }
        return;
    }

    // ── BLOCK <id> <offset> <size> <used> <label> ──
    if (line.startsWith("BLOCK")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 5) {
            ArenaBlock b;
            b.id     = p[1].toInt();
            b.offset = p[2].toLong();
            b.size   = p[3].toLong();
            b.used   = p[4].toInt() != 0;
            b.label  = QStringList(p.mid(5)).join(' ');
            pendingBlocks.push_back(b);
        }
        return;
    }

    // ── ARENA <total> <used> <free> <count> <mode> ──
    if (line.startsWith("ARENA")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        ArenaSummary s;
        if (p.size() >= 6) {
            s.totalBytes = p[1].toLong();
            s.usedBytes  = p[2].toLong();
            s.freeBytes  = p[3].toLong();
            s.blockCount = p[4].toInt();
            s.modeName   = p[5];
        }

        // Flush pending struct first (so it's in prevBlocks_ for next diff)
        if (inStructDef) {
            inStructDef = false;
            // Merge struct block into pendingBlocks if not already there
            bool found = false;
            for (auto& b : pendingBlocks) if (b.id == pendingStructBlock.id) { found = true; break; }
            if (!found) pendingBlocks.push_back(pendingStructBlock);
            else {
                for (auto& b : pendingBlocks) if (b.id == pendingStructBlock.id) {
                    b.isStruct   = true;
                    b.structType = pendingStructBlock.structType;
                    b.elemCount  = pendingStructBlock.elemCount;
                    b.elemSize   = pendingStructBlock.elemSize;
                    b.label      = pendingStructBlock.label;
                }
            }
            emit structDefined(pendingStructBlock, pendingNodes);
            pendingNodes.clear();
        }

        diffAndLog(pendingBlocks);
        prevBlocks_ = pendingBlocks;
        emit arenaUpdated(pendingBlocks, s);
        pendingBlocks.clear();
        return;
    }

    // ── RESET_LOG ──
    if (line.startsWith("RESET_LOG")) {
        AllocEvent ev;
        ev.timestampMs = QDateTime::currentMSecsSinceEpoch();
        ev.op          = AllocEvent::Op::Reset;
        ev.offset = ev.size = ev.blockId = 0;
        log_.push_back(ev);
        emit eventLogged(ev);
        prevBlocks_.clear();
        return;
    }

    // ── Page table ──
    if (line.startsWith("PAGETABLE_ENTRY")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 5) {
            PageEntry e;
            e.vpn     = p[1].toInt();
            e.frame   = p[2].toInt();
            e.present = p[3].toInt() != 0;
            e.blockId = p[4].toInt();
            pendingPageTable.push_back(e);
        }
        return;
    }

    // ── Segment table ──
    if (line.startsWith("SEGTABLE_ENTRY")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 5) {
            SegEntry e;
            e.name    = p[1];
            e.base    = p[2].toLong();
            e.limit   = p[3].toLong();
            e.blockId = p[4].toInt();
            pendingSegTable.push_back(e);
        }
        return;
    }

    // ── Frame table ──
    if (line.startsWith("FRAMETABLE_ENTRY")) {
        QStringList p = line.split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 4) {
            FrameEntry e;
            e.frame   = p[1].toInt();
            e.free    = p[2].toInt() != 0;
            e.blockId = p[3].toInt();
            pendingFrameTable.push_back(e);
        }
        return;
    }

    if (line == "OK") {
        // Flush accumulated table dumps
        if (!pendingPageTable.empty()) {
            emit pageTableReady(pendingPageTable);
            pendingPageTable.clear();
        }
        if (!pendingSegTable.empty()) {
            emit segTableReady(pendingSegTable);
            pendingSegTable.clear();
        }
        if (!pendingFrameTable.empty()) {
            emit frameTableReady(pendingFrameTable);
            pendingFrameTable.clear();
        }
        return;
    }

    if (line.startsWith("ERR")) {
        emit commandFailed(line.mid(4).trimmed());
        pendingBlocks.clear();
        inStructDef = false;
        return;
    }
    if (line == "PONG") return;
}

void MemoryLabDriver::onProcessError(QProcess::ProcessError) {
    emit commandFailed("Worker process error.");
}
void MemoryLabDriver::onProcessFinished(int, QProcess::ExitStatus) {
    emit workerDied();
}
