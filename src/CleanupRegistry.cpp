// ── CleanupRegistry.cpp ───────────────────────────────────────────────────────
// Defines the LearnOSCleanup globals and shadow-array mirror functions that are
// declared in CleanupRegistry.h and formerly lived in main.cpp.
//
// Splitting them out means test executables can link this single translation
// unit instead of pulling in main.cpp (which tries to build MainWindow etc.).

#include "CleanupRegistry.h"
#include <cstring>

namespace LearnOSCleanup {

// ── Public std::vector bookkeeping ────────────────────────────────────────────
std::vector<pid_t>       childPids;
std::vector<int>         shmIds;
std::vector<std::string> socketPaths;

// ── Async-signal-safe shadow arrays ──────────────────────────────────────────
// These are what crashHandler() in main.cpp actually reads.  Fixed-size C
// arrays + volatile sig_atomic_t length — no heap, no STL internals.

static constexpr int MAX_PIDS    = 256;
static constexpr int MAX_SHMIDS  = 64;
static constexpr int MAX_SOCKETS = 32;
static constexpr int MAX_PATH    = 256;

volatile pid_t        _pids[MAX_PIDS]              = {};
volatile sig_atomic_t _pidCount                     = 0;
volatile int          _shms[MAX_SHMIDS]             = {};
volatile sig_atomic_t _shmCount                     = 0;
volatile char         _socks[MAX_SOCKETS][MAX_PATH] = {};
volatile sig_atomic_t _sockCount                    = 0;

void _shadowAddPid(pid_t pid) {
    int n = _pidCount;
    if (n < MAX_PIDS) { _pids[n] = pid; _pidCount = n + 1; }
}
void _shadowRemovePid(pid_t pid) {
    int n = _pidCount;
    for (int i = 0; i < n; i++) {
        if (_pids[i] == pid) {
            // Swap-with-last, then decrement.
            // Ordering matters for signal safety:
            //   ① copy last→slot i   (count still n: both slots valid, one duplicated)
            //   ② decrement count    (duplicate is now past the end, invisible)
            // If a signal fires between ① and ②, the handler sees two entries
            // with the same PID and issues kill() twice — the second call returns
            // ESRCH, which is harmless.  The reverse order (decrement first) would
            // be unsafe: the handler could miss the entry entirely while count is n-1
            // but the last slot still holds the PID being removed.
            _pids[i] = _pids[n - 1]; // ①
            _pidCount = n - 1;        // ②
            return;
        }
    }
}
void _shadowAddShm(int id) {
    int n = _shmCount;
    if (n < MAX_SHMIDS) { _shms[n] = id; _shmCount = n + 1; }
}
void _shadowRemoveShm(int id) {
    int n = _shmCount;
    for (int i = 0; i < n; i++) {
        if (_shms[i] == id) {
            // Same swap-with-last, decrement-second ordering as _shadowRemovePid.
            // Duplicate shmctl(IPC_RMID) on the same id returns EINVAL — harmless.
            _shms[i] = _shms[n - 1]; // ①
            _shmCount = n - 1;        // ②
            return;
        }
    }
}
void _shadowAddSocket(const std::string& path) {
    int n = _sockCount;
    if (n < MAX_SOCKETS && path.size() < MAX_PATH) {
        const char* src = path.c_str();
        volatile char* dst = _socks[n];
        for (int i = 0; i <= (int)path.size(); i++) dst[i] = src[i];
        _sockCount = n + 1;
    }
}
void _shadowRemoveSocket(const std::string& path) {
    int n = _sockCount;
    for (int i = 0; i < n; i++) {
        if (std::string(const_cast<const char*>(
                reinterpret_cast<const volatile char*>(_socks[i]))) == path) {
            // Copy last slot into slot i, then decrement — same safe ordering.
            // Duplicate unlink() on the same path returns ENOENT — harmless.
            volatile char* dst = _socks[i];
            volatile char* src = _socks[n - 1];
            for (int j = 0; j < MAX_PATH; j++) dst[j] = src[j]; // ①
            _sockCount = n - 1;                                   // ②
            return;
        }
    }
}

} // namespace LearnOSCleanup
