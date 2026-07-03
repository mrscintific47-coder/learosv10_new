#pragma once
#include <string>
#include <vector>

struct ProcessInfo {
    int pid;
    std::string name;
    std::string state;       // R=running, S=sleeping, Z=zombie, etc.
    std::string stateLabel;  // human readable
    long memoryKB;
    float cpuPercent;
    int priority;
    int threads;
    std::string user;
    bool isSystem;           // true = kernel/system process
    bool isSandbox;          // true = we spawned it for experiments
};

class ProcessReader {
public:
    static std::vector<ProcessInfo> readAll();
    static ProcessInfo readOne(int pid);
    static std::string explainState(const std::string& state);
    static std::string explainProcess(const ProcessInfo& p);

private:
    static float calculateCpu(int pid);
    static std::string getProcessUser(int pid);
    static bool isSystemProcess(const std::string& name, int pid);
};
