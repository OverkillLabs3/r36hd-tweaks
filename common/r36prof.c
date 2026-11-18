/* libr36prof.so: a sampling profiler for the ports, loaded with LD_PRELOAD when the file /mnt/sdcard/cubegm/prof_on exists.
 * A 250 Hz CPU-time timer records the interrupted program counter, so the result includes every stall (cache and TLB
 * misses), which an instruction count does not. Output (little endian uint32 pc values), one set per port tag (R36_PROF_TAG):
 *   /mnt/sdcard/cubegm/logs/prof_<tag>_samples.bin and the memory map prof_<tag>_maps.txt (rewritten every 20 s)
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
static char tag[32] = "game";

static void on_prof(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; (void)si;
    ucontext_t *uc = (ucontext_t *)uc_;
    uint32_t pc = (uint32_t)uc->uc_mcontext.pc;
    int i = __sync_fetch_and_add(&n, 1);          // two threads can be interrupted at once
    if (i < NS) buf[i] = pc;
    if (i == NS - 1 && fd >= 0) {
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
        char mp[96]; snprintf(mp, sizeof mp, "/mnt/sdcard/cubegm/logs/prof_%s_maps.txt", tag);
        int out = open(mp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
    char exe[256]; ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (el <= 0) return;
    exe[el] = 0;
    const char *bn = strrchr(exe, 47); bn = bn ? bn + 1 : exe;
    if (strcmp(bn, "picoarch") != 0) return;          // only the frontend process, never the shell or helpers
    const char *tg = getenv("R36_PROF_TAG");
    if (tg && *tg) snprintf(tag, sizeof tag, "%s", tg);
    char sp[96]; snprintf(sp, sizeof sp, "/mnt/sdcard/cubegm/logs/prof_%s_samples.bin", tag);
    fd = open(sp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
