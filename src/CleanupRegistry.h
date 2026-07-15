#pragma once
// ── LearnOSCleanup — cross-module crash cleanup registry ─────────────────────
// Each widget that forks/clones children or creates SysV IPC segments registers
// those resources here.  The crash signal handler in main.cpp reads fixed-size
// C arrays (not the std::vector — which is not async-signal-safe) and kills /
// removes everything even if destructors never run.
//
// How it works (two-layer design):
//   1. The std::vector members (childPids, shmIds, socketPaths) are the
//      "Qt-side" bookkeeping — used for normal-path cleanup in destructors.
//   2. The shadow C arrays in main.cpp (_pids[], _shms[], _socks[]) are the
//      "signal-handler-safe" bookkeeping — only read by crashHandler(), never
//      written inside the handler, and indexed by volatile sig_atomic_t counts.
//
// The register/unregister inline functions below update BOTH layers atomically
// from the Qt main thread.
//
// Thread-safety: all callers run on the main Qt thread; the signal handler only
// reads the shadow arrays (never writes) so no mutex is required.
//
// Known limitation — PID reuse:
//   Linux recycles PIDs after a process exits.  If a tracked child dies, gets
//   unregisterPid()'d, and then an unrelated system process is assigned that
//   same PID before a crash fires the handler, the handler could kill() the
//   wrong process.  This is an inherent hazard of any PID-tracking design.
//   Mitigation: call unregisterPid() as close as possible to the waitpid() /
//   exit confirmation that confirms the child is gone — which all callers here
//   already do.  There is no race-free solution short of pidfd (Linux 5.3+).

#include <signal.h>
#include <unistd.h>
#include <sys/shm.h>
#include <string>
#include <vector>
#include <algorithm>

namespace LearnOSCleanup {

// Defined in CleanupRegistry.cpp.
extern std::vector<pid_t>       childPids;
extern std::vector<int>         shmIds;
extern std::vector<std::string> socketPaths;

// Shadow-array mirrors — defined in CleanupRegistry.cpp.
// The crash handler in main.cpp reads these directly (no function call overhead
// inside the signal handler).
extern volatile pid_t        _pids[];
extern volatile sig_atomic_t _pidCount;
extern volatile int          _shms[];
extern volatile sig_atomic_t _shmCount;
extern volatile char         _socks[][256];
extern volatile sig_atomic_t _sockCount;

// Declared here so the register/unregister inlines below can call them.
void _shadowAddPid(pid_t pid);
void _shadowRemovePid(pid_t pid);
void _shadowAddShm(int id);
void _shadowRemoveShm(int id);
void _shadowAddSocket(const std::string& path);
void _shadowRemoveSocket(const std::string& path);

inline void registerPid(pid_t pid) {
    if (pid > 0) { childPids.push_back(pid); _shadowAddPid(pid); }
}
inline void unregisterPid(pid_t pid) {
    childPids.erase(std::remove(childPids.begin(), childPids.end(), pid), childPids.end());
    _shadowRemovePid(pid);
}

inline void registerShmId(int id) {
    if (id >= 0) { shmIds.push_back(id); _shadowAddShm(id); }
}
inline void unregisterShmId(int id) {
    shmIds.erase(std::remove(shmIds.begin(), shmIds.end(), id), shmIds.end());
    _shadowRemoveShm(id);
}

inline void registerSocketPath(const std::string& path) {
    if (!path.empty()) { socketPaths.push_back(path); _shadowAddSocket(path); }
}
inline void unregisterSocketPath(const std::string& path) {
    socketPaths.erase(std::remove(socketPaths.begin(), socketPaths.end(), path), socketPaths.end());
    _shadowRemoveSocket(path);
}

} // namespace LearnOSCleanup
