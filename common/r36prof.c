/* libr36prof.so: a sampling profiler for the ports, loaded with LD_PRELOAD when the file /mnt/sdcard/cubegm/prof_on exists.
 * A 250 Hz CPU-time timer records the interrupted program counter, so the result includes every stall (cache and TLB
 * misses), which an instruction count does not. Output (appended every 10 s, little endian uint32 pc values):
 *   /mnt/sdcard/cubegm/logs/prof_samples.bin    and the memory map /mnt/sdcard/cubegm/logs/prof_maps.txt (rewritten every 20 s)
 * Turn it into a per-function table on the PC with tools/prof_report.py (needs the unstripped or symbol-keeping core). */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <ucontext.h>
#include <sys/time.h>

#define NS 2500
static uint32_t buf[NS];
static volatile int n;
static int fd = -1;

static void on_prof(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; (void)si;
    ucontext_t *uc = (ucontext_t *)uc_;
    uint32_t pc = (uint32_t)uc->uc_mcontext.pc;
    int i = n;
    if (i < NS) { buf[i] = pc; n = i + 1; }
    if (n >= NS && fd >= 0) {
        if (write(fd, buf, sizeof buf) < 0) { }
        n = 0;
    }
}

static void *maps_thread(void *a)
{
    (void)a;
    for (int round = 0;; round++) {
        sleep(round ? 20 : 8);
        int in = open("/proc/self/maps", O_RDONLY);
        int out = open("/mnt/sdcard/cubegm/logs/prof_maps.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (in >= 0 && out >= 0) {
            char b[4096]; ssize_t r;
            while ((r = read(in, b, sizeof b)) > 0) if (write(out, b, (size_t)r) < 0) break;
        }
        if (in >= 0) close(in);
        if (out >= 0) close(out);
    }
    return NULL;
}

__attribute__((constructor)) static void prof_init(void)
{
    if (access("/mnt/sdcard/cubegm/prof_on", F_OK) != 0) return;
    fd = open("/mnt/sdcard/cubegm/logs/prof_samples.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = { { 0, 4000 }, { 0, 4000 } };
    setitimer(ITIMER_PROF, &it, NULL);
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&th, &at, maps_thread, NULL);
}
