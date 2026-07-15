#pragma once
#include <signal.h>
#include <QString>
// ─────────────────────────────────────────────────────────────────────────────
// EventBus — the central nervous system of LearnOS.
//
// Every significant OS event flows through here. Any widget can:
//   - emit an event:   EventBus::get().emit(event)
//   - listen:          connect(&EventBus::get(), &EventBus::osEvent, ...)
//
// This is what makes actions in one tab visibly ripple into others.
// ─────────────────────────────────────────────────────────────────────────────
#include <QObject>
#include <QString>
#include <unistd.h>

struct OSEvent {
    enum Type {
        // Process lifecycle
        ProcessSpawned,     // a new sandbox process was created
        ProcessKilled,      // a process was killed
        ProcessPaused,      // SIGSTOP sent
        ProcessResumed,     // SIGCONT sent
        SignalSent,         // any signal sent to any process

        // Memory
        MemoryAllocated,    // memory allocated (DS lab, memory lab)
        MemoryFreed,        // memory freed
        MemoryPressureHigh, // RSS > 80% of total

        // IPC
        IPCChannelCreated,  // pipe/shm/socket created
        IPCDataSent,        // data sent through channel
        IPCChannelDestroyed,

        // Scheduler
        SchedAlgoChanged,   // scheduling algorithm changed
        SchedTick,          // one scheduling tick happened
        SchedProcessDone,   // a process finished in the stepper

        // Data structures
        DSOperation,        // push/pop/insert/remove/search

        // System
        CPUHighLoad,        // any core > 80%
        SwapActive,         // kernel started swapping

        // Observability (EbpfLab)
        PerfCounterTick,    // one perf_event_open() sample interval
        FtraceEvent,        // one batch of ftrace lines read from trace_pipe

        // Filesystem (FilesystemLab)
        FilesystemEvent,    // inotify event — create/delete/modify/open/close/…
    };

    Type    type;
    pid_t   pid     = -1;       // relevant PID if any
    int     signum  = 0;        // signal number if any
    QString detail;             // human-readable what happened
    long    valueLong = 0;      // bytes, KB, count, etc
    QString extra;              // extra context
};

class EventBus : public QObject {
    Q_OBJECT

public:
    // Singleton
    static EventBus& get() {
        static EventBus instance;
        return instance;
    }

    // Fire an event — all connected listeners receive it
    void fire(const OSEvent& event) {
        emit osEvent(event);
    }

    // Convenience fire methods
    void processSpawned(pid_t pid, const QString& name, const QString& type) {
        OSEvent e; e.type=OSEvent::ProcessSpawned; e.pid=pid; e.detail=QString("Spawned %1 (%2)").arg(name).arg(type); e.extra=type; fire(e);
    }
    void processKilled(pid_t pid, const QString& name) {
        OSEvent e; e.type=OSEvent::ProcessKilled; e.pid=pid; e.detail=QString("Killed %1 (PID %2)").arg(name).arg(pid); fire(e);
    }
    void processPaused(pid_t pid) {
        OSEvent e; e.type=OSEvent::ProcessPaused; e.pid=pid; e.signum=SIGSTOP;
        e.detail=QString("PID %1 paused (SIGSTOP)").arg(pid); fire(e);
    }
    void processResumed(pid_t pid) {
        OSEvent e; e.type=OSEvent::ProcessResumed; e.pid=pid; e.signum=SIGCONT;
        e.detail=QString("PID %1 resumed (SIGCONT)").arg(pid); fire(e);
    }
    void signalSent(pid_t pid, int signum, const QString& sigName, const QString& result) {
        OSEvent e; e.type=OSEvent::SignalSent; e.pid=pid; e.signum=signum; e.detail=QString("%1 → PID %2: %3").arg(sigName).arg(pid).arg(result); e.extra=result; fire(e);
    }
    void memoryAllocated(pid_t pid, long bytes, const QString& source) {
        OSEvent e; e.type=OSEvent::MemoryAllocated; e.pid=pid; e.detail=QString("%1 allocated %2 KB").arg(source).arg(bytes/1024); e.valueLong=bytes; e.extra=source; fire(e);
    }
    void memoryFreed(pid_t pid, long bytes) {
        OSEvent e; e.type=OSEvent::MemoryFreed; e.pid=pid; e.detail=QString("PID %1 freed %2 KB").arg(pid).arg(bytes/1024); e.valueLong=bytes; fire(e);
    }
    void ipcCreated(const QString& type, pid_t sender, pid_t receiver) {
        OSEvent e; e.type=OSEvent::IPCChannelCreated; e.pid=sender; e.detail=QString("%1 created: PID %2 → PID %3").arg(type).arg(sender).arg(receiver); e.extra=type; fire(e);
    }
    void ipcDataSent(const QString& channelName, long bytes) {
        OSEvent e; e.type=OSEvent::IPCDataSent; e.detail=QString("%1: %2 bytes sent").arg(channelName).arg(bytes); e.valueLong=bytes; e.extra=channelName; fire(e);
    }
    void dsOperation(const QString& op, int value, const QString& dsType, long rssKB) {
        OSEvent e; e.type=OSEvent::DSOperation; e.detail=QString("%1 %2 on %3").arg(op).arg(value).arg(dsType); e.valueLong=rssKB; e.extra=dsType; fire(e);
    }
    void schedTick(int tick, const QString& algo, const QString& running) {
        OSEvent e; e.type=OSEvent::SchedTick; e.detail=QString("Tick %1 [%2]: running %3").arg(tick).arg(algo).arg(running); e.valueLong=tick; e.extra=running; fire(e);
    }

signals:
    void osEvent(OSEvent event);

private:
    EventBus() : QObject(nullptr) {}
};
