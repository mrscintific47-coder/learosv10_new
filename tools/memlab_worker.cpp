// memlab_worker — LearnOS Memory Lab backend (v2)
//
// Manages a single 4 MB sandbox region using real mmap(MAP_ANONYMOUS).
// Supports four allocation technique modes that control HOW addresses are
// assigned within that 4 MB, making page tables / segment tables / frame
// tables visible to the student.
//
// ── Allocation technique modes ──────────────────────────────────────────────
//
//  contiguous  (default)
//    Blocks are placed sequentially from offset 0, pack-forward.
//    Freed blocks leave holes; no compaction.
//
//  paged
//    The sandbox is divided into fixed 4096-byte pages (1024 pages total).
//    Each allocation claims N whole pages. The "page table" maps virtual
//    page numbers → frame offsets within the sandbox.
//    PAGETABLE command dumps the full VPN→frame mapping.
//
//  segmented
//    Up to 8 named segments (code, data, heap, stack, bss, tls, extra1, extra2).
//    Each segment has a base + limit; allocations target a named segment.
//    SEGTABLE command dumps the segment descriptor table.
//
//  framed
//    Identical physical-frame semantics to paged but explicitly shows a
//    frame table (which physical frames are free/occupied and by whom).
//    FRAMETABLE command dumps the frame table.
//
// ── Data structure commands ─────────────────────────────────────────────────
//
//  ALLOC <size> <label>     raw allocation, label is free text
//  STRUCT LL <n>            allocate n linked-list nodes (each 32 bytes)
//  STRUCT ARRAY <n> <esize> allocate array of n elements of esize bytes
//  STRUCT TREE <n>          allocate n binary-tree nodes (each 48 bytes)
//  STRUCT HASH <buckets>    allocate hash table (bucket array + metadata)
//
//  Each STRUCT command allocates the actual bytes in the sandbox and
//  returns a STRUCTDEF line describing the layout so the GUI can draw it.
//
// ── Other commands ───────────────────────────────────────────────────────────
//  MODE contiguous|paged|segmented|framed   switch technique; resets sandbox
//  FREE <id>                free a block by id
//  RESET                    free all blocks
//  STATUS                   print all BLOCK lines + ARENA summary
//  GETPID                   print WORKERPID <pid>
//  PAGETABLE                dump page table (paged/framed modes)
//  SEGTABLE                 dump segment table (segmented mode)
//  FRAMETABLE               dump frame table (framed mode)
//
// ── Response protocol ────────────────────────────────────────────────────────
//  BLOCK <id> <offset> <size> <used 0|1> <label>
//  STRUCTDEF <id> <type> <offset> <totalSize> <elementSize> <count> <label>
//    (followed by NODE lines for LL/TREE, or just STRUCTDEF for ARRAY/HASH)
//  NODE <structId> <nodeIdx> <offset>
//  ARENA <totalBytes> <usedBytes> <freeBytes> <blockCount> <mode>
//  PAGETABLE_ENTRY <vpn> <frame> <present 0|1> <blockId>
//  SEGTABLE_ENTRY <segName> <base> <limit> <blockId>
//  FRAMETABLE_ENTRY <frame> <free 0|1> <blockId>
//  RESET_LOG
//  OK / ERR <reason>

#include <sys/mman.h>
#include <unistd.h>
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
#include <map>

