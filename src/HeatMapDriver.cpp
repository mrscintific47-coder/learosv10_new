#include "HeatMapDriver.h"
#include <dirent.h>

HeatMapDriver::HeatMapDriver(HeatMap* map, QObject* parent)
    : QObject(parent), heatMap(map)
{
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &HeatMapDriver::refresh);
    timer->start(800); // refresh 800ms — snappy but not wasteful
    refresh();
}

void HeatMapDriver::refresh() {
    auto cores = readCores();
    auto procSlots = readSlots();
    float pct; long usedMB, totalMB;
    readMem(pct, usedMB, totalMB);

    heatMap->updateCores(cores);
    heatMap->updateMemory(pct, usedMB, totalMB);
    heatMap->updateProcessSlots(procSlots);
}

// Reads /proc/stat — one line per core: cpu0 user nice system idle ...
std::vector<CoreStat> HeatMapDriver::readCores() {
    std::vector<CoreStat> result;
    std::ifstream stat("/proc/stat");
    std::string line;

    while (std::getline(stat, line)) {
        if (line.rfind("cpu", 0) != 0) continue;
        if (line[3] == ' ') continue; // skip the aggregate "cpu" line

        int coreId = std::stoi(line.substr(3));
        std::istringstream ss(line);
        std::string label;
        long long user, nice, system, idle, iowait, irq, softirq, steal;
        ss >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

        long long idleTime  = idle + iowait;
        long long totalTime = user+nice+system+idle+iowait+irq+softirq+steal;

        float usage = 0;
        auto it = prevTimes.find(coreId);
        if (it != prevTimes.end()) {
            long long dIdle  = idleTime  - it->second.idle;
            long long dTotal = totalTime - it->second.total;
            if (dTotal > 0)
                usage = 100.0f * (1.0f - (float)dIdle / dTotal);
        }
        prevTimes[coreId] = {idleTime, totalTime};

        CoreStat cs; cs.coreId = coreId; cs.usage = usage;
        result.push_back(cs);
    }
    return result;
}

// Reads /proc/meminfo
void HeatMapDriver::readMem(float& pct, long& usedMB, long& totalMB) {
    std::ifstream f("/proc/meminfo");
    std::string line;
    long totalKB = 0, availKB = 0;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key; long val;
        ss >> key >> val;
        if (key == "MemTotal:")     totalKB = val;
        if (key == "MemAvailable:") availKB = val;
    }
    long usedKB = totalKB - availKB;
    totalMB = totalKB / 1024;
    usedMB  = usedKB  / 1024;
    pct     = totalKB > 0 ? (usedKB * 100.0f / totalKB) : 0;
}

// Reads all /proc/[pid]/stat to get process states
std::vector<ProcessSlot> HeatMapDriver::readSlots() {
    std::vector<ProcessSlot> result;
    DIR* proc = opendir("/proc");
    if (!proc) return result;

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        std::string name(entry->d_name);
        if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit)) continue;

        int pid = std::stoi(name);
        std::ifstream statFile("/proc/" + name + "/stat");
        if (!statFile.is_open()) continue;

        std::string content;
        std::getline(statFile, content);

        // Find state character — it's after the closing ) of the comm field
        auto rp = content.rfind(')');
        if (rp == std::string::npos || rp + 2 >= content.size()) continue;
        char state = content[rp + 2];

        // Get cpu times from fields 14+15
        std::istringstream ss(content);
        std::string tok;
        std::vector<std::string> fields;
        while (ss >> tok) fields.push_back(tok);

        float cpu = 0;
        if (fields.size() >= 15) {
            long long utime = std::stoll(fields[13]);
            long long stime = std::stoll(fields[14]);
            cpu = std::min((float)(utime + stime) / 100.0f, 100.0f);
        }

        ProcessSlot ps; ps.pid = pid; ps.state = state; ps.cpu = cpu;
        result.push_back(ps);
    }
    closedir(proc);
    return result;
}
