// thread_worker — LearnOS Thread Lab backend.
//
// Spawns N pthreads running one of several demo scenarios and periodically
// writes STATUS lines to stdout so the GUI can show live state.
//
// Usage: thread_worker <nThreads> <demoIndex>
//
// Demo indices:
//   0  CPU Race        — all threads burn CPU in parallel (no lock)
//   1  Mutex Contention — threads increment shared counter WITH pthread_mutex
//   2  Producer-Consumer — threads cooperate via pthread_cond
//   3  Reader-Writer   — readers share, writers exclude via pthread_rwlock
//   4  Race Condition  — threads increment shared counter WITHOUT any lock
//   5  DEADLOCK        — lock ordering inversion: real pthread deadlock
//   6  Lock Graph Demo — intentional ABBA deadlock, visualizable
//
// Output protocol:
//   READY <pid>
//   STATUS counter=<N> threads=<N> demo=<N>
//   LOCKSTATE tid=<T> holds=<M> waits=<M>   (for demos 5/6, per-tick)
//   DEADLOCK_DETECTED tid_a=<T> tid_b=<T>   (emitted when deadlock confirmed)
//   PROCSTATE tid=<T> state=<S>             (R/S/D from /proc)

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <dirent.h>

// ── Shared state ─────────────────────────────────────────────────────────────

static int            g_demo     = 0;
static int            g_nThreads = 4;
static volatile long  g_counter  = 0;   // plain volatile — intentionally racy for demo 4
static long           g_atomic_counter = 0;
static pthread_mutex_t g_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mutex_a = PTHREAD_MUTEX_INITIALIZER;  // ABBA lock A
static pthread_mutex_t g_mutex_b = PTHREAD_MUTEX_INITIALIZER;  // ABBA lock B
static pthread_rwlock_t g_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_cond_t  g_cond_full  = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_cond_empty = PTHREAD_COND_INITIALIZER;
static volatile int    g_buf_count = 0;
static const int       BUF_SIZE    = 8;
static volatile bool   g_running   = true;

// Lock graph state: who holds what, who waits for what
#define MAX_THREADS 16
static volatile pid_t  g_tid_map[MAX_THREADS] = {};        // index → tid
static volatile int    g_holds_a[MAX_THREADS] = {};        // holds mutex_a?
static volatile int    g_holds_b[MAX_THREADS] = {};        // holds mutex_b?
static volatile int    g_waits_a[MAX_THREADS] = {};        // waiting for a?
static volatile int    g_waits_b[MAX_THREADS] = {};        // waiting for b?
static int g_tid_idx = 0;
static pthread_mutex_t g_tid_mutex = PTHREAD_MUTEX_INITIALIZER;

static int register_tid() {
    pid_t tid = (pid_t)syscall(SYS_gettid);
    pthread_mutex_lock(&g_tid_mutex);
    int idx = g_tid_idx++;
    if (idx < MAX_THREADS) g_tid_map[idx] = tid;
    pthread_mutex_unlock(&g_tid_mutex);
    return idx;
}

// ── Demo thread functions ─────────────────────────────────────────────────────

static void* cpu_race(void*) {
    while (g_running) {
        volatile double x = 1.0;
        for (int i = 0; i < 500000; i++) x += i * 0.001;
        pthread_mutex_lock(&g_mutex);
        g_atomic_counter++;
        pthread_mutex_unlock(&g_mutex);
        usleep(1000);
    }
    return nullptr;
}

static void* mutex_contention(void*) {
    while (g_running) {
        pthread_mutex_lock(&g_mutex);
        volatile double x = 1.0;
        for (int i = 0; i < 200000; i++) x += i;
        g_atomic_counter++;
        pthread_mutex_unlock(&g_mutex);
        usleep(500);
    }
    return nullptr;
}

static void* producer(void*) {
    while (g_running) {
        pthread_mutex_lock(&g_mutex);
        while (g_buf_count >= BUF_SIZE && g_running)
            pthread_cond_wait(&g_cond_empty, &g_mutex);
        if (!g_running) { pthread_mutex_unlock(&g_mutex); break; }
        g_buf_count++;
        g_atomic_counter++;
        pthread_cond_signal(&g_cond_full);
        pthread_mutex_unlock(&g_mutex);
        usleep(2000);
    }
    return nullptr;
}