namespace {

// ─────────────────── constants ───────────────────────────────────────────────

constexpr size_t SANDBOX_SIZE = 4 * 1024 * 1024;  // 4 MB
constexpr size_t PAGE_SIZE    = 4096;
constexpr int    NUM_PAGES    = (int)(SANDBOX_SIZE / PAGE_SIZE); // 1024
constexpr int    MAX_BLOCKS   = 256;
constexpr int    MIN_ALIGN    = 16;

// ─────────────────── global sandbox ──────────────────────────────────────────

static uint8_t* g_sandbox = nullptr;  // base of the 4 MB mmap

// ─────────────────── block record ────────────────────────────────────────────

struct Block {
    int    id;
    size_t offset;   // byte offset from g_sandbox
    size_t size;
    bool   used;
    std::string label;
    // For structured allocations:
    bool   isStruct   = false;
    std::string structType; // "LL", "ARRAY", "TREE", "HASH", ""
    int    elemCount  = 0;
    size_t elemSize   = 0;
};

static std::vector<Block> g_blocks;
static int g_nextId = 1;

// ─────────────────── mode ────────────────────────────────────────────────────

enum class Mode { Contiguous, Paged, Segmented, Framed };
static Mode g_mode = Mode::Contiguous;

// ─────────────────── free-list (contiguous + segmented) ──────────────────────

struct FreeSpan { size_t start; size_t size; };
static std::vector<FreeSpan> g_free;

static void initFreeList() {
    g_free.clear();
    g_free.push_back({0, SANDBOX_SIZE});
}

static void coalesce() {
    std::sort(g_free.begin(), g_free.end(),
              [](const FreeSpan& a, const FreeSpan& b){ return a.start < b.start; });
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i + 1 < g_free.size(); i++) {
            if (g_free[i].start + g_free[i].size == g_free[i+1].start) {
                g_free[i].size += g_free[i+1].size;
                g_free.erase(g_free.begin() + i + 1);
                merged = true; break;
            }
        }
    }
}

static size_t alignUp(size_t n, size_t align = MIN_ALIGN) {
    return (n + align - 1) & ~(align - 1);
}

// First-fit from free list
static size_t firstFit(size_t size, std::string& err) {
    size_t aligned = alignUp(size);
    for (size_t i = 0; i < g_free.size(); i++) {
        if (g_free[i].size >= aligned) {
            size_t off = g_free[i].start;
            if (g_free[i].size == aligned) g_free.erase(g_free.begin() + i);
            else { g_free[i].start += aligned; g_free[i].size -= aligned; }
            return off;
        }
    }
    err = "out of sandbox space";
    return SIZE_MAX;
}

static void returnToFreeList(size_t offset, size_t size) {
    g_free.push_back({offset, alignUp(size)});
    coalesce();
}

// ─────────────────── paged / framed tables ───────────────────────────────────

struct PageEntry {
    bool  present  = false;
    int   blockId  = -1;  // which block owns this page
};
static PageEntry g_pageTable[NUM_PAGES];

static void clearPageTable() {
    for (auto& e : g_pageTable) { e.present = false; e.blockId = -1; }
}

// Allocate n consecutive pages; return starting page number or -1
static int allocPages(int n, int blockId, std::string& err) {
    // Find first run of n free pages
    for (int start = 0; start <= NUM_PAGES - n; start++) {
        bool ok = true;
        for (int i = 0; i < n; i++) if (g_pageTable[start + i].present) { ok = false; break; }
        if (ok) {
            for (int i = 0; i < n; i++) {
                g_pageTable[start + i].present = true;
                g_pageTable[start + i].blockId = blockId;
            }
            return start;
        }
    }
    err = "no contiguous page run of size " + std::to_string(n);
    return -1;
}

static void freePages(int blockId) {
    for (auto& e : g_pageTable)
        if (e.blockId == blockId) { e.present = false; e.blockId = -1; }
}

// ─────────────────── segment table ───────────────────────────────────────────

static const char* SEG_NAMES[] = {
    "code", "data", "heap", "stack", "bss", "tls", "extra1", "extra2"
};
static const int NUM_SEGS = 8;

struct SegEntry {
    size_t base  = 0;
    size_t limit = 0;
    int    blockId = -1;
    bool   active  = false;
};
static SegEntry g_segTable[NUM_SEGS];
static int g_nextSeg = 0; // round-robin segment assignment

static void clearSegTable() {
    for (auto& s : g_segTable) { s.base = 0; s.limit = 0; s.blockId = -1; s.active = false; }
    g_nextSeg = 0;
    // Pre-lay the segments across the 4 MB range with equal partitions
    size_t partSize = SANDBOX_SIZE / NUM_SEGS;
    for (int i = 0; i < NUM_SEGS; i++) {
        g_segTable[i].base  = i * partSize;
        g_segTable[i].limit = partSize;
    }
}

