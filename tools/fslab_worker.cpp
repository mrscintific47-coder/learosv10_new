/**
 * fslab_worker — a tiny process used by FilesystemLab's real-filesystem experiments.
 *
 * Usage:
 *   fslab_worker lock  <path> <role A|B> <fd-report-pipe-write-end>
 *       Opens <path>, acquires an exclusive flock(), holds it 8 seconds,
 *       releases it.  Every state change is reported as a line to the pipe:
 *           TRYING\n
 *           LOCKED\n
 *           RELEASED\n
 *
 *   fslab_worker read  <path> <io-report-pipe-write-end>
 *       Opens <path>, reads all bytes, reports bytes read and elapsed µs.
 *           BYTES <n>\n
 *           USEC  <n>\n
 *
 *   fslab_worker write <path> <content> <io-report-pipe-write-end>
 *       Writes <content> to <path> (O_WRONLY|O_CREAT|O_TRUNC), reports:
 *           BYTES <n>\n
 *           USEC  <n>\n
 *
 *   fslab_worker odirect <path> <io-report-pipe-write-end>
 *       Tries O_DIRECT read; falls back to buffered if EINVAL.
 *       Reports MODE buffered|direct plus BYTES and USEC.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>   // flock
#include <sys/stat.h>

static void report(int pfd, const char* msg) {
    write(pfd, msg, strlen(msg));
}

static int open_pipe(const char* arg) {
    return atoi(arg);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::string mode = argv[1];

    // ── lock mode ────────────────────────────────────────────────────────────
    if (mode == "lock" && argc >= 5) {
        const char* path  = argv[2];
        // argv[3] = role ("A" or "B") — unused in logic, parent labels it
        int pfd = open_pipe(argv[4]);

        int fd = open(path, O_RDWR | O_CREAT, 0644);
        if (fd < 0) { report(pfd, "ERROR open\n"); return 1; }

        report(pfd, "TRYING\n");
        if (flock(fd, LOCK_EX) != 0) {   // blocks until lock is available
            report(pfd, "ERROR flock\n");
            close(fd); return 1;
        }
        report(pfd, "LOCKED\n");
        sleep(6);                          // hold the lock for 6 seconds
        flock(fd, LOCK_UN);
        report(pfd, "RELEASED\n");
        close(fd);
        return 0;
    }

    // ── read mode ────────────────────────────────────────────────────────────
    if (mode == "read" && argc >= 4) {
        const char* path = argv[2];
        int pfd = open_pipe(argv[3]);

        auto t0 = std::chrono::steady_clock::now();
        int fd = open(path, O_RDONLY);
        if (fd < 0) { report(pfd, "ERROR open\n"); return 1; }
        char buf[4096];
        ssize_t total = 0, n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) total += n;
        close(fd);
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();

        char line[128];
        snprintf(line, sizeof(line), "BYTES %zd\n", total);  report(pfd, line);
        snprintf(line, sizeof(line), "USEC  %lld\n", (long long)usec); report(pfd, line);
        return 0;
    }

    // ── write mode ───────────────────────────────────────────────────────────
    if (mode == "write" && argc >= 5) {
        const char* path    = argv[2];
        const char* content = argv[3];
        int pfd = open_pipe(argv[4]);

        auto t0 = std::chrono::steady_clock::now();
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { report(pfd, "ERROR open\n"); return 1; }
        size_t len = strlen(content);
        ssize_t written = write(fd, content, len);
        fsync(fd);
        close(fd);
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();

        char line[128];
        snprintf(line, sizeof(line), "BYTES %zd\n", written);  report(pfd, line);
        snprintf(line, sizeof(line), "USEC  %lld\n", (long long)usec); report(pfd, line);
        return 0;
    }

    // ── odirect mode ─────────────────────────────────────────────────────────
    if (mode == "odirect" && argc >= 4) {
        const char* path = argv[2];
        int pfd = open_pipe(argv[3]);

        // Try O_DIRECT first; fall back to buffered on EINVAL
        int flags = O_RDONLY;
#ifdef O_DIRECT
        flags |= O_DIRECT;
#endif
        int fd = open(path, flags);
        bool direct = (fd >= 0);
        if (!direct) {
            fd = open(path, O_RDONLY);
            if (fd < 0) { report(pfd, "ERROR open\n"); return 1; }
        }

        report(pfd, direct ? "MODE direct\n" : "MODE buffered\n");

        auto t0 = std::chrono::steady_clock::now();
        // For O_DIRECT reads we need aligned buffer
        char* buf = nullptr;
        if (posix_memalign((void**)&buf, 512, 4096*4) != 0) {
            close(fd); report(pfd, "ERROR memalign\n"); return 1;
        }
        ssize_t total = 0, n;
        while ((n = read(fd, buf, 4096*4)) > 0) total += n;
        free(buf);
        close(fd);
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();

        char line[128];
        snprintf(line, sizeof(line), "BYTES %zd\n", total);  report(pfd, line);
        snprintf(line, sizeof(line), "USEC  %lld\n", (long long)usec); report(pfd, line);
        return 0;
    }

    return 1;
}
