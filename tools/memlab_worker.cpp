// memlab_worker — LearnOS Memory Lab backend.
//
// Each ALLOC calls mmap(MAP_ANONYMOUS|MAP_PRIVATE) for the requested size,
// so every allocation appears as a distinct entry in /proc/pid/maps.
// FREE calls munmap on the real pointer so the region vanishes from the map.
// The GUI reads back the real addresses and points MemMapWidget at this PID,
// so the student watches real kernel-visible regions appear and disappear.
//
// Protocol:
//   ALLOC <size> <strategy>   strategy is stored but not used for placement
//                              (with per-mmap semantics there is no arena to fit)
//   FREE <id>                 munmaps the real region; id comes from BLOCK lines
//   WRITE <id> <byte 0-255>   fills the region with a pattern (makes it resident)
//   RESET                     munmaps all live regions
//   STATUS                    prints all BLOCK lines + ARENA summary
//   GETPID                    prints WORKERPID <pid>\nOK
//   PING                      prints PONG\nOK
//
// Responses always end with OK or ERR <reason>.
// BLOCK line format: BLOCK <id> <addr_hex> <size> <used 0|1> <fillByte>
// ARENA line format: ARENA <totalBytes> <usedBytes> <freeBytes> <blockCount> <largestFreeRun>
//   (freeBytes and largestFreeRun are 0 — there is no free pool with per-mmap)

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>

namespace {

constexpr size_t MAX_ALLOC    = 4 * 1024 * 1024; // 4 MB cap per allocation
constexpr int    MAX_BLOCKS   = 512;

// Protection permission string
static const char* permStr(int prot) {
    if (prot == (PROT_READ|PROT_WRITE)) return "RW";
    if (prot == PROT_READ)              return "RO";
    if (prot == PROT_NONE)              return "NONE";
    return "?";
}

// Signal handler for SIGSEGV triggered by mprotect demo
static volatile int g_sigsegv_caught = 0;
static void sigsegv_handler(int) { g_sigsegv_caught = 1; }

struct Block {
    int      id;
    void*    ptr;
    size_t   size;
    bool     used;
    uint8_t  fillByte;
    int      prot;     // current protection (PROT_READ|PROT_WRITE etc.)
};

std::vector<Block> blocks;
int nextId = 1;

void printBlocks() {
    for (auto& b : blocks) {
        std::cout << "BLOCK " << b.id << " "
                  << reinterpret_cast<unsigned long>(b.ptr) << " "
                  << b.size << " "
                  << (b.used ? 1 : 0) << " "
                  << (int)b.fillByte << " "
                  << permStr(b.prot) << "\n";
    }
}

void printArenaSummary() {
    size_t used = 0;
    for (auto& b : blocks) if (b.used) used += b.size;
    size_t total = used; // no free pool — total == used in this model
    std::cout << "ARENA " << total << " " << used << " 0 "
              << blocks.size() << " 0\n";
}

bool cmdAlloc(size_t size, std::string& err) {
    if (size == 0) { err = "size must be > 0"; return false; }
    if (size > MAX_ALLOC) { err = "size exceeds 4 MB cap"; return false; }
    if ((int)blocks.size() >= MAX_BLOCKS) { err = "too many blocks — reset first"; return false; }

    size_t aligned = (size + 4095UL) & ~4095UL;

    void* ptr = mmap(nullptr, aligned, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) { err = "mmap failed"; return false; }

    Block b;
    b.id       = nextId++;
    b.ptr      = ptr;
    b.size     = aligned;
    b.used     = true;
    b.fillByte = 0;
    b.prot     = PROT_READ | PROT_WRITE;
    blocks.push_back(b);
    return true;
}

// mprotect demo: change protection on a block. Writing to a RO/NONE page
// causes SIGSEGV which we catch and report so the student sees the fault live.
bool cmdMprotect(int id, const std::string& permName, std::string& err) {
    for (auto& b : blocks) {
        if (!b.used || b.id != id) continue;
        int prot = PROT_READ | PROT_WRITE;
        if (permName == "RO")   prot = PROT_READ;
        if (permName == "NONE") prot = PROT_NONE;

        if (mprotect(b.ptr, b.size, prot) != 0) {
            err = "mprotect failed: "; err += strerror(errno); return false;
        }
        b.prot = prot;

        // If protection was set to RO or NONE, attempt a write to demonstrate SIGSEGV.
        if (prot != (PROT_READ | PROT_WRITE)) {
            // Install a signal handler so we survive the fault
            struct sigaction sa = {}, old_sa = {};
            sa.sa_handler = sigsegv_handler;
            sa.sa_flags = SA_RESETHAND; // one-shot
            sigaction(SIGSEGV, &sa, &old_sa);
            g_sigsegv_caught = 0;

            // Touch the first byte — will fault if RO or NONE
            volatile char* p = (volatile char*)b.ptr;
            *p = 0x42;  // this triggers SIGSEGV if not writable

            if (g_sigsegv_caught) {
                std::cout << "SIGSEGV_TRIGGERED\n";
            }
            // Restore default handler
            sigaction(SIGSEGV, &old_sa, nullptr);
        }
        return true;
    }
    err = "no such block id"; return false;
}

// madvise demo: advise the kernel about future usage pattern
bool cmdMadvise(int id, const std::string& advice, std::string& err) {
    for (auto& b : blocks) {
        if (!b.used || b.id != id) continue;
        int adv = MADV_DONTNEED;
        if (advice == "WILLNEED") adv = MADV_WILLNEED;
        if (advice == "DONTNEED") adv = MADV_DONTNEED;
        if (madvise(b.ptr, b.size, adv) != 0) {
            err = "madvise failed: "; err += strerror(errno); return false;
        }
        return true;
    }
    err = "no such block id"; return false;
}

// COW fork demo: fork a child, both parent and child share physical pages initially.
// Then the child writes to diverge. We report shared_pages before and after.
void cmdCowFork() {
    if (blocks.empty()) {
        std::cout << "ERR no blocks to fork — allocate first\n";
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        std::cout << "ERR fork failed\n";
        return;
    }
    if (pid == 0) {
        // Child: write to each block to trigger COW
        for (auto& b : blocks) {
            if (!b.used || b.prot != (PROT_READ|PROT_WRITE)) continue;
            memset(b.ptr, 0xBB, b.size);  // diverges from parent
        }
        // Report child's smaps RSS to parent via stdout (will be interleaved,
        // but the parent reads it back via the protocol line)
        std::ifstream smaps("/proc/self/smaps_rollup");
        std::string line;
        long rss = 0;
        while (std::getline(smaps, line)) {
            if (line.rfind("Rss:", 0) == 0) {
                std::istringstream ss(line.substr(4)); ss >> rss; break;
            }
        }
        fprintf(stdout, "COWFORK child_rss=%ld\n", rss);
        fflush(stdout);
        _exit(0);
    }
    // Parent: report pids
    std::ifstream smaps("/proc/self/smaps_rollup");
    std::string line;
    long parent_rss = 0;
    while (std::getline(smaps, line)) {
        if (line.rfind("Rss:", 0) == 0) {
            std::istringstream ss(line.substr(4)); ss >> parent_rss; break;
        }
    }
    fprintf(stdout, "COWFORK parent_pid=%d child_pid=%d parent_rss=%ld\n",
            (int)getpid(), (int)pid, parent_rss);
    fflush(stdout);
    // Wait for child
    int status; waitpid(pid, &status, 0);
}

bool cmdFree(int id, std::string& err) {
    for (size_t i = 0; i < blocks.size(); i++) {
        if (blocks[i].used && blocks[i].id == id) {
            munmap(blocks[i].ptr, blocks[i].size);
            blocks.erase(blocks.begin() + i);
            return true;
        }
    }
    err = "no such block id";
    return false;
}

bool cmdWrite(int id, int byteVal, std::string& err) {
    if (byteVal < 0 || byteVal > 255) { err = "byte must be 0-255"; return false; }
    for (auto& b : blocks) {
        if (b.used && b.id == id) {
            b.fillByte = (uint8_t)byteVal;
            memset(b.ptr, byteVal, b.size);
            return true;
        }
    }
    err = "no such block id";
    return false;
}

void cmdReset() {
    for (auto& b : blocks)
        if (b.used) munmap(b.ptr, b.size);
    blocks.clear();
    nextId = 1;
}

} // namespace