// Allocate within the next available segment slot
static size_t allocSeg(size_t size, int blockId, std::string& err) {
    // Find a segment whose limit can hold 'size' and isn't occupied
    for (int i = 0; i < NUM_SEGS; i++) {
        int idx = (g_nextSeg + i) % NUM_SEGS;
        SegEntry& s = g_segTable[idx];
        if (!s.active && s.limit >= alignUp(size)) {
            s.active  = true;
            s.blockId = blockId;
            // Shrink the segment limit to the actual size used
            size_t off = s.base;
            s.limit = alignUp(size);
            g_nextSeg = (idx + 1) % NUM_SEGS;
            return off;
        }
    }
    err = "no free segment slot";
    return SIZE_MAX;
}

static void freeSeg(int blockId) {
    // Reset the partition to full limit (simplified — no sub-segment fragmentation)
    size_t partSize = SANDBOX_SIZE / NUM_SEGS;
    for (auto& s : g_segTable) {
        if (s.blockId == blockId) {
            s.active  = false;
            s.blockId = -1;
            s.limit   = partSize;
        }
    }
}

// ─────────────────── allocation dispatch ─────────────────────────────────────

static size_t allocInSandbox(size_t size, int blockId, std::string& err) {
    if (size == 0) { err = "size must be > 0"; return SIZE_MAX; }
    if (size > SANDBOX_SIZE) { err = "exceeds 4 MB sandbox"; return SIZE_MAX; }
    if ((int)g_blocks.size() >= MAX_BLOCKS) { err = "too many blocks — reset first"; return SIZE_MAX; }

    switch (g_mode) {
    case Mode::Contiguous:
        return firstFit(size, err);

    case Mode::Paged:
    case Mode::Framed: {
        int pages = (int)((alignUp(size, PAGE_SIZE)) / PAGE_SIZE);
        int startPage = allocPages(pages, blockId, err);
        if (startPage < 0) return SIZE_MAX;
        return (size_t)startPage * PAGE_SIZE;
    }

    case Mode::Segmented:
        return allocSeg(size, blockId, err);
    }
    err = "unknown mode"; return SIZE_MAX;
}

static void freeFromSandbox(const Block& b) {
    switch (g_mode) {
    case Mode::Contiguous:
        returnToFreeList(b.offset, b.size);
        break;
    case Mode::Paged:
    case Mode::Framed:
        freePages(b.id);
        break;
    case Mode::Segmented:
        freeSeg(b.id);
        break;
    }
}

// ─────────────────── output helpers ──────────────────────────────────────────

static const char* modeName() {
    switch (g_mode) {
    case Mode::Contiguous: return "contiguous";
    case Mode::Paged:      return "paged";
    case Mode::Segmented:  return "segmented";
    case Mode::Framed:     return "framed";
    }
    return "contiguous";
}

static void printBlocks() {
    for (auto& b : g_blocks) {
        std::cout << "BLOCK " << b.id << " "
                  << b.offset << " "
                  << b.size << " "
                  << (b.used ? 1 : 0) << " "
                  << b.label << "\n";
    }
}

static void printArenaSummary() {
    size_t used = 0;
    for (auto& b : g_blocks) if (b.used) used += b.size;
    size_t free_ = SANDBOX_SIZE - used;
    std::cout << "ARENA " << SANDBOX_SIZE << " " << used << " "
              << free_ << " " << g_blocks.size() << " " << modeName() << "\n";
}

static void printPageTable() {
    for (int i = 0; i < NUM_PAGES; i++) {
        std::cout << "PAGETABLE_ENTRY " << i << " "
                  << i << " "   // frame == vpn in this flat model
                  << (g_pageTable[i].present ? 1 : 0) << " "
                  << g_pageTable[i].blockId << "\n";
    }
}

static void printSegTable() {
    for (int i = 0; i < NUM_SEGS; i++) {
        std::cout << "SEGTABLE_ENTRY "
                  << SEG_NAMES[i] << " "
                  << g_segTable[i].base << " "
                  << g_segTable[i].limit << " "
                  << g_segTable[i].blockId << "\n";
    }
}

static void printFrameTable() {
    // Each frame = one page; report free/occupied + owning block
    for (int i = 0; i < NUM_PAGES; i++) {
        std::cout << "FRAMETABLE_ENTRY " << i << " "
                  << (g_pageTable[i].present ? 0 : 1) << " "   // 1 = free
                  << g_pageTable[i].blockId << "\n";
    }
}

// ─────────────────── block commands ──────────────────────────────────────────

