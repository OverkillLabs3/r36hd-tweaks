/* sdclk: how fast can the SD card be read on this console?
 * The kernel limits the card clock to 20 MHz (device tree property clock-freq-min-max = 100 kHz..20 MHz, same in the
 * stock firmware). The card reports "high speed", which allows 50 MHz. This probe reads the dw_mmc clock registers,
 * reads a big file at the current clock, switches the controller divider to a faster clock for the read only, reads the
 * same file again, compares a checksum, and puts the divider back. Nothing is written to the card.
 *
 * Safety: a forked watchdog restores the original divider after 20 s no matter what the main process does, so a card
 * that cannot take the fast clock does not stay on it. The probe skips the test if the controller looks busy or the
 * registers do not look like the expected dw_mmc layout.
 *
 * dw_mmc (Synopsys DesignWare mshc) registers: CTRL 0x00, CLKDIV 0x08, CLKSRC 0x0c, CLKENA 0x10, CMDARG 0x28,
 * CMD 0x2c, STATUS 0x48. Card clock = bus_hz / (2 * CLKDIV); bus_hz is 198 MHz here (kernel log "host clk=198000000").
 * Usage: sdclk <report> <testfile> [mb]
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define BASE 0x1884c000u
#define R_CTRL 0x00
#define R_CLKDIV 0x08
#define R_CLKSRC 0x0c
#define R_CLKENA 0x10
#define R_CMDARG 0x28
#define R_CMD 0x2c
#define R_STATUS 0x48
#define CMD_START (1u << 31)
#define CMD_UPD_CLK (1u << 21)
#define CMD_PRV_DAT_WAIT (1u << 13)
#define BUS_HZ 198000000.0

static volatile uint32_t *reg;
static FILE *rep;
static void say(const char *f, ...)
{
    va_list ap; va_start(ap, f); vfprintf(rep, f, ap); va_end(ap); va_start(ap, f); vprintf(f, ap); va_end(ap);
    fflush(rep); fsync(fileno(rep));
}

static int update_clock(void)
{
    reg[R_CMDARG / 4] = 0;
    __sync_synchronize();
    reg[R_CMD / 4] = CMD_START | CMD_UPD_CLK | CMD_PRV_DAT_WAIT;
    for (int i = 0; i < 2000000; i++) {
        if (!(reg[R_CMD / 4] & CMD_START)) return 0;
    }
    return -1;
}

static int set_div(uint32_t div)
{
    uint32_t ena = reg[R_CLKENA / 4];
    reg[R_CLKENA / 4] = 0;
    if (update_clock()) return -1;
    reg[R_CLKDIV / 4] = div;
    if (update_clock()) return -2;
    reg[R_CLKENA / 4] = ena ? ena : (1u | (1u << 16));
    if (update_clock()) return -3;
    return 0;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* read mb megabytes from the start of the file with a cold cache, return seconds, fill checksum */
static double read_test(const char *path, int mb, uint32_t *sum, long *bytes)
{
    static char buf[256 * 1024];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    int dc = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (dc >= 0) { if (write(dc, "3", 1) < 0) {} close(dc); }
    uint32_t s = 0; long tot = 0, want = (long)mb * 1024 * 1024;
    double t0 = now();
    while (tot < want) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i += 64) s = s * 31 + (uint8_t)buf[i];
        tot += n;
    }
    double t1 = now();
    close(fd);
    *sum = s; *bytes = tot;
    return t1 - t0;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/mnt/sdcard/sdclk_report.txt";
    const char *file = argc > 2 ? argv[2] : "/mnt/sdcard/cubegm/UI_Res.cpd";
    int mb = argc > 3 ? atoi(argv[3]) : 12;
    rep = fopen(out, "w");
    if (!rep) return 1;
    say("sdclk probe\n");
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { say("cannot open /dev/mem\n"); return 0; }
    reg = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, BASE);
    if (reg == MAP_FAILED) { say("mmap failed\n"); return 0; }
    uint32_t div0 = reg[R_CLKDIV / 4] & 0xff, ena0 = reg[R_CLKENA / 4], src0 = reg[R_CLKSRC / 4], st = reg[R_STATUS / 4];
    say("CLKDIV=0x%08x CLKENA=0x%08x CLKSRC=0x%08x STATUS=0x%08x CTRL=0x%08x\n", reg[R_CLKDIV / 4], ena0, src0, st, reg[R_CTRL / 4]);
    if (div0 == 0 || div0 > 20) { say("unexpected divider %u, stop\n", div0); return 0; }
    say("current card clock: %.2f MHz (divider %u, bus %.0f MHz)\n", BUS_HZ / (2.0 * div0) / 1e6, div0, BUS_HZ / 1e6);
    if ((st & (1u << 9)) || (st & (1u << 10))) { say("controller busy, stop\n"); return 0; }

    uint32_t sum0, sum1, sum2; long b0, b1, b2;
    double t0 = read_test(file, mb, &sum0, &b0);
    if (t0 < 0) { say("cannot read %s\n", file); return 0; }
    say("pass 1 (current clock): %ld bytes in %.3f s = %.2f MB/s (checksum %08x)\n", b0, t0, b0 / t0 / 1e6, sum0);

    /* watchdog: restore the original divider after 20 s whatever happens */
    pid_t wd = fork();
    if (wd == 0) {
        sleep(20);
        int f2 = open("/dev/mem", O_RDWR | O_SYNC);
        volatile uint32_t *r2 = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, f2, BASE);
        reg = r2;
        if (((reg[R_CLKDIV / 4]) & 0xff) != div0) set_div(div0);
        _exit(0);
    }

    static const uint32_t tries[] = { 3, 2 };    /* 33 MHz, 49.5 MHz */
    for (unsigned k = 0; k < sizeof tries / sizeof *tries; k++) {
        uint32_t d = tries[k];
        if (d >= div0) continue;
        sync();
        int r = set_div(d);
        say("set divider %u (%.2f MHz): %d, CLKDIV now 0x%08x\n", d, BUS_HZ / (2.0 * d) / 1e6, r, reg[R_CLKDIV / 4]);
        if (r) { set_div(div0); say("switch failed, restored\n"); break; }
        double t = read_test(file, mb, &sum1, &b1);
        say("pass at %.2f MHz: %ld bytes in %.3f s = %.2f MB/s (checksum %08x, %s)\n", BUS_HZ / (2.0 * d) / 1e6, b1, t, b1 / t / 1e6, sum1, sum1 == sum0 ? "same data" : "DATA DIFFERS");
        set_div(div0);
        say("restored divider %u, CLKDIV now 0x%08x\n", div0, reg[R_CLKDIV / 4]);
        if (sum1 != sum0) { say("stopping: the faster clock gave different data\n"); break; }
        double t3 = read_test(file, mb, &sum2, &b2);
        say("pass back at the original clock: %.2f MB/s (checksum %08x)\n", b2 / t3 / 1e6, sum2);
    }
    kill(wd, SIGKILL); waitpid(wd, NULL, 0);
    say("final CLKDIV 0x%08x\ndone\n", reg[R_CLKDIV / 4]);
    return 0;
}
