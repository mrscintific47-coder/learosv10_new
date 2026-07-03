// memlab_worker — LearnOS Memory Lab backend.
//
// This is a deliberately small, standalone process. It mmaps ONE fixed-size
// real memory arena and performs real pointer arithmetic to satisfy
// allocation requests inside it — this is genuine memory management, not a
// drawn simulation. It is a separate process from the GUI on purpose:
//
//   - The arena is capped (default 4MB) so nothing here can exhaust real
//     system memory or take down the student's machine.
//   - If a command corrupts internal state or the process crashes, only
//     this worker dies — LearnOS notices the broken pipe and respawns it.
//   - "RESET" wipes all allocations without restarting the process.
//   - "PING"/"STATUS" let the GUI verify the worker is still alive.
//
// Protocol: one line in on stdin -> one or more lines out on stdout.
// Commands:
//   ALLOC <size> <strategy>      strategy = first|best|worst
//   FREE <id>
//   WRITE <id> <byte 0-255>      fills the block with this byte (visual "data")
//   RESET
//   STATUS
// Responses (always end with a line "OK" or "ERR <reason>"):
//   BLOCK <id> <offset> <size> <used 0|1> <fillByte>
//   ARENA <totalBytes> <usedBytes> <freeBytes> <blockCount> <largestFreeRun>
//   OK
//   ERR <reason>

#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace {

constexpr size_t ARENA_SIZE = 4 * 1024 * 1024; // 4MB hard cap — the guardrail
constexpr int MAX_BLOCKS = 4096;                // sanity cap on fragmentation

struct Block {
    int      id;
    size_t   offset;
    size_t   size;
    bool     used;
    uint8_t  fillByte;
};

uint8_t* arena = nullptr;
std::vector<Block> blocks;   // kept sorted by offset, contiguous, covers [0, ARENA_SIZE)
int nextId = 1;

void resetArena() {
    blocks.clear();
    Block whole;
    whole.id = 0; // 0 is reserved for "free space", never returned to student
    whole.offset = 0;
    whole.size = ARENA_SIZE;
    whole.used = false;
    whole.fillByte = 0;
    blocks.push_back(whole);
    nextId = 1;
    if (arena) memset(arena, 0, ARENA_SIZE);
}

void printArenaSummary() {
    size_t used = 0, free = 0, largestFree = 0;
    for (auto& b : blocks) {
        if (b.used) used += b.size;
        else { free += b.size; largestFree = std::max(largestFree, b.size); }
    }
    std::cout << "ARENA " << ARENA_SIZE << " " << used << " " << free << " "
              << blocks.size() << " " << largestFree << "\n";
}

void printBlocks() {
    for (auto& b : blocks) {
        std::cout << "BLOCK " << b.id << " " << b.offset << " " << b.size << " "
                  << (b.used ? 1 : 0) << " " << (int)b.fillByte << "\n";
    }
}

void mergeFreeNeighbors() {
    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b){ return a.offset < b.offset; });
    for (size_t i = 0; i + 1 < blocks.size(); ) {
        if (!blocks[i].used && !blocks[i+1].used) {
            blocks[i].size += blocks[i+1].size;
            blocks.erase(blocks.begin() + i + 1);
        } else {
            i++;
        }
    }
}

// Returns index of chosen free block, or -1 if none fits.
int findFit(size_t size, const std::string& strategy) {
    int chosen = -1;
    for (size_t i = 0; i < blocks.size(); i++) {
        if (blocks[i].used || blocks[i].size < size) continue;
        if (strategy == "first") return (int)i;
        if (chosen == -1) { chosen = (int)i; continue; }
        if (strategy == "best"  && blocks[i].size < blocks[chosen].size) chosen = (int)i;
        if (strategy == "worst" && blocks[i].size > blocks[chosen].size) chosen = (int)i;
    }
    return chosen;
}

bool cmdAlloc(size_t size, const std::string& strategy, std::string& err) {
    if (size == 0) { err = "size must be > 0"; return false; }
    if (size > ARENA_SIZE) { err = "size exceeds arena capacity"; return false; }
    if ((int)blocks.size() >= MAX_BLOCKS) { err = "too many blocks — reset arena"; return false; }

    int idx = findFit(size, strategy);
    if (idx == -1) { err = "no free block large enough (fragmented?)"; return false; }

    Block& freeBlk = blocks[idx];
    size_t leftover = freeBlk.size - size;

    Block used;
    used.id = nextId++;
    used.offset = freeBlk.offset;
    used.size = size;
    used.used = true;
    used.fillByte = 0;

    if (leftover == 0) {
        blocks[idx] = used;
    } else {
        Block remainder;
        remainder.id = 0;
        remainder.offset = freeBlk.offset + size;
        remainder.size = leftover;
        remainder.used = false;
        remainder.fillByte = 0;
        blocks[idx] = used;
        blocks.insert(blocks.begin() + idx + 1, remainder);
    }
    return true;
}

bool cmdFree(int id, std::string& err) {
    for (auto& b : blocks) {
        if (b.used && b.id == id) {
            b.used = false;
            b.id = 0;
            b.fillByte = 0;
            if (arena) memset(arena + b.offset, 0, b.size);
            mergeFreeNeighbors();
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
            if (arena) memset(arena + b.offset, byteVal, b.size);
            return true;
        }
    }
    err = "no such block id";
    return false;
}

} // namespace

int main() {
    // Real mmap'd memory — genuine addresses, genuine writes — but capped
    // and isolated to this disposable process.
    arena = (uint8_t*)mmap(nullptr, ARENA_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) {
        std::cout << "ERR failed to mmap arena\n";
        return 1;
    }
    resetArena();

    // Tell the GUI we're alive and ready, with our cap, before any command.
    std::cout << "READY " << ARENA_SIZE << "\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::string err;

        if (cmd == "ALLOC") {
            size_t size; std::string strategy;
            ss >> size >> strategy;
            if (cmdAlloc(size, strategy, err)) {
                printBlocks();
                printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "FREE") {
            int id; ss >> id;
            if (cmdFree(id, err)) {
                printBlocks();
                printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "WRITE") {
            int id, byteVal; ss >> id >> byteVal;
            if (cmdWrite(id, byteVal, err)) {
                printBlocks();
                printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }
        } else if (cmd == "RESET") {
            resetArena();
            printBlocks();
            printArenaSummary();
            std::cout << "OK\n";
        } else if (cmd == "STATUS") {
            printBlocks();
            printArenaSummary();
            std::cout << "OK\n";
        } else if (cmd == "PING") {
            std::cout << "PONG\n" << "OK\n";
        } else {
            std::cout << "ERR unknown command\n";
        }
        std::cout << std::flush;
    }

    munmap(arena, ARENA_SIZE);
    return 0;
}