static bool cmdAlloc(size_t size, const std::string& label, std::string& err) {
    int id = g_nextId++;
    size_t off = allocInSandbox(size, id, err);
    if (off == SIZE_MAX) { g_nextId--; return false; }

    Block b;
    b.id    = id;
    b.offset= off;
    b.size  = alignUp(size);
    b.used  = true;
    b.label = label.empty() ? ("block" + std::to_string(id)) : label;
    // Zero the bytes so the student gets a clean slate
    memset(g_sandbox + off, 0, b.size);
    g_blocks.push_back(b);
    return true;
}

static bool cmdFree(int id, std::string& err) {
    for (size_t i = 0; i < g_blocks.size(); i++) {
        if (g_blocks[i].used && g_blocks[i].id == id) {
            freeFromSandbox(g_blocks[i]);
            g_blocks.erase(g_blocks.begin() + i);
            return true;
        }
    }
    err = "no such block id"; return false;
}

static void cmdReset() {
    g_blocks.clear();
    g_nextId = 1;
    initFreeList();
    clearPageTable();
    clearSegTable();
}

// ─────────────────── struct commands ─────────────────────────────────────────

// Linked list: each node is { next_ptr(8), data(24) } = 32 bytes
static bool cmdStructLL(int n, const std::string& label, std::string& err) {
    if (n <= 0 || n > 512) { err = "n must be 1–512"; return false; }
    constexpr size_t NODE_SIZE = 32;
    int id = g_nextId++;
    size_t totalSize = (size_t)n * NODE_SIZE;
    size_t off = allocInSandbox(totalSize, id, err);
    if (off == SIZE_MAX) { g_nextId--; return false; }

    memset(g_sandbox + off, 0, alignUp(totalSize));

    // Write next pointers: each node[i].next = base + (i+1)*NODE_SIZE, last = NULL
    for (int i = 0; i < n; i++) {
        uint8_t* node = g_sandbox + off + i * NODE_SIZE;
        // Offset of the *next* pointer within the sandbox (not a real pointer)
        uint64_t nextOff = (i < n - 1) ? (uint64_t)(off + (i + 1) * NODE_SIZE) : 0;
        memcpy(node, &nextOff, 8);
        // data bytes: fill with index value for visibility
        memset(node + 8, (uint8_t)(i + 1), 24);
    }

    Block b;
    b.id        = id;
    b.offset    = off;
    b.size      = alignUp(totalSize);
    b.used      = true;
    b.label     = label.empty() ? "LinkedList" : label;
    b.isStruct  = true;
    b.structType= "LL";
    b.elemCount = n;
    b.elemSize  = NODE_SIZE;
    g_blocks.push_back(b);

    // Print STRUCTDEF + NODE lines
    std::cout << "STRUCTDEF " << id << " LL "
              << off << " " << b.size << " "
              << NODE_SIZE << " " << n << " " << b.label << "\n";
    for (int i = 0; i < n; i++) {
        // next_ptr stored in first 8 bytes of node
        uint64_t nextOff = 0;
        memcpy(&nextOff, g_sandbox + off + i * NODE_SIZE, 8);
        std::cout << "NODE " << id << " " << i << " "
                  << (off + (size_t)i * NODE_SIZE) << " "
                  << nextOff << "\n";
    }
    return true;
}

// Dynamic array: contiguous elements
static bool cmdStructArray(int n, size_t esize, const std::string& label, std::string& err) {
    if (n <= 0 || n > 65536) { err = "n must be 1–65536"; return false; }
    if (esize == 0 || esize > 256) { err = "element size must be 1–256"; return false; }
    int id = g_nextId++;
    size_t totalSize = (size_t)n * esize;
    size_t off = allocInSandbox(totalSize, id, err);
    if (off == SIZE_MAX) { g_nextId--; return false; }

    memset(g_sandbox + off, 0, alignUp(totalSize));
    // Fill each element with its index (mod 256)
    for (int i = 0; i < n; i++)
        memset(g_sandbox + off + (size_t)i * esize, (uint8_t)(i % 256), esize);

    Block b;
    b.id        = id;
    b.offset    = off;
    b.size      = alignUp(totalSize);
    b.used      = true;
    b.label     = label.empty() ? "Array" : label;
    b.isStruct  = true;
    b.structType= "ARRAY";
    b.elemCount = n;
    b.elemSize  = esize;
    g_blocks.push_back(b);

    std::cout << "STRUCTDEF " << id << " ARRAY "
              << off << " " << b.size << " "
              << esize << " " << n << " " << b.label << "\n";
    return true;
}

