/* cpusoak: stability soak for an overclocked CPU. Runs integer, 64-bit multiply, double-precision, memory-copy and
 * hash work in a loop for N minutes. Every round must give the same result as the first round and the first round is result
 * compared in the first round with the PC result (EXPECT_* below, from QEMU). A CPU that is clocked too high usually returns wrong results before it
 * crashes, so a mismatch is logged, flushed to the card and the program goes on. Progress is written every 10 s.
 * usage: cpusoak [minutes] [logfile]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#ifndef EXPECT_A
#define EXPECT_A 0u
#define EXPECT_B 0u
#define EXPECT_C 0u
#define EXPECT_D 0u
#define EXPECT_E 0u
#endif

static volatile uint32_t *hwt;
static double hw_ms(void)
{
    uint32_t s1, s2; uint16_t ms;
    do { s1 = *(volatile uint32_t *)((volatile uint8_t *)hwt + 0xa0c); ms = *(volatile uint16_t *)((volatile uint8_t *)hwt + 0xa0a); s2 = *(volatile uint32_t *)((volatile uint8_t *)hwt + 0xa0c); } while (s1 != s2);
    return s1 * 1000.0 + ms;
}

static uint32_t work_int(void)
{
    uint32_t x = 1, y = 3;
    for (int i = 0; i < 2000000; i++) { x += y; y ^= x; x = (x << 3) | (x >> 29); x += i; }
    return x ^ y;
}
static uint32_t work_mul64(void)
{
    uint64_t a = 0x123456789abcdefULL;
    for (int i = 0; i < 600000; i++) { a = a * 6364136223846793005ULL + 1442695040888963407ULL; a ^= a >> 29; }
    return (uint32_t)(a ^ (a >> 32));
}
static uint32_t work_fp(void)
{
    double s = 0.5, t = 1.0;
    for (int i = 1; i < 400000; i++) { t = t * 1.0000001 + 1.0 / (double)i; s = s * 0.9999999 + t * 1e-9; if (t > 1e6) t *= 1e-6; }
    union { double d; uint32_t w[2]; } u; u.d = s + t;
    return u.w[0] ^ u.w[1];
}
static uint32_t work_mem(uint32_t *a, uint32_t *b, size_t n)
{
    uint32_t h = 0;
    for (size_t i = 0; i < n; i++) a[i] = (uint32_t)(i * 2654435761u);
    memcpy(b, a, n * 4);
    for (size_t i = 0; i < n; i++) h = h * 31 + b[i];
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) h ^= 0xdead0000u;
    return h;
}
static uint32_t work_hash(void)
{
    uint32_t h[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int r = 0; r < 60000; r++) {
        for (int i = 0; i < 8; i++) { h[i] += h[(i + 1) & 7] ^ (h[(i + 5) & 7] >> 3); h[i] = (h[i] << 7) | (h[i] >> 25); }
    }
    return h[0] ^ h[3] ^ h[5] ^ h[7];
}

static void logf_(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void logf_(FILE *f, const char *fmt, ...)
{
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    vfprintf(f, fmt, ap); __builtin_va_end(ap);
    fflush(f); fsync(fileno(f));
}

static int read_int(const char *path)
{
    FILE *f = fopen(path, "r"); int v = -1;
    if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
    return v;
}

int main(int argc, char **argv)
{
    double minutes = argc > 1 ? atof(argv[1]) : 10.0;
    const char *logp = argc > 2 ? argv[2] : "/mnt/sdcard/cpusoak_report.txt";
    FILE *log = fopen(logp, "w");
    if (!log) log = stdout;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    hwt = fd >= 0 ? mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x18818000) : MAP_FAILED;
    volatile uint32_t *clk = fd >= 0 ? mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x18800000) : MAP_FAILED;
    int have_t = hwt != MAP_FAILED;
    size_t n = 256 * 1024;                       /* 1 MB per buffer: larger than the 32 KB cache, small in RAM */
    uint32_t *a = malloc(n * 4), *b = malloc(n * 4);
    if (!a || !b) { logf_(log, "no memory\n"); return 1; }

    if (clk != MAP_FAILED) {
        uint32_t r74 = clk[0x74 / 4], r380 = clk[0x380 / 4];
        logf_(log, "cpu mux %u, PLL divider %u -> PLL ~ %.1f MHz\n", (r74 >> 8) & 7, (r380 >> 16) & 0x7fff, ((r380 >> 16) & 0x7fff) * 2.4 + 2.4);
    }
    logf_(log, "soak for %.1f minutes, report every 10 s (timer: %s)\n", minutes, have_t ? "24 MHz hardware" : "none");

    uint32_t ref[5] = { 0 };
    int have_ref = 0, bad_rounds = 0, rounds = 0, bat = -1;
    double start = have_t ? hw_ms() : 0, last = start, tmin = 1e18, tmax = 0;
    struct timespec ts0; clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (;;) {
        double r0 = have_t ? hw_ms() : 0;
        uint32_t v[5] = { work_int(), work_mul64(), work_fp(), work_mem(a, b, n), work_hash() };
        double dt = have_t ? hw_ms() - r0 : 0;
        if (dt < tmin) tmin = dt;
        if (dt > tmax) tmax = dt;
        rounds++;
        if (!have_ref) { memcpy(ref, v, sizeof ref); have_ref = 1;
            logf_(log, "first round: int %08x mul64 %08x fp %08x mem %08x hash %08x\n", v[0], v[1], v[2], v[3], v[4]);
            if (EXPECT_A || EXPECT_B)
                logf_(log, "first round vs the PC result: int %s mul64 %s fp %s mem %s hash %s\n", v[0] == EXPECT_A ? "same" : "DIFFERENT", v[1] == EXPECT_B ? "same" : "DIFFERENT",
                      v[2] == EXPECT_C ? "same" : "DIFFERENT", v[3] == EXPECT_D ? "same" : "DIFFERENT", v[4] == EXPECT_E ? "same" : "DIFFERENT"); }
        int bad = memcmp(ref, v, sizeof ref) != 0;
        if (bad) { bad_rounds++;
            logf_(log, "WRONG RESULT in round %d: int %08x mul64 %08x fp %08x mem %08x hash %08x\n", rounds, v[0], v[1], v[2], v[3], v[4]); }
        double now = have_t ? hw_ms() : 0;
        struct timespec ts1; clock_gettime(CLOCK_MONOTONIC, &ts1);
        double elapsed_s = have_t ? (now - start) / 1000.0 : (ts1.tv_sec - ts0.tv_sec);
        if (now - last >= 10000.0 || !have_t) {
            last = now;
            if (bat < 0) bat = read_int("/sys/class/power_supply/battery/voltage_now");
            logf_(log, "t=%5.0f s rounds=%d bad=%d round ms min %.0f max %.0f battery_uV %d\n", elapsed_s, rounds, bad_rounds, tmin, tmax,
                  read_int("/sys/class/power_supply/battery/voltage_now"));
            tmin = 1e18; tmax = 0;
        }
        if (elapsed_s >= minutes * 60.0) break;
    }
    logf_(log, "finished: %d rounds, %d wrong. %s\n", rounds, bad_rounds, bad_rounds ? "UNSTABLE at this clock: lower it" : "no errors");
    return 0;
}