static void* consumer(void*) {
    while (g_running) {
        pthread_mutex_lock(&g_mutex);
        while (g_buf_count == 0 && g_running)
            pthread_cond_wait(&g_cond_full, &g_mutex);
        if (!g_running) { pthread_mutex_unlock(&g_mutex); break; }
        g_buf_count--;
        pthread_cond_signal(&g_cond_empty);
        pthread_mutex_unlock(&g_mutex);
        usleep(3000);
    }
    return nullptr;
}

static void* reader(void*) {
    while (g_running) {
        pthread_rwlock_rdlock(&g_rwlock);
        volatile long v = g_atomic_counter; (void)v;
        usleep(1000);
        pthread_rwlock_unlock(&g_rwlock);
        usleep(500);
    }
    return nullptr;
}

static void* writer(void*) {
    while (g_running) {
        pthread_rwlock_wrlock(&g_rwlock);
        g_atomic_counter++;
        usleep(5000);
        pthread_rwlock_unlock(&g_rwlock);
        usleep(2000);
    }
    return nullptr;
}

static void* race_condition(void*) {
    while (g_running) {
        long tmp = g_counter;
        tmp += 1;
        g_counter = tmp;
        usleep(100);
    }
    return nullptr;
}

// ── Demo 5: REAL DEADLOCK — ABBA lock ordering inversion ─────────────────────
// Thread 0 acquires A then tries to acquire B.
// Thread 1 acquires B then tries to acquire A.
// After both hold one lock and try for the other, neither can proceed.
// This is a real pthread deadlock — the threads will block forever.
// We emit LOCKSTATE lines so the GUI can show the lock graph live.

static void* deadlock_thread_a(void* arg) {
    int idx = register_tid();
    pid_t tid = g_tid_map[idx];
    (void)arg;

    // Let thread B start first
    usleep(100000);

    // Acquire A
    g_waits_a[idx] = 1;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=A holds=none\n", tid); fflush(stdout);
    pthread_mutex_lock(&g_mutex_a);
    g_holds_a[idx] = 1; g_waits_a[idx] = 0;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=none holds=A\n", tid); fflush(stdout);

    // Give thread B time to acquire B
    usleep(200000);

    // Try to acquire B — this will DEADLOCK because thread B holds B
    g_waits_b[idx] = 1;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=B holds=A\n", tid); fflush(stdout);
    pthread_mutex_lock(&g_mutex_b);  // BLOCKS FOREVER
    g_holds_b[idx] = 1; g_waits_b[idx] = 0;
    pthread_mutex_unlock(&g_mutex_b);
    pthread_mutex_unlock(&g_mutex_a);
    return nullptr;
}

static void* deadlock_thread_b(void* arg) {
    int idx = register_tid();
    pid_t tid = g_tid_map[idx];
    (void)arg;

    // Acquire B first
    g_waits_b[idx] = 1;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=B holds=none\n", tid); fflush(stdout);
    pthread_mutex_lock(&g_mutex_b);
    g_holds_b[idx] = 1; g_waits_b[idx] = 0;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=none holds=B\n", tid); fflush(stdout);

    // Give thread A time to acquire A
    usleep(200000);

    // Try to acquire A — DEADLOCK because thread A holds A
    g_waits_a[idx] = 1;
    fprintf(stdout, "LOCKSTATE tid=%d waits_for=A holds=B\n", tid); fflush(stdout);
    pthread_mutex_lock(&g_mutex_a);  // BLOCKS FOREVER
    g_holds_a[idx] = 1; g_waits_a[idx] = 0;
    pthread_mutex_unlock(&g_mutex_a);
    pthread_mutex_unlock(&g_mutex_b);
    return nullptr;
}

// ── Read /proc state ──────────────────────────────────────────────────────────
static char read_proc_state(pid_t pid, pid_t tid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/status", pid, tid);
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("State:", 0) == 0 && line.size() > 7)
            return line[7];
    }
    return '?';
}