// Binary tree: each node is { left(8), right(8), key(4), pad(28) } = 48 bytes
// Laid out as a complete binary tree in BFS order
static bool cmdStructTree(int n, const std::string& label, std::string& err) {
    if (n <= 0 || n > 256) { err = "n must be 1–256"; return false; }
    constexpr size_t NODE_SIZE = 48;
    int id = g_nextId++;
    size_t totalSize = (size_t)n * NODE_SIZE;
    size_t off = allocInSandbox(totalSize, id, err);
    if (off == SIZE_MAX) { g_nextId--; return false; }

    memset(g_sandbox + off, 0, alignUp(totalSize));
    // Write left/right offsets (BFS layout: left child = 2i+1, right = 2i+2)
    for (int i = 0; i < n; i++) {
        uint8_t* node = g_sandbox + off + (size_t)i * NODE_SIZE;
        // key = i+1
        uint32_t key = (uint32_t)(i + 1);
        memcpy(node + 16, &key, 4);
        int leftIdx  = 2 * i + 1;
        int rightIdx = 2 * i + 2;
        uint64_t leftOff  = (leftIdx  < n) ? (uint64_t)(off + (size_t)leftIdx  * NODE_SIZE) : 0;
        uint64_t rightOff = (rightIdx < n) ? (uint64_t)(off + (size_t)rightIdx * NODE_SIZE) : 0;
        memcpy(node,     &leftOff,  8);
        memcpy(node + 8, &rightOff, 8);
    }

    Block b;
    b.id        = id;
    b.offset    = off;
    b.size      = alignUp(totalSize);
    b.used      = true;
    b.label     = label.empty() ? "BinTree" : label;
    b.isStruct  = true;
    b.structType= "TREE";
    b.elemCount = n;
    b.elemSize  = NODE_SIZE;
    g_blocks.push_back(b);

    std::cout << "STRUCTDEF " << id << " TREE "
              << off << " " << b.size << " "
              << NODE_SIZE << " " << n << " " << b.label << "\n";
    for (int i = 0; i < n; i++) {
        uint64_t leftOff = 0, rightOff = 0;
        memcpy(&leftOff,  g_sandbox + off + (size_t)i * NODE_SIZE,     8);
        memcpy(&rightOff, g_sandbox + off + (size_t)i * NODE_SIZE + 8, 8);
        std::cout << "NODE " << id << " " << i << " "
                  << (off + (size_t)i * NODE_SIZE) << " "
                  << leftOff << " " << rightOff << "\n";
    }
    return true;
}

// Hash table: bucket array (each bucket = 16 bytes) + metadata block (64 bytes)
static bool cmdStructHash(int buckets, const std::string& label, std::string& err) {
    if (buckets <= 0 || buckets > 256) { err = "buckets must be 1–256"; return false; }
    constexpr size_t BUCKET_SIZE = 16;
    constexpr size_t META_SIZE   = 64;
    int id = g_nextId++;
    size_t totalSize = META_SIZE + (size_t)buckets * BUCKET_SIZE;
    size_t off = allocInSandbox(totalSize, id, err);
    if (off == SIZE_MAX) { g_nextId--; return false; }

    memset(g_sandbox + off, 0, alignUp(totalSize));
    // Metadata: bucket_count(4), load_factor_x100(4), ...
    uint32_t bcount = (uint32_t)buckets;
    uint32_t lf     = 75; // 0.75 load factor
    memcpy(g_sandbox + off,     &bcount, 4);
    memcpy(g_sandbox + off + 4, &lf,     4);

    Block b;
    b.id        = id;
    b.offset    = off;
    b.size      = alignUp(totalSize);
    b.used      = true;
    b.label     = label.empty() ? "HashMap" : label;
    b.isStruct  = true;
    b.structType= "HASH";
    b.elemCount = buckets;
    b.elemSize  = BUCKET_SIZE;
    g_blocks.push_back(b);

    std::cout << "STRUCTDEF " << id << " HASH "
              << off << " " << b.size << " "
              << BUCKET_SIZE << " " << buckets << " " << b.label << "\n";
    // Report each bucket offset
    for (int i = 0; i < buckets; i++) {
        size_t boff = off + META_SIZE + (size_t)i * BUCKET_SIZE;
        std::cout << "NODE " << id << " " << i << " " << boff << " 0 0\n";
    }
    return true;
}

