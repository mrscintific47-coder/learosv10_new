#include "DSLabDriver.h"
#include "CleanupRegistry.h"
#include "EventBus.h"
#include <QCoreApplication>
#include <QFileInfo>

DSLabDriver::DSLabDriver(QObject* parent) : QObject(parent) {
    proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this](){onReadyRead();});
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e){onError(e);});
    connect(proc, &QProcess::finished, this, [this](int c, QProcess::ExitStatus s){onFinished(c,s);});
}
DSLabDriver::~DSLabDriver() { stop(); }

QString DSLabDriver::findBinary() {
    QString d = QCoreApplication::applicationDirPath();
    for (auto& c : QStringList{d+"/dslab_worker", d+"/../tools/dslab_worker"})
        if (QFileInfo::exists(c)) return c;
    return d+"/dslab_worker";
}
void DSLabDriver::start() {
    if (proc->state()!=QProcess::NotRunning) return;
    buf.clear(); nodeBuffer.clear(); hasOp=false;
    proc->start(findBinary(), {});
    if (proc->state()!=QProcess::NotRunning) {
        qpid = proc->processId();
        if (qpid > 0) LearnOSCleanup::registerPid((pid_t)qpid);
    }
}
void DSLabDriver::stop() {
    if (proc->state()==QProcess::NotRunning) return;
    if (qpid > 0) { LearnOSCleanup::unregisterPid((pid_t)qpid); qpid = -1; }
    if (pidVal > 0) { LearnOSCleanup::unregisterPid(pidVal); pidVal = -1; }
    proc->kill(); proc->waitForFinished(500);
}
bool DSLabDriver::isRunning() const { return proc->state()==QProcess::Running; }
void DSLabDriver::send(const QString& l) {
    if (!isRunning()) { emit commandFailed("Worker not running"); return; }
    proc->write((l+"\n").toUtf8());
}
void DSLabDriver::setDS(const QString& t) { nodeBuffer.clear(); hasOp=false; send("DS "+t); }
void DSLabDriver::push(int v)    { send(QString("PUSH %1").arg(v)); }
void DSLabDriver::pop()          { send("POP"); }
void DSLabDriver::enqueue(int v) { send(QString("ENQUEUE %1").arg(v)); }
void DSLabDriver::dequeue()      { send("DEQUEUE"); }
void DSLabDriver::insert(int v)  { send(QString("INSERT %1").arg(v)); }
void DSLabDriver::remove(int v)  { send(QString("REMOVE %1").arg(v)); }
void DSLabDriver::search(int v)  { send(QString("SEARCH %1").arg(v)); }
void DSLabDriver::stress(int n)  { send(QString("STRESS %1").arg(n)); }
void DSLabDriver::clear()        { nodeBuffer.clear(); hasOp=false; send("CLEAR"); }
void DSLabDriver::status()       { send("STATUS"); }

void DSLabDriver::onReadyRead() {
    buf += proc->readAllStandardOutput();
    while (true) {
        int nl = buf.indexOf('\n'); if (nl<0) break;
        QString line = QString::fromUtf8(buf.left(nl)).trimmed();
        buf.remove(0, nl+1);
        if (!line.isEmpty()) processLine(line);
    }
}
void DSLabDriver::processLine(const QString& line) {
    QStringList p = line.split(' ', Qt::SkipEmptyParts);
    if (p.isEmpty()) return;
    if (p[0]=="READY") { pidVal=p.size()>1?p[1].toLongLong():-1; emit workerReady(pidVal); return; }
    if (p[0]=="NODE"&&p.size()>=4) {
        DSWorkerNode n; n.type="node"; n.address=p[1]; n.value=p[2].toInt(); n.nextAddr=p[3];
        nodeBuffer.push_back(n); return;
    }
    if (p[0]=="HNODE"&&p.size()>=6) {
        DSWorkerNode n; n.type="hnode"; n.address=p[1]; n.bucket=p[2].toInt();
        n.hmKey=p[3].toInt(); n.hmValue=p[4].toInt(); n.nextAddr=p[5];
        nodeBuffer.push_back(n); return;
    }
    if (p[0]=="BNODE"&&p.size()>=5) {
        DSWorkerNode n; n.type="bnode"; n.address=p[1]; n.value=p[2].toInt();
        n.leftAddr=p[3]; n.rightAddr=p[4];
        nodeBuffer.push_back(n); return;
    }
    if (p[0]=="OP"&&p.size()>=5) {
        pendingOp.operation=p[1]; pendingOp.value=p[2].toInt();
        pendingOp.address=p[3]; pendingOp.result=p[4]; hasOp=true; return;
    }
    if (p[0]=="SUMMARY"&&p.size()>=6) {
        DSWorkerSummary s; s.dsType=p[1]; s.size=p[2].toInt();
        s.rssKB=p[3].toLong(); s.heapStart=p[4]; s.heapEnd=p[5];
        if (hasOp) {
            EventBus::get().dsOperation(pendingOp.operation, pendingOp.value, s.dsType, s.rssKB);
            if (pendingOp.operation=="PUSH"||pendingOp.operation=="ENQUEUE"||pendingOp.operation=="INSERT")
                EventBus::get().memoryAllocated(pidVal, s.rssKB*1024, "DS Lab");
            else if (pendingOp.operation=="POP"||pendingOp.operation=="DEQUEUE"||pendingOp.operation=="REMOVE")
                EventBus::get().memoryFreed(pidVal, 64);
        }
        emit stateUpdated(nodeBuffer, s, hasOp?pendingOp:DSWorkerOp{});
        nodeBuffer.clear(); hasOp=false; return;
    }
    if (p[0]=="STRESS_DONE"&&p.size()>=4) {
        DSStressResult r; r.count=p[1].toInt(); r.ms=p[2].toLong(); r.rssKB=p[3].toLong();
        emit stressCompleted(r); return;
    }
    if (p[0]=="OK") return;
    if (p[0]=="ERR") { emit commandFailed(line.mid(4)); return; }
}
void DSLabDriver::onError(QProcess::ProcessError) { emit commandFailed("Worker process error"); }
void DSLabDriver::onFinished(int,QProcess::ExitStatus) { pidVal=-1; emit workerDied(); }
