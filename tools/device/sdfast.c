/* sdfast: run the SD card at its high-speed clock (50 MHz) instead of the 20 MHz the device tree allows, at run time,
 * through the driver's own clock change. Resets on reboot.
 *  1. Find the mmc host structure in kernel RAM through /dev/mem: three consecutive words f_min = 100000,
 *     f_max = 20000000, f_init = 400000 (struct mmc_host). Exactly one match is required.
 *  2. Raise f_max to 50000000 there.
 *  3. Ask the mmc core for the new clock: echo 50000000 > /sys/kernel/debug/mmc0/clock (mmc_set_clock under the host
 *     claim, so no transfer is cut).
 *  4. Report /sys/kernel/debug/mmc0/ios before and after, and time a 12 MB cold read.
 * Usage: sdfast <report> [testfile] [hz]          hz default 50000000; "sdfast <report> - 0" only reports
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/mount.h>

#define RAM_END 0x02f91000u
static FILE *rep;
static void say(const char *f, ...)
{
    va_list ap; va_start(ap, f); vfprintf(rep, f, ap); va_end(ap);
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    fflush(rep); fsync(fileno(rep));
}
static void cat(const char *p)
{
    char b[1024]; FILE *f = fopen(p, "r");
    if (!f) { say("(cannot read %s)\n", p); return; }
    size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
    say("%s", b);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double readtest(const char *path, int mb)
{
    static char buf[256 * 1024];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    int dc = open("/proc/sys/vm/drop_caches", O_WRONLY); if (dc >= 0) { if (write(dc, "3", 1) < 0) {} close(dc); }
    long tot = 0, want = (long)mb << 20; double t0 = now();
    while (tot < want) { ssize_t n = read(fd, buf, sizeof buf); if (n <= 0) break; tot += n; }
    double t = now() - t0; close(fd);
    return tot / t / 1e6;
}

int main(int argc, char **argv)
{
    rep = fopen(argc > 1 ? argv[1] : "/mnt/sdcard/sdfast_report.txt", "w");
    if (!rep) return 1;
    const char *file = argc > 2 && strcmp(argv[2], "-") ? argv[2] : "/mnt/sdcard/cubegm/UI_Res.cpd";
    uint32_t hz = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 50000000u;
    if (access("/sys/kernel/debug/mmc0/ios", F_OK)) mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);
    say("sdfast, target %u Hz\n-- ios before:\n", hz); cat("/sys/kernel/debug/mmc0/ios");
    if (strcmp(file, "none")) say("read before: %.2f MB/s\n", readtest(file, 12));
    if (!hz) { say("report only\n"); return 0; }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { say("no /dev/mem\n"); return 0; }
    uint32_t found = 0, nfound = 0;
    for (uint32_t base = 0; base < RAM_END; base += 1u << 20) {
        uint32_t len = (RAM_END - base) < (1u << 20) ? RAM_END - base : (1u << 20);
        volatile uint32_t *m = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, base);
        if (m == MAP_FAILED) { say("mmap of 0x%08x failed, stop\n", base); return 0; }
        for (uint32_t i = 0; i + 2 < len / 4; i++)
            if (m[i] == 100000u && m[i + 1] == 20000000u && m[i + 2] == 400000u) { found = base + i * 4; nfound++; }
        munmap((void *)m, len);
    }
    say("f_min/f_max/f_init pattern found %u time(s), last at physical 0x%08x\n", nfound, found);
    if (nfound != 1) { say("need exactly one match, nothing changed\n"); return 0; }
    uint32_t pg = found & ~4095u;
    volatile uint32_t *w = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pg);
    if (w == MAP_FAILED) { say("mmap rw failed\n"); return 0; }
    volatile uint32_t *fm = w + ((found & 4095u) / 4);
    say("context:"); for (int i = -4; i < 8; i++) say(" %08x", fm[i]); say("\n");
    fm[1] = hz;
    __sync_synchronize();
    say("f_max now %u\n", fm[1]);
    munmap((void *)w, 4096); close(fd);

    FILE *c = fopen("/sys/kernel/debug/mmc0/clock", "w");
    if (!c) { say("cannot open debugfs clock\n"); return 0; }
    fprintf(c, "%u\n", hz); int r = fclose(c);
    say("debugfs clock write: %s\n-- ios after:\n", r == 0 ? "ok" : "failed"); cat("/sys/kernel/debug/mmc0/ios");
    /* write check at the new clock: 2 MB written, synced, dropped from the cache, read back and compared.
       On any difference the clock goes back to 20 MHz (debugfs, then f_max). */
    {
        static uint32_t wb[512 * 1024], rb[512 * 1024];
        const char *tp = "/mnt/sdcard/cubegm/logs/.sdfast_check";
        for (uint32_t i = 0; i < 512 * 1024; i++) wb[i] = i * 2654435761u ^ (uint32_t)now();
        int ok = 0, tf = open(tp, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (tf >= 0 && write(tf, wb, sizeof wb) == (ssize_t)sizeof wb && fsync(tf) == 0) {
            close(tf);
            int dc = open("/proc/sys/vm/drop_caches", O_WRONLY); if (dc >= 0) { if (write(dc, "3", 1) < 0) {} close(dc); }
            tf = open(tp, O_RDONLY);
            if (tf >= 0 && read(tf, rb, sizeof rb) == (ssize_t)sizeof rb && !memcmp(wb, rb, sizeof wb)) ok = 1;
        }
        if (tf >= 0) close(tf);
        unlink(tp);
        say("write check at the new clock: %s\n", ok ? "2 MB written and read back identical" : "FAILED");
        if (!ok) {
            FILE *c2 = fopen("/sys/kernel/debug/mmc0/clock", "w");
            if (c2) { fprintf(c2, "20000000\n"); fclose(c2); }
            int fd2 = open("/dev/mem", O_RDWR | O_SYNC);
            volatile uint32_t *w2 = fd2 >= 0 ? mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, pg) : MAP_FAILED;
            if (w2 != MAP_FAILED) { w2[((found & 4095u) / 4) + 1] = 20000000u; munmap((void *)w2, 4096); }
            if (fd2 >= 0) close(fd2);
            say("clock put back to 20 MHz\n"); cat("/sys/kernel/debug/mmc0/ios");
            return 0;
        }
    }
    if (strcmp(file, "none")) say("read after: %.2f MB/s\n", readtest(file, 12));
    say("done\n");
    return 0;
}