// ─────────────────── mode switch ─────────────────────────────────────────────

static void switchMode(const std::string& name) {
    cmdReset();
    if      (name == "paged")     g_mode = Mode::Paged;
    else if (name == "segmented") g_mode = Mode::Segmented;
    else if (name == "framed")    g_mode = Mode::Framed;
    else                          g_mode = Mode::Contiguous;
    // Re-init the technique-specific tables
    initFreeList();
    clearPageTable();
    clearSegTable();
}

} // namespace

// ─────────────────── main loop ───────────────────────────────────────────────

int main() {
    // Allocate the 4 MB sandbox
    g_sandbox = (uint8_t*)mmap(nullptr, SANDBOX_SIZE,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_sandbox == MAP_FAILED) {
        std::cerr << "ERR mmap sandbox failed\n";
        return 1;
    }
    memset(g_sandbox, 0, SANDBOX_SIZE);
    initFreeList();
    clearPageTable();
    clearSegTable();

    std::cout << "READY " << SANDBOX_SIZE << "\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::string err;

        if (cmd == "MODE") {
            std::string name; ss >> name;
            switchMode(name);
            std::cout << "RESET_LOG\n";
            printBlocks(); printArenaSummary();
            std::cout << "OK\n";

        } else if (cmd == "ALLOC") {
            size_t size; ss >> size;
            std::string label; std::getline(ss, label);
            if (!label.empty() && label[0] == ' ') label = label.substr(1);
            if (cmdAlloc(size, label, err)) {
                printBlocks(); printArenaSummary();
                std::cout << "OK\n";
            } else {
                std::cout << "ERR " << err << "\n";
            }

        } else if (cmd == "STRUCT") {
            std::string type; ss >> type;
            bool ok = false;

            if (type == "LL") {
                int n; ss >> n;
                std::string lbl; std::getline(ss, lbl);
                if (!lbl.empty() && lbl[0] == ' ') lbl = lbl.substr(1);
                ok = cmdStructLL(n, lbl, err);
            } else if (type == "ARRAY") {
                int n; size_t esize; ss >> n >> esize;
                std::string lbl; std::getline(ss, lbl);
                if (!lbl.empty() && lbl[0] == ' ') lbl = lbl.substr(1);
                ok = cmdStructArray(n, esize, lbl, err);
            } else if (type == "TREE") {
                int n; ss >> n;
                std::string lbl; std::getline(ss, lbl);
                if (!lbl.empty() && lbl[0] == ' ') lbl = lbl.substr(1);
                ok = cmdStructTree(n, lbl, err);
            } else if (type == "HASH") {
                int buckets; ss >> buckets;
                std::string lbl; std::getline(ss, lbl);
                if (!lbl.empty() && lbl[0] == ' ') lbl = lbl.substr(1);
                ok = cmdStructHash(buckets, lbl, err);
            } else {
                err = "unknown struct type: " + type;
            }

            if (ok) {
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

        } else if (cmd == "RESET") {
            std::cout << "RESET_LOG\n";
            cmdReset();
            printBlocks(); printArenaSummary();
            std::cout << "OK\n";

        } else if (cmd == "PAGETABLE") {
            printPageTable();
            std::cout << "OK\n";

        } else if (cmd == "SEGTABLE") {
            printSegTable();
            std::cout << "OK\n";

        } else if (cmd == "FRAMETABLE") {
            printFrameTable();
            std::cout << "OK\n";

        } else if (cmd == "STATUS") {
            printBlocks(); printArenaSummary();
            std::cout << "OK\n";

        } else if (cmd == "GETPID") {
            std::cout << "WORKERPID " << (int)getpid() << "\nOK\n";

        } else if (cmd == "PING") {
            std::cout << "PONG\nOK\n";

        } else {
            std::cout << "ERR unknown command: " << cmd << "\n";
        }
        std::cout << std::flush;
    }

    munmap(g_sandbox, SANDBOX_SIZE);
    return 0;
}
