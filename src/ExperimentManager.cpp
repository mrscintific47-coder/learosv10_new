#include "ExperimentManager.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/mman.h>

// ── Kernel parameter I/O ─────────────────────────────────────────────────────

bool ExperimentManager::writeSysFile(const QString& path, const QString& value) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream s(&f);
    s << value;
    return true;
}

QString ExperimentManager::readSysFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return "unavailable";
    return f.readAll().trimmed();
}

bool ExperimentManager::setSchedLatency(long ns) {
    bool ok = writeSysFile("/proc/sys/kernel/sched_latency_ns", QString::number(ns));
    if (ok) {
        emit kernelParamChanged("sched_latency_ns", QString::number(ns));
        emit explanationNeeded(QString(
            "<b>Kernel parameter changed: sched_latency_ns = %1 ns</b><br><br>"
            "This is the target latency for CFS (Completely Fair Scheduler). "
            "Every runnable process gets to run at least once per this period.<br><br>"
            "<b>Lower value (%1 ns = %2 ms)</b> → more responsive, more context switches<br>"
            "<b>Higher value</b> → better throughput, less switching overhead<br><br>"
            "Watch the process slots on the heatmap — switching frequency will change."
        ).arg(ns).arg(ns/1000000.0, 0, 'f', 1));
    } else {
        emit kernelWriteFailed("sched_latency_ns", QString::number(ns));
    }
    return ok;
}

bool ExperimentManager::setSwappiness(int value) {
    bool ok = writeSysFile("/proc/sys/vm/swappiness", QString::number(value));
    if (ok) {
        emit kernelParamChanged("vm.swappiness", QString::number(value));
        emit explanationNeeded(QString(
            "<b>Kernel parameter changed: vm.swappiness = %1</b><br><br>"
            "Controls how aggressively the kernel swaps memory pages to disk.<br><br>"
            "<b>0</b> = avoid swapping, keep everything in RAM<br>"
            "<b>60</b> = default Linux behavior<br>"
            "<b>100</b> = swap aggressively<br><br>"
            "Current value: <b>%1</b><br>"
            "Spawn a Memory Eater and watch how this affects RSS and swap usage."
        ).arg(value));
    } else {
        emit kernelWriteFailed("vm.swappiness", QString::number(value));
    }
    return ok;
}

bool ExperimentManager::setOvercommit(int value) {
    bool ok = writeSysFile("/proc/sys/vm/overcommit_memory", QString::number(value));
    if (ok) {
        emit kernelParamChanged("vm.overcommit_memory", QString::number(value));
        emit explanationNeeded(QString(
            "<b>Kernel parameter changed: vm.overcommit_memory = %1</b><br><br>"
            "<b>0</b> = heuristic overcommit (default) — kernel estimates if allocation will succeed<br>"
            "<b>1</b> = always overcommit — malloc() never fails, but OOM killer may strike later<br>"
            "<b>2</b> = never overcommit — fail immediately if not enough RAM<br><br>"
            "Current: <b>%2</b><br>"
            "This directly affects how your Memory Eater behaves."
        ).arg(value).arg(value==0?"heuristic":value==1?"always overcommit":"never overcommit"));
    } else {
        emit kernelWriteFailed("vm.overcommit_memory", QString::number(value));
    }
    return ok;
}

bool ExperimentManager::setDirtyRatio(int value) {
    bool ok = writeSysFile("/proc/sys/vm/dirty_ratio", QString::number(value));
    if (ok) emit kernelParamChanged("vm.dirty_ratio", QString::number(value));
    else emit kernelWriteFailed("vm.dirty_ratio", QString::number(value));
    return ok;
}

long ExperimentManager::readSchedLatency() {
    return readSysFile("/proc/sys/kernel/sched_latency_ns").toLong();
}
int ExperimentManager::readSwappiness() {
    return readSysFile("/proc/sys/vm/swappiness").toInt();
}
int ExperimentManager::readOvercommit() {
    return readSysFile("/proc/sys/vm/overcommit_memory").toInt();
}

// ── Available experiments ────────────────────────────────────────────────────

