#include "MemoryLabDriver.h"
#include "CleanupRegistry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

MemoryLabDriver::MemoryLabDriver(QObject* parent) : QObject(parent) {
    proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, &MemoryLabDriver::onReadyRead);
    connect(proc, &QProcess::errorOccurred, this, &MemoryLabDriver::onProcessError);
    connect(proc, &QProcess::finished, this, &MemoryLabDriver::onProcessFinished);
}

MemoryLabDriver::~MemoryLabDriver() {
    stop();
}

static QString findWorkerBinary() {
    // The worker is built alongside the main app by CMake. Look next to
    // the running executable first (normal case), then fall back to the
    // build directory layout used during development.
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/memlab_worker",
        appDir + "/tools/memlab_worker",
        appDir + "/../tools/memlab_worker",
    };
    for (auto& c : candidates) {
        if (QFileInfo::exists(c)) return c;
    }
    return appDir + "/memlab_worker"; // best effort; will fail loudly if missing
}

void MemoryLabDriver::start() {
    if (proc->state() != QProcess::NotRunning) return;
    buffer.clear();
    pidVal = -1;
    proc->start(findWorkerBinary(), {});
    // Register immediately with the QProcess PID so the crash handler can
    // kill the worker even if the app crashes before WORKERPID is received.
    if (proc->state() != QProcess::NotRunning) {
        qpid = proc->processId();
        if (qpid > 0) LearnOSCleanup::registerPid((pid_t)qpid);
    }
}

void MemoryLabDriver::stop() {
    if (proc->state() == QProcess::NotRunning) return;
    if (qpid > 0) { LearnOSCleanup::unregisterPid((pid_t)qpid); qpid = -1; }
    if (pidVal > 0) { LearnOSCleanup::unregisterPid(pidVal); }
    proc->kill();
    proc->waitForFinished(500);
    pidVal = -1;
}

bool MemoryLabDriver::isRunning() const {
    return proc->state() == QProcess::Running;
}

void MemoryLabDriver::send(const QString& line) {
    if (!isRunning()) {
        emit commandFailed("Worker is not running — click Restart Worker.");
        return;
    }
    proc->write((line + "\n").toUtf8());
}

void MemoryLabDriver::alloc(long size, const QString& strategy) {
    send(QString("ALLOC %1 %2").arg(size).arg(strategy));
}

void MemoryLabDriver::freeBlock(int id) {
    send(QString("FREE %1").arg(id));
}

void MemoryLabDriver::writeBlock(int id, int byteVal) {
    send(QString("WRITE %1 %2").arg(id).arg(byteVal));
}

void MemoryLabDriver::mprotect(long id, const QString& perm) {
    send(QString("MPROTECT %1 %2").arg(id).arg(perm));
}

void MemoryLabDriver::madvise(long id, const QString& advice) {
    send(QString("MADVISE %1 %2").arg(id).arg(advice));
}

void MemoryLabDriver::cowFork() {
    send("COW_FORK");
}

void MemoryLabDriver::resetArena() {
    send("RESET");
}

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

void MemoryLabDriver::processLine(const QString& line) {
    if (line.startsWith("READY")) {
        long cap = line.section(' ', 1, 1).toLong();
        emit workerReady(cap);
        // Ask the worker for its PID so the GUI can point MemMapWidget at it.
        send("GETPID");
        return;
    }
    if (line.startsWith("WORKERPID")) {
        // Unregister the QProcess-level PID (it IS the worker PID for a QProcess,
        // but register the inner PID reported by the worker too, in case they differ).
        pidVal = (pid_t)line.section(' ', 1, 1).toLong();
        if (pidVal > 0 && pidVal != (pid_t)qpid)
            LearnOSCleanup::registerPid(pidVal);
        emit workerPidKnown(pidVal);
        return;
    }
    if (line.startsWith("BLOCK")) {
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 6) {
            ArenaBlock b;
            b.id       = parts[1].toInt();
            b.offset   = parts[2].toLong();
            b.size     = parts[3].toLong();
            b.used     = parts[4].toInt() != 0;
            b.fillByte = parts[5].toInt();
            if (b.used) lastBlock = b.id;
            pendingBlocks.push_back(b);
        }
        return;
    }
    if (line == "SIGSEGV_TRIGGERED") {
        emit commandFailed("SIGSEGV triggered (caught by sigaction handler) — page fault on write to protected page");
        return;
    }
    if (line.startsWith("COWFORK")) {
        // Just forward as a status message
        emit commandFailed("COW fork: " + line.mid(7).trimmed());
        return;
    }
    if (line.startsWith("ARENA")) {
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        ArenaSummary s;
        if (parts.size() >= 6) {
            s.totalBytes     = parts[1].toLong();
            s.usedBytes      = parts[2].toLong();
            s.freeBytes      = parts[3].toLong();
            s.blockCount     = parts[4].toInt();
            s.largestFreeRun = parts[5].toLong();
        }
        emit arenaUpdated(pendingBlocks, s);
        pendingBlocks.clear();
        return;
    }
    if (line == "OK") {
        return; // terminator for a successful multi-line response; state already emitted
    }
    if (line.startsWith("ERR")) {
        emit commandFailed(line.mid(4));
        pendingBlocks.clear();
        return;
    }
    if (line == "PONG") {
        return;
    }
}

void MemoryLabDriver::onProcessError(QProcess::ProcessError) {
    emit commandFailed("Worker process error — it may need a restart.");
}

void MemoryLabDriver::onProcessFinished(int, QProcess::ExitStatus) {
    emit workerDied();
}