int main() {
    std::cout << "READY " << (MAX_ALLOC * MAX_BLOCKS) << "\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::string err;

        if (cmd == "ALLOC") {
            size_t size; std::string strategy;
            ss >> size >> strategy;
            if (cmdAlloc(size, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "FREE") {
            int id; ss >> id;
            if (cmdFree(id, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "WRITE") {
            int id, byteVal; ss >> id >> byteVal;
            if (cmdWrite(id, byteVal, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "MPROTECT") {
            int id; std::string perm;
            ss >> id >> perm;
            if (cmdMprotect(id, perm, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "MADVISE") {
            int id; std::string advice;
            ss >> id >> advice;
            if (cmdMadvise(id, advice, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "COW_FORK") {
            cmdCowFork();
            printBlocks(); printArenaSummary();
            std::cout << "OK\n";
        } else if (cmd == "RESET") {
            cmdReset(); printBlocks(); printArenaSummary();
            std::cout << "OK\n";
        } else if (cmd == "STATUS") {
            printBlocks(); printArenaSummary();
            std::cout << "OK\n";
        } else if (cmd == "GETPID") {
            std::cout << "WORKERPID " << (int)getpid() << "\nOK\n";
        } else if (cmd == "PING") {
            std::cout << "PONG\nOK\n";
        } else {
            std::cout << "ERR unknown command\n";
        }
        std::cout << std::flush;
    }

    cmdReset();
    return 0;
}