QVector<Experiment> ExperimentManager::availableExperiments() {
    return {
        { Experiment::SchedulerComparison, "Scheduler Comparison",
          "Run 4 CPU-bound processes under FCFS, Round Robin, Priority, and SJF. "
          "Compare wait time, turnaround, and CPU utilization side by side.", 40, "fcfs", 4 },

        { Experiment::MemoryPressure, "Memory Pressure Lab",
          "Gradually fill RAM and watch the kernel respond — page reclamation, "
          "swap activation, OOM killer behavior. Modify swappiness live.", 30, "", 3 },

        { Experiment::IPCThroughput, "IPC Throughput Benchmark",
          "Measure real throughput of Pipe vs Shared Memory vs Unix Socket. "
          "Send 10MB through each and compare latency and bandwidth.", 20, "", 0 },

        { Experiment::SignalCascade, "Signal Cascade Demo",
          "Spawn a process tree, fire signals, watch how they propagate. "
          "See SIGTERM vs SIGKILL, signal inheritance, and signal masking.", 15, "", 5 },

        { Experiment::ProcessLifecycle, "Full Process Lifecycle",
          "Watch a complete fork()→exec()→wait()→exit() cycle with every "
          "state transition visible in real time.", 10, "", 1 },

        { Experiment::PageFaultStorm, "Page Fault Storm",
          "Trigger thousands of page faults by accessing unmapped memory. "
          "Watch /proc/vmstat counters spike in real time.", 20, "", 2 },
    };
}

// ── Experiment orchestration ─────────────────────────────────────────────────

void ExperimentManager::runExperiment(const Experiment& exp) {
    if (experimentRunning) stopExperiment();

    currentExp      = exp;
    currentTick     = 0;
    contextSwitches = 0;
    lastRunning     = "";
    processes.clear();
    result = ExpResult{};
    result.algo = exp.algo;

    experimentRunning = true;

    emit experimentStarted(exp);
    emit stageChanged("Initializing", "Setting up experiment environment...");

    // Stage 1: announce what we're doing
    emit explanationNeeded(QString(
        "<b>🔬 Experiment Starting: %1</b><br><br>"
        "%2<br><br>"
        "<b>Duration:</b> %3 ticks<br>"
        "<b>Processes:</b> %4<br><br>"
        "Watch the heatmap, process slots, and activity feed — "
        "everything will update in real time as the experiment runs."
    ).arg(exp.name).arg(exp.description).arg(exp.durationTicks).arg(exp.processCount));

    // Stage 2: spawn processes based on experiment type
    QTimer::singleShot(500, this, [this]() {
        emit stageChanged("Spawning", "Creating experiment processes...");

        if (currentExp.type == Experiment::SchedulerComparison) {
            spawnProcesses(currentExp.processCount, {"cpu","cpu","memory","mixed"});
        } else if (currentExp.type == Experiment::MemoryPressure) {
            spawnProcesses(3, {"memory","memory","memory"});
        } else if (currentExp.type == Experiment::SignalCascade) {
            spawnProcesses(currentExp.processCount, {"idle","idle","idle","idle","idle"});
        } else if (currentExp.type == Experiment::ProcessLifecycle) {
            spawnProcesses(1, {"lifecycle"});
        } else if (currentExp.type == Experiment::PageFaultStorm) {
            spawnProcesses(2, {"pagefault","pagefault"});
        } else {
            spawnProcesses(currentExp.processCount, {"cpu"});
        }

        // Stage 3: start ticking
        QTimer::singleShot(500, this, [this]() {
            emit stageChanged("Running", QString("Experiment in progress — %1 ticks").arg(currentExp.durationTicks));
            tickTimer = new QTimer(this);
            connect(tickTimer, &QTimer::timeout, this, &ExperimentManager::onTick);
            tickTimer->start(600); // one tick every 600ms — visible but not too fast
        });
    });
}

void ExperimentManager::stopExperiment() {
    if (tickTimer) { tickTimer->stop(); tickTimer->deleteLater(); tickTimer = nullptr; }
    killAllProcesses();
    experimentRunning = false;
}

