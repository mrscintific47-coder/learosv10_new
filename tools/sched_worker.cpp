// sched_worker — LearnOS Scheduler Lab backend.
//
// Spawns N pthreads and applies the requested scheduling policy via
// sched_setscheduler() or sched_setattr().  Emits periodic STATUS lines so
// the GUI can follow along.  The GUI also reads /proc directly for richer data.
//
// Usage: sched_worker <nThreads> <policy>
//   policy: OTHER | FIFO | RR | DEADLINE | BATCH | IDLE
//
// Output protocol (line-delimited stdout):
//   READY <pid>                          — worker is running, threads are up
//   STATUS pid=<P> nthreads=<N> policy=<POL>
//   POLICY_APPLIED policy=<POL> tid=<T> result=ok|eperm
//   ERROR <message>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <atomic>
#include <string>
#include <cmath>

// ── globals ───────────────────────────────────────────────────────────────────

static std::atomic<int>  g_running(1);
static int               g_nThreads = 4;
static std::string       g_policy   = "OTHER";

// ── worker thread fn ──────────────────────────────────────────────────────────

static void applyPolicy(const std::string& policy) {
    long tid = syscall(SYS_gettid);

    if (policy == "FIFO" || policy == "RR") {
        int sched = (policy == "FIFO") ? SCHED_FIFO : SCHED_RR;
        struct sched_param sp = {};
        sp.sched_priority = 1; // minimum real-time priority
        int r = sched_setscheduler(0, sched, &sp);
        if (r == 0)
            fprintf(stdout, "POLICY_APPLIED policy=%s tid=%ld result=ok\n",
                    policy.c_str(), tid);
        else
            fprintf(stdout, "POLICY_APPLIED policy=%s tid=%ld result=%s\n",
                    policy.c_str(), tid, (errno == EPERM) ? "eperm" : strerror(errno));
        fflush(stdout);

    } else if (policy == "DEADLINE") {
        // sched_setattr() with SCHED_DEADLINE — requires CAP_SYS_NICE
        // runtime=5ms, deadline=10ms, period=10ms
        struct {
            uint32_t size;
            uint32_t sched_policy;
            uint64_t sched_flags;
            int32_t  sched_nice;
            uint32_t sched_priority;
            uint64_t sched_runtime;
            uint64_t sched_deadline;
            uint64_t sched_period;
        } attr = {};
        attr.size          = sizeof(attr);
        attr.sched_policy  = 6; // SCHED_DEADLINE
        attr.sched_runtime  = 5000000ULL;   // 5ms
        attr.sched_deadline = 10000000ULL;  // 10ms
        attr.sched_period   = 10000000ULL;  // 10ms
        int r = (int)syscall(SYS_sched_setattr, 0, &attr, 0);
        if (r == 0)
            fprintf(stdout, "POLICY_APPLIED policy=DEADLINE tid=%ld result=ok\n", tid);
        else
            fprintf(stdout, "POLICY_APPLIED policy=DEADLINE tid=%ld result=%s\n",
                    tid, (errno == EPERM) ? "eperm" : strerror(errno));
        fflush(stdout);

    } else if (policy == "BATCH") {
        struct sched_param sp = {};
        int r = sched_setscheduler(0, SCHED_BATCH, &sp);
        if (r == 0)
            fprintf(stdout, "POLICY_APPLIED policy=BATCH tid=%ld result=ok\n", tid);
        else
            fprintf(stdout, "POLICY_APPLIED policy=BATCH tid=%ld result=%s\n",
                    tid, strerror(errno));
        fflush(stdout);

    } else if (policy == "IDLE") {
        struct sched_param sp = {};
        int r = sched_setscheduler(0, SCHED_IDLE, &sp);
        if (r == 0)
            fprintf(stdout, "POLICY_APPLIED policy=IDLE tid=%ld result=ok\n", tid);
        else
            fprintf(stdout, "POLICY_APPLIED policy=IDLE tid=%ld result=%s\n",
                    tid, strerror(errno));
        fflush(stdout);

    } else {
        // SCHED_OTHER (default) — just ensure we're on it
        struct sched_param sp = {};
        sched_setscheduler(0, SCHED_OTHER, &sp);
        fprintf(stdout, "POLICY_APPLIED policy=OTHER tid=%ld result=ok\n", tid);
        fflush(stdout);
    }
}

static void* workerThread(void* arg) {
    long idx = (long)arg;
    (void)idx;

    applyPolicy(g_policy);

    // CPU-bound loop so the scheduler gets interesting things to schedule
    volatile double x = 1.0 + (double)idx * 0.001;
    while (g_running.load()) {
        for (int i = 0; i < 10000; i++)
            x = std::sin(x) * std::cos(x) + 1.0001;
        // Tiny voluntary yield so SCHED_FIFO threads don't starve others
        // for DEADLINE, we sleep long enough to not exceed runtime
        if (g_policy == "DEADLINE")
            usleep(6000); // 6ms sleep per 10ms period — stays within budget
        else
            usleep(200);  // 200µs cooperate
    }
    return nullptr;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        int n = atoi(argv[1]);
        if (n >= 1 && n <= 32) g_nThreads = n;
    }
    if (argc >= 3) {
        g_policy = argv[2];
    }

    // Line-buffered stdout so GUI reads lines as they arrive
    setvbuf(stdout, nullptr, _IOLBF, 0);

    pthread_t* threads = new pthread_t[g_nThreads];

    for (int i = 0; i < g_nThreads; i++) {
        int r = pthread_create(&threads[i], nullptr, workerThread, (void*)(long)i);
        if (r != 0) {
            fprintf(stdout, "ERROR pthread_create failed: %s\n", strerror(r));
            fflush(stdout);
            return 1;
        }
    }

    // Report ready — GUI starts sampling /proc after this
    fprintf(stdout, "READY %d\n", (int)getpid());
    fflush(stdout);

    // Emit periodic STATUS so GUI knows we're still alive
    int tick = 0;
    while (g_running.load()) {
        sleep(1);
        tick++;
        fprintf(stdout, "STATUS pid=%d nthreads=%d policy=%s tick=%d\n",
                (int)getpid(), g_nThreads, g_policy.c_str(), tick);
        fflush(stdout);
    }

    for (int i = 0; i < g_nThreads; i++)
        pthread_join(threads[i], nullptr);

    delete[] threads;
    return 0;
}