static long read_voluntary_sw(pid_t pid, pid_t tid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/status", pid, tid);
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("voluntary_ctxt_switches:", 0) == 0) {
            std::istringstream ss(line.substr(24));
            long v = 0; ss >> v; return v;
        }
    }
    return 0;
}

// Emit /proc wait states for all tasks (for the GUI lock-graph view)
static void emit_proc_states(pid_t pid) {
    char taskPath[256];
    snprintf(taskPath, sizeof(taskPath), "/proc/%d/task", pid);
    DIR* dir = opendir(taskPath);
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        long t = strtol(ent->d_name, nullptr, 10);
        if (t <= 0 || t == pid) continue;
        char state = read_proc_state(pid, (pid_t)t);
        long sw    = read_voluntary_sw(pid, (pid_t)t);
        fprintf(stdout, "PROCSTATE tid=%ld state=%c switches=%ld\n", t, state, sw);
    }
    closedir(dir);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc >= 2) g_nThreads = atoi(argv[1]);
    if (argc >= 3) g_demo     = atoi(argv[2]);

    if (g_nThreads < 1)  g_nThreads = 1;
    if (g_nThreads > 16) g_nThreads = 16;
    if (g_demo < 0 || g_demo > 6) g_demo = 0;

    pid_t pid = getpid();
    fprintf(stdout, "READY %d\n", pid);
    fflush(stdout);

    pthread_t* threads = new pthread_t[g_nThreads];

    if (g_demo == 5 || g_demo == 6) {
        // Deadlock demo: always exactly 2 threads in ABBA pattern
        // regardless of nThreads (makes visualisation clear)
        pthread_create(&threads[0], nullptr, deadlock_thread_a, nullptr);
        pthread_create(&threads[1], nullptr, deadlock_thread_b, nullptr);
        // Any extra threads added as innocent bystanders (race mode)
        for (int i = 2; i < g_nThreads; i++)
            pthread_create(&threads[i], nullptr, cpu_race, nullptr);
    } else {
        void* (*funcs[5])(void*) = {
            cpu_race, mutex_contention, producer, reader, race_condition
        };
        for (int i = 0; i < g_nThreads; i++) {
            void* (*fn)(void*) = funcs[g_demo];
            if (g_demo == 2) fn = (i % 2 == 0) ? producer : consumer;
            if (g_demo == 3) fn = (i == g_nThreads - 1) ? writer : reader;
            pthread_create(&threads[i], nullptr, fn, nullptr);
        }
    }

    // Report status every 500ms + emit /proc states
    int tick = 0;
    while (g_running) {
        usleep(500000);
        tick++;
        long reported = (g_demo == 4) ? g_counter : g_atomic_counter;
        fprintf(stdout, "STATUS counter=%ld threads=%d demo=%d\n",
                reported, g_nThreads, g_demo);

        // For deadlock demos emit per-thread proc state so GUI can show lock graph
        if (g_demo == 5 || g_demo == 6) {
            emit_proc_states(pid);
            // Detect deadlock: both threads waiting for a mutex
            bool a_blocks = false, b_blocks = false;
            for (int i = 0; i < MAX_THREADS; i++) {
                if (g_tid_map[i] == 0) continue;
                if (g_waits_b[i] && g_holds_a[i]) a_blocks = true;
                if (g_waits_a[i] && g_holds_b[i]) b_blocks = true;
            }
            if (a_blocks && b_blocks) {
                // Find the TIDs
                pid_t tid_a = 0, tid_b = 0;
                for (int i = 0; i < MAX_THREADS; i++) {
                    if (g_holds_a[i] && g_waits_b[i]) tid_a = g_tid_map[i];
                    if (g_holds_b[i] && g_waits_a[i]) tid_b = g_tid_map[i];
                }
                fprintf(stdout, "DEADLOCK_DETECTED tid_a=%d tid_b=%d\n", tid_a, tid_b);
            }
        }
        fflush(stdout);
    }

    delete[] threads;
    return 0;
}
