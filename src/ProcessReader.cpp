#include "ProcessReader.h"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <map>
#include <chrono>
#include <thread>

// We store previous CPU times to calculate % usage between reads
static std::map<int, std::pair<long long, long long>> prevCpuTimes;
static long long prevTotalCpu = 0;

// Read total CPU time from /proc/stat
static long long getTotalCpuTime() {
    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line);
    std::istringstream ss(line);
    std::string label;
    ss >> label;
    long long total = 0, val;
    while (ss >> val) total += val;
    return total;
}

// Read process CPU time from /proc/[pid]/stat
static long long getProcessCpuTime(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) return 0;
    std::string line;
    std::getline(f, line);
    // Fields 14 and 15 are utime and stime (after the comm field)
    // comm can have spaces so we find the last ')' first
    size_t pos = line.rfind(')');
    if (pos == std::string::npos) return 0;
    std::istringstream ss(line.substr(pos + 2));
    std::string state;
    ss >> state;
    long long ppid, pgrp, session, tty, tpgid, flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    ss >> ppid >> pgrp >> session >> tty >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;
    return utime + stime;
}

float ProcessReader::calculateCpu(int pid) {
    long long totalNow = getTotalCpuTime();
    long long procNow = getProcessCpuTime(pid);

    if (prevCpuTimes.count(pid) == 0 || prevTotalCpu == 0) {
        prevCpuTimes[pid] = {procNow, totalNow};
        prevTotalCpu = totalNow;
        return 0.0f;
    }

    long long procDelta = procNow - prevCpuTimes[pid].first;
    long long totalDelta = totalNow - prevTotalCpu;

    prevCpuTimes[pid] = {procNow, totalNow};
    prevTotalCpu = totalNow;

    if (totalDelta == 0) return 0.0f;
    return 100.0f * (float)procDelta / (float)totalDelta;
}

std::string ProcessReader::getProcessUser(int pid) {
    struct stat st;
    std::string path = "/proc/" + std::to_string(pid);
    if (stat(path.c_str(), &st) != 0) return "unknown";
    struct passwd* pw = getpwuid(st.st_uid);
    if (pw) return pw->pw_name;
    return std::to_string(st.st_uid);
}

bool ProcessReader::isSystemProcess(const std::string& name, int pid) {
    // PIDs under 100 are almost always kernel threads
    if (pid < 100) return true;
    // Kernel threads have names in brackets like [kworker]
    if (!name.empty() && name[0] == '[') return true;
    // Common system process names
    static const std::vector<std::string> sysNames = {
        "systemd", "init", "kthreadd", "ksoftirqd", "kworker",
        "rcu_", "migration", "watchdog", "cpuhp", "netns",
        "dbus", "udevd", "sshd", "cron", "rsyslog"
    };
    for (const auto& s : sysNames)
        if (name.find(s) != std::string::npos) return true;
    return false;
}

std::string ProcessReader::explainState(const std::string& state) {
    if (state == "R") return "Running — actively using CPU right now";
    if (state == "S") return "Sleeping — waiting for something (I/O, timer, input)";
    if (state == "D") return "Uninterruptible sleep — waiting for disk/hardware, cannot be killed";
    if (state == "Z") return "Zombie — finished but parent hasn't cleaned it up yet";
    if (state == "T") return "Stopped — paused by a signal or debugger";
    if (state == "I") return "Idle kernel thread — doing nothing";
    return "Unknown state";
}

std::string ProcessReader::explainProcess(const ProcessInfo& p) {
    std::string exp = "Process: " + p.name + " (PID " + std::to_string(p.pid) + ")\n";
    exp += "State: " + explainState(p.state) + "\n";
    exp += "Memory: " + std::to_string(p.memoryKB / 1024) + " MB in RAM\n";
    exp += "CPU: " + std::to_string((int)p.cpuPercent) + "% of processor\n";
    exp += "Threads: " + std::to_string(p.threads) + " parallel execution paths\n";
    exp += "Priority: " + std::to_string(p.priority) + " (lower = more urgent)\n";
    if (p.isSystem)
        exp += "Type: System process — part of the OS core, do not kill\n";
    else if (p.isSandbox)
        exp += "Type: Sandbox process — safe to experiment on\n";
    else
        exp += "Type: User process — started by you or your apps\n";
    return exp;
}

ProcessInfo ProcessReader::readOne(int pid) {
    ProcessInfo p;
    p.pid = pid;
    p.isSandbox = false;
    p.cpuPercent = calculateCpu(pid);

    // Read /proc/[pid]/status for name, state, memory, threads
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.substr(0, 5) == "Name:") {
            p.name = line.substr(6);
            if (!p.name.empty() && p.name.back() == '\n') p.name.pop_back();
        }
        else if (line.substr(0, 6) == "State:") {
            // Format: "State:\tR (running)"
            p.state = std::string(1, line[7]);
            p.stateLabel = explainState(p.state);
        }
        else if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream ss(line.substr(7));
            ss >> p.memoryKB;
        }
        else if (line.substr(0, 8) == "Threads:") {
            std::istringstream ss(line.substr(9));
            ss >> p.threads;
        }
    }

    // Read priority from /proc/[pid]/stat
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (stat) {
        std::string statLine;
        std::getline(stat, statLine);
        size_t pos = statLine.rfind(')');
        if (pos != std::string::npos) {
            std::istringstream ss(statLine.substr(pos + 2));
            std::string s; long long v;
            ss >> s; // state
            for (int i = 0; i < 15; i++) ss >> v; // skip to priority
            ss >> p.priority;
        }
    }

    p.user = getProcessUser(pid);
    p.isSystem = isSystemProcess(p.name, pid);
    return p;
}

std::vector<ProcessInfo> ProcessReader::readAll() {
    std::vector<ProcessInfo> result;
    DIR* proc = opendir("/proc");
    if (!proc) return result;

    // Update total CPU time once before reading all processes
    prevTotalCpu = getTotalCpuTime();

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        // /proc contains numbered folders for each PID
        std::string name = entry->d_name;
        bool isNum = !name.empty() && std::all_of(name.begin(), name.end(), ::isdigit);
        if (!isNum) continue;

        int pid = std::stoi(name);
        try {
            ProcessInfo p = readOne(pid);
            if (!p.name.empty()) result.push_back(p);
        } catch (...) {
            // Process may have died between readdir and readOne — skip it
        }
    }
    closedir(proc);
    return result;
}