void ExperimentManager::spawnProcesses(int count, const QStringList& workloads) {
    for (int i = 0; i < count; i++) {
        QString wl = i < workloads.size() ? workloads[i] : "cpu";
        ExpProcess p;
        p.name     = QString("%1_%2").arg(wl).arg(i+1);
        p.workload = wl;
        p.priority = i * 5; // 0, 5, 10, 15 — varied priorities
        p.burstLeft = 8 + i * 4; // varied burst times
        p.alive    = false;
        p.running  = false;

        // Fork real child
        pid_t pid = fork();
        if (pid == 0) {
            // Child — nice=19 so it can't hurt the system
            nice(19);
            if (wl == "cpu") {
                volatile double x = 1.0;
                while(true) x = std::sin(x)*std::cos(x)+1.0001;
            } else if (wl == "memory") {
                while(true) {
                    char* m = (char*)malloc(10*1024*1024);
                    if(m) { for(size_t j=0;j<10*1024*1024;j+=4096) m[j]=1; }
                    sleep(2); free(m);
                }
            } else if (wl == "io") {
                char buf[4096]; memset(buf,0xAB,sizeof(buf));
                char path[]="/tmp/learnos_exp_XXXXXX";
                int fd=mkstemp(path);
                while(fd>=0){ write(fd,buf,sizeof(buf)); lseek(fd,0,SEEK_SET); read(fd,buf,sizeof(buf)); }
            } else if (wl == "pagefault") {
                // Access memory in random order to maximize page faults
                const size_t SIZE = 100*1024*1024;
                char* m = (char*)mmap(nullptr,SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
                if(m!=MAP_FAILED) {
                    srand(getpid());
                    while(true) { size_t idx=(size_t)rand()%SIZE; m[idx]++; }
                }
            } else {
                // idle — just sleep, used for signal experiments
                while(true) sleep(1);
            }
            _exit(0);
        }

        if (pid > 0) {
            p.pid   = pid;
            p.alive = true;
            // Start paused — experiment manager controls who runs
            kill(pid, SIGSTOP);
            processes.append(p);
            EventBus::get().processSpawned(pid, p.name, wl);
        }
    }

    emit explanationNeeded(QString(
        "<b>%1 processes spawned</b><br><br>"
        "All processes are currently <b>paused</b> (SIGSTOP). "
        "The experiment manager will control exactly which process runs "
        "on each tick, implementing the scheduling algorithm in real time.<br><br>"
        "Each process has a different workload and burst time — "
        "this is what makes the scheduler comparison meaningful."
    ).arg(processes.size()));
}

void ExperimentManager::killAllProcesses() {
    for (auto& p : processes) {
        if (p.pid > 0 && p.alive) {
            kill(p.pid, SIGCONT); // unfreeze first
            kill(p.pid, SIGKILL);
            waitpid(p.pid, nullptr, WNOHANG);
            p.alive = false;
        }
    }
    processes.clear();
}

// ── Tick — called every 600ms during an experiment ───────────────────────────

void ExperimentManager::onTick() {
    if (!experimentRunning) return;
    currentTick++;

    // Collect metrics for all processes
    collectMetrics();

    // Run the appropriate algorithm for one tick
    if (currentExp.type == Experiment::SchedulerComparison) {
        if      (currentExp.algo == "fcfs")     tickFCFS();
        else if (currentExp.algo == "rr")       tickRoundRobin();
        else if (currentExp.algo == "priority") tickPriority();
        else if (currentExp.algo == "sjf")      tickSJF();
    } else if (currentExp.type == Experiment::MemoryPressure) {
        // All memory processes run freely — just observe
        for (auto& p : processes) {
            if (p.alive && !p.running) {
                kill(p.pid, SIGCONT);
                p.running = true;
            }
        }
        // Check memory pressure
        std::ifstream mi("/proc/meminfo");
        std::string line; long total=0, avail=0;
        while(std::getline(mi,line)){
            if(line.rfind("MemTotal:",0)==0){std::istringstream ss(line.substr(9));ss>>total;}
            if(line.rfind("MemAvailable:",0)==0){std::istringstream ss(line.substr(13));ss>>avail;}
        }
        float used = total>0 ? (1.0f-(float)avail/total)*100.0f : 0;
        if(used > 80.0f) {
            OSEvent e; e.type=OSEvent::MemoryPressureHigh;
            e.detail=QString("Memory at %1% — kernel may start swapping").arg((int)used);
            e.valueLong=(long)used;
            EventBus::get().fire(e);
        }
    } else if (currentExp.type == Experiment::SignalCascade) {
        // Send different signals each few ticks to demonstrate cascade
        if (currentTick == 3 && !processes.isEmpty())
            kill(processes[0].pid, SIGUSR1);
        if (currentTick == 6 && processes.size()>1)
            kill(processes[1].pid, SIGTERM);
        if (currentTick == 9 && processes.size()>2)
            kill(processes[2].pid, SIGSTOP);
        if (currentTick == 12 && processes.size()>2)
            kill(processes[2].pid, SIGCONT);
    } else if (currentExp.type == Experiment::PageFaultStorm) {
        // All pagefault processes run freely — watch vmstat
        for (auto& p : processes) {
            if (p.alive && !p.running) {
                kill(p.pid, SIGCONT);
                p.running = true;
            }
        }
    }

    // Emit tick with current state
    emit experimentTick(currentTick, currentExp.durationTicks, processes);

    // Record metric
    QString running;
    for (auto& p : processes) if (p.running) { running = p.name; break; }
    ExpMetric m;
    m.tick = currentTick; m.runningProcess = running;
    m.algo = currentExp.algo;
    long totalRss = 0;
    for (auto& p : processes) totalRss += p.rssKB;
    m.totalRssKB = totalRss;
    m.contextSwitches = contextSwitches;
    result.timeline.append(m);

    // Done?
    if (currentTick >= currentExp.durationTicks) {
        tickTimer->stop();
        buildResult();
        experimentRunning = false;
        emit experimentFinished(result);
        emit stageChanged("Complete", "Experiment finished — results ready");
        killAllProcesses();
    }
}

// ── Scheduling algorithms ─────────────────────────────────────────────────────

void ExperimentManager::tickFCFS() {
    // Find first non-done process
    for (auto& p : processes) {
        if (!p.alive || p.burstLeft <= 0) continue;

        // Pause everyone else
        for (auto& other : processes)
            if (other.pid != p.pid && other.running) {
                kill(other.pid, SIGSTOP);
                other.running = false;
            }

        // Run this one
        if (!p.running) {
            kill(p.pid, SIGCONT);
            p.running = true;
            if (lastRunning != p.name) {
                contextSwitches++;
                lastRunning = p.name;
                EventBus::get().schedTick(currentTick, "FCFS", p.name);
                emit explanationNeeded(QString(
                    "<b>FCFS Tick %1 — Running: %2</b><br><br>"
                    "Burst remaining: <b>%3</b> ticks<br>"
                    "Wait time: <b>%4</b> ticks<br><br>"
                    "FCFS runs this process until it finishes. "
                    "All others wait — even if they arrived first and need less time.<br><br>"
                    "<i>Watch: processes behind it are paused (SIGSTOP) on the heatmap.</i>"
                ).arg(currentTick).arg(p.name).arg(p.burstLeft).arg(p.waitTicks));
            }
        }
        p.burstLeft--;
        p.runTicks++;
        if (p.burstLeft <= 0) {
            kill(p.pid, SIGSTOP);
            p.running = false;
            { OSEvent e; e.type=OSEvent::SchedProcessDone;
              e.pid=p.pid; e.detail=QString("%1 finished (FCFS)").arg(p.name);
              EventBus::get().fire(e); }
        }
        // Increment wait for all others
        for (auto& other : processes)
            if (other.pid != p.pid && other.burstLeft > 0) other.waitTicks++;
        return;
    }
}

void ExperimentManager::tickRoundRobin() {
    static int rrIndex = 0;
    static int rrTick  = 0;
    const int  quantum = 3;

    // Find next alive process
    int tried = 0;
    while (tried < processes.size()) {
        int idx = rrIndex % processes.size();
        ExpProcess& p = processes[idx];
        if (p.alive && p.burstLeft > 0) {
            // Pause all others
            for (int i=0;i<processes.size();i++) {
                if (i != idx && processes[i].running) {
                    kill(processes[i].pid, SIGSTOP);
                    processes[i].running = false;
                }
            }
            // Run this one
            if (!p.running) {
                kill(p.pid, SIGCONT);
                p.running = true;
                if (lastRunning != p.name) {
                    contextSwitches++;
                    lastRunning = p.name;
                    EventBus::get().schedTick(currentTick, "Round Robin", p.name);
                    emit explanationNeeded(QString(
                        "<b>Round Robin Tick %1 — Running: %2</b><br><br>"
                        "Quantum tick: <b>%3/%4</b><br>"
                        "Burst remaining: <b>%5</b><br><br>"
                        "After %4 ticks this process goes to the back of the queue "
                        "regardless of whether it's finished."
                    ).arg(currentTick).arg(p.name).arg(rrTick+1).arg(quantum).arg(p.burstLeft));
                }
            }
            p.burstLeft--; p.runTicks++; rrTick++;
            for (auto& other : processes)
                if (other.pid != p.pid && other.burstLeft > 0) other.waitTicks++;

            if (p.burstLeft <= 0) {
                kill(p.pid, SIGSTOP); p.running=false; rrTick=0; rrIndex++;
            } else if (rrTick >= quantum) {
                kill(p.pid, SIGSTOP); p.running=false; rrTick=0; rrIndex++;
            }
            return;
        }
        rrIndex++; tried++;
    }
}

void ExperimentManager::tickPriority() {
    // Find alive process with lowest nice (highest priority)
    int bestIdx = -1;
    for (int i=0;i<processes.size();i++) {
        auto& p = processes[i];
        if (!p.alive || p.burstLeft <= 0) continue;
        if (bestIdx==-1 || p.priority < processes[bestIdx].priority) bestIdx=i;
    }
    if (bestIdx < 0) return;

    ExpProcess& best = processes[bestIdx];
    for (int i=0;i<processes.size();i++) {
        if (i!=bestIdx && processes[i].running) {
            kill(processes[i].pid, SIGSTOP);
            processes[i].running=false;
        }
    }
    if (!best.running) {
        kill(best.pid, SIGCONT); best.running=true;
        if (lastRunning != best.name) {
            contextSwitches++;
            lastRunning = best.name;
            EventBus::get().schedTick(currentTick, "Priority", best.name);
            emit explanationNeeded(QString(
                "<b>Priority Tick %1 — Running: %2</b><br><br>"
                "Priority (nice): <b>%3</b> — lowest value wins<br>"
                "Burst remaining: <b>%4</b><br><br>"
                "Higher-priority processes always preempt lower ones. "
                "Watch lower-priority processes never get CPU."
            ).arg(currentTick).arg(best.name).arg(best.priority).arg(best.burstLeft));
        }
    }
    best.burstLeft--; best.runTicks++;
    if (best.burstLeft<=0) { kill(best.pid,SIGSTOP); best.running=false; }
    for (auto& p : processes)
        if (p.pid!=best.pid && p.burstLeft>0) p.waitTicks++;
}

void ExperimentManager::tickSJF() {
    // Find alive process with shortest burst remaining
    int bestIdx = -1;
    for (int i=0;i<processes.size();i++) {
        auto& p = processes[i];
        if (!p.alive || p.burstLeft <= 0) continue;
        if (bestIdx==-1 || p.burstLeft < processes[bestIdx].burstLeft) bestIdx=i;
    }
    if (bestIdx < 0) return;

    ExpProcess& best = processes[bestIdx];
    for (int i=0;i<processes.size();i++) {
        if (i!=bestIdx && processes[i].running) {
            kill(processes[i].pid, SIGSTOP);
            processes[i].running=false;
        }
    }
    if (!best.running) {
        kill(best.pid, SIGCONT); best.running=true;
        if (lastRunning != best.name) {
            contextSwitches++;
            lastRunning = best.name;
            EventBus::get().schedTick(currentTick, "SJF", best.name);
            emit explanationNeeded(QString(
                "<b>SJF Tick %1 — Running: %2</b><br><br>"
                "Burst remaining: <b>%3</b> — shortest of all alive processes<br><br>"
                "SJF always picks the process closest to finishing. "
                "This minimizes average wait time but can starve long processes forever."
            ).arg(currentTick).arg(best.name).arg(best.burstLeft));
        }
    }
    best.burstLeft--; best.runTicks++;
    if (best.burstLeft<=0) { kill(best.pid,SIGSTOP); best.running=false; }
    for (auto& p : processes)
        if (p.pid!=best.pid && p.burstLeft>0) p.waitTicks++;
}

// ── Metrics ──────────────────────────────────────────────────────────────────

void ExperimentManager::collectMetrics() {
    for (auto& p : processes) {
        if (!p.alive) continue;
        if (kill(p.pid, 0) != 0) { p.alive=false; continue; }
        p.rssKB     = readProcessRss(p.pid);
        p.cpuPercent = readProcessCpu(p.pid);
    }
}

float ExperimentManager::readProcessCpu(pid_t pid) {
    std::ifstream f("/proc/"+std::to_string(pid)+"/stat");
    if (!f.is_open()) return 0;
    std::string s; std::getline(f,s);
    std::istringstream ss(s); std::string tok;
    std::vector<std::string> fields;
    while(ss>>tok) fields.push_back(tok);
    if (fields.size()<15) return 0;
    long long cpu = std::stoll(fields[13])+std::stoll(fields[14]);
    return cpu > 0 ? std::min((float)cpu/100.0f, 100.0f) : 0;
}

long ExperimentManager::readProcessRss(pid_t pid) {
    std::ifstream f("/proc/"+std::to_string(pid)+"/status");
    std::string line;
    while(std::getline(f,line))
        if(line.rfind("VmRSS:",0)==0){
            std::istringstream ss(line.substr(6)); long v; ss>>v; return v;
        }
    return 0;
}

// ── Result building ───────────────────────────────────────────────────────────

void ExperimentManager::buildResult() {
    result.totalTicks      = currentTick;
    result.contextSwitches = contextSwitches;

    float totalWait = 0, totalTurnaround = 0;
    int   count     = 0;
    float totalRun  = 0;

    for (auto& p : processes) {
        totalWait       += p.waitTicks;
        totalTurnaround += p.waitTicks + p.runTicks;
        totalRun        += p.runTicks;
        count++;
    }

    result.avgWaitTime    = count > 0 ? totalWait / count : 0;
    result.avgTurnaround  = count > 0 ? totalTurnaround / count : 0;
    result.cpuUtilization = currentTick > 0 ? (totalRun / (currentTick * count)) * 100.0f : 0;

    emit explanationNeeded(QString(
        "<b>🏁 Experiment Complete: %1</b><br><br>"
        "<table style='font-size:12px;width:100%%;'>"
        "<tr><td><b>Algorithm</b></td><td>%2</td></tr>"
        "<tr><td><b>Total ticks</b></td><td>%3</td></tr>"
        "<tr><td><b>Avg wait time</b></td><td>%4 ticks</td></tr>"
        "<tr><td><b>Avg turnaround</b></td><td>%5 ticks</td></tr>"
        "<tr><td><b>Context switches</b></td><td>%6</td></tr>"
        "<tr><td><b>CPU utilization</b></td><td>%7%</td></tr>"
        "</table><br>"
        "<b>What this means:</b><br>"
        "%8"
    ).arg(currentExp.name)
     .arg(result.algo.isEmpty() ? currentExp.name : result.algo)
     .arg(result.totalTicks)
     .arg(result.avgWaitTime, 0, 'f', 1)
     .arg(result.avgTurnaround, 0, 'f', 1)
     .arg(result.contextSwitches)
     .arg(result.cpuUtilization, 0, 'f', 1)
     .arg(result.contextSwitches > 20 ?
        "High context switch count — the algorithm is sharing CPU fairly "
        "but paying overhead for each switch." :
        result.avgWaitTime > 10 ?
        "High average wait time — some processes waited a long time "
        "before getting CPU. Try Round Robin for better fairness." :
        "Good balance between wait time and CPU utilization."));
}
