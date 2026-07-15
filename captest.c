#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <fcntl.h>

int main() {
    printf("uid=%d euid=%d\n", getuid(), geteuid());

    FILE* f = fopen("/proc/self/status", "r");
    char line[256];
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "Cap", 3) == 0) printf("%s", line);
    fclose(f);

    int r = unshare(CLONE_NEWUTS);
    printf("unshare(CLONE_NEWUTS): %s (errno=%d)\n",
           r==0 ? "SUCCESS" : strerror(errno), r==0 ? 0 : errno);

    struct perf_event_attr attr = {};
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
    attr.disabled = 1;
    long fd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, 0);
    printf("perf_event_open(pid=-1): %s (errno=%d)\n",
           fd>=0 ? "SUCCESS" : strerror(errno), fd<0 ? errno : 0);
    if (fd >= 0) close((int)fd);
    return 0;
}
