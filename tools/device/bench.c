/* Micro-benchmarks that run ON the handheld and print one line per test.
   Built with the same toolchain and flags as the port, so numbers reflect what the port will get. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static volatile uint32_t sink32; static volatile double sinkd; static volatile uint64_t sink64;

/* ---- dependent integer chain: one add per cycle on an in-order or out-of-order core => ~clock rate */
static double bench_clock(void)
{
    uint32_t x = 1, n = 200000000u, i;
    double t = now_s();
    for (i = 0; i < n; i++) { __asm__ volatile("" : "+r"(x)); x += i; }   /* 1 dependent add (+ loop overhead) */
    t = now_s() - t;
    sink32 = x;
    return n / t / 1e6;          /* million loop iterations per second */
}

static double bench_int_mul(void)
{
    uint32_t x = 3, n = 50000000u, i;
    double t = now_s();
    for (i = 0; i < n; i++) x = x * 2654435761u + 12345u;
    t = now_s() - t; sink32 = x;
    return n / t / 1e6;
}

static double bench_mul64(void)
{
    int64_t x = 3; int32_t a = 70001, b = 3, i, n = 20000000;
    double t = now_s();
    for (i = 0; i < n; i++) { x += ((int64_t)a * (b + i)) >> 16; }
    t = now_s() - t; sink64 = (uint64_t)x;
    return n / t / 1e6;
}

static double bench_double_add(void)
{
    double x = 0, n = 30000000;
    double t = now_s();
    long i;
    for (i = 0; i < 30000000; i++) x = x * 0.9999999 + 1.0;
    t = now_s() - t; sinkd = x;
    return n / t / 1e6;
}

static double bench_float_sqrt(void)
{
    double x = 2.0, s = 0; long i;
    double t = now_s();
    for (i = 0; i < 5000000; i++) { x += 1.0; s += sqrt(x); }
    t = now_s() - t; sinkd = s;
    return 5000000 / t / 1e6;
}

static double bench_sin(void)
{
    double x = 0.1, s = 0; long i;
    double t = now_s();
    for (i = 0; i < 2000000; i++) { x += 0.001; s += sin(x); }
    t = now_s() - t; sinkd = s;
    return 2000000 / t / 1e6;
}

static double bench_memcpy(size_t mb)
{
    size_t n = mb << 20; char *a = malloc(n), *b = malloc(n); int r; double t;
    memset(a, 1, n); memset(b, 2, n);
    t = now_s();
    for (r = 0; r < 8; r++) memcpy(b, a, n);
    t = now_s() - t;
    sink32 = b[n / 2]; free(a); free(b);
    return 8.0 * mb / t;                      /* MB/s */
}

static double bench_read(size_t mb)
{
    size_t n = (mb << 20) / 4, i; uint32_t *a = malloc(n * 4), s = 0; int r; double t;
    for (i = 0; i < n; i++) a[i] = (uint32_t)i;
    t = now_s();
    for (r = 0; r < 8; r++) for (i = 0; i < n; i++) s += a[i];
    t = now_s() - t; sink32 = s; free(a);
    return 8.0 * mb / t;
}

/* pointer chase: ns per dependent load for a working set of kb kilobytes */
static double bench_latency(size_t kb)
{
    size_t n = (kb << 10) / sizeof(void *), i, j, k; void **a = malloc(n * sizeof(void *)); size_t *perm = malloc(n * sizeof(size_t)); void **p; long steps = 4000000, s; double t;
    for (i = 0; i < n; i++) perm[i] = i;
    for (i = n - 1; i > 0; i--) { j = (size_t)(((uint64_t)rand() * 40503u + i) % (i + 1)); k = perm[i]; perm[i] = perm[j]; perm[j] = k; }
    for (i = 0; i < n; i++) a[perm[i]] = &a[perm[(i + 1) % n]];
    p = (void **)a[0];
    t = now_s();
    for (s = 0; s < steps; s++) p = (void **)*p;
    t = now_s() - t; sink64 = (uint64_t)(uintptr_t)p; free(a); free(perm);
    return t / steps * 1e9;
}

/* the port's real inner loops */
static inline uint32_t mul255(uint32_t lanes, uint32_t b) { uint32_t t = lanes * b + 0x00800080u; t = (t + ((t >> 8) & 0x00FF00FFu)) >> 8; return t & 0x00FF00FFu; }
static double bench_blend_pixels(void)    /* million alpha-blended pixels per second (SWAR) */
{
    int n = 640 * 480; uint32_t *d = malloc(n * 4); uint32_t src = 0x80306090u; int r, i; double t;
    for (i = 0; i < n; i++) d[i] = 0xFF102030u + i;
    t = now_s();
    for (r = 0; r < 20; r++) {
        uint32_t sa = src >> 24, ia = 255 - sa, rbs = mul255(src & 0x00FF00FFu, sa);
        for (i = 0; i < n; i++) {
            uint32_t v = d[i];
            uint32_t rb = (mul255(v & 0x00FF00FFu, ia) + rbs) & 0x00FF00FFu;
            uint32_t g = ((((v >> 8) & 0xFF) * ia + 128) >> 8) + 0x40;
            d[i] = rb | ((g & 0xFF) << 8) | 0xFF000000u;
        }
    }
    t = now_s() - t; sink32 = d[100]; free(d);
    return 20.0 * n / t / 1e6;
}

static int32_t sin_tab[4096];
static double bench_paint_cells(void)     /* the background shader's inner loop: cells per second (thousands) */
{
    int i, c, cells = 4800, r; double t; int32_t sum = 0;
    for (i = 0; i < 4096; i++) sin_tab[i] = (int32_t)lrint(sin(2 * M_PI * i / 4096) * 65536.0);
    t = now_s();
    for (r = 0; r < 10; r++)
        for (c = 0; c < cells; c++) {
            int32_t x = (c * 977) & 0x1FFFFF, y = (c * 619) & 0x1FFFFF, x2 = x + y, y2 = x + y;
            for (i = 0; i < 5; i++) {
                int32_t m = x > y ? x : y;
                int32_t sm = sin_tab[((uint32_t)(((int64_t)m * 10430) >> 16) >> 4) & 4095];
                int32_t cs, sn;
                x2 += sm + x; y2 += sm + y;
                x += sin_tab[((uint32_t)(((int64_t)(x2 * 3) * 10430) >> 16) >> 4 & 4095) + 1024 & 4095] >> 1;
                y += sin_tab[((uint32_t)(((int64_t)(y2 - 7) * 10430) >> 16) >> 4) & 4095] >> 1;
                cs = sin_tab[(((uint32_t)(((int64_t)(x + y) * 10430) >> 16) >> 4) + 1024) & 4095]; sn = sin_tab[((uint32_t)(((int64_t)(x - y) * 10430) >> 16) >> 4) & 4095];
                x -= cs - sn; y -= cs - sn;
            }
            sum += x ^ y;
        }
    t = now_s() - t; sink32 = (uint32_t)sum;
    return 10.0 * cells / t / 1e3;
}

/* ---- thread scaling */
static void *spin(void *arg)
{
    uint32_t x = 1, i; (void)arg;
    for (i = 0; i < 60000000u; i++) x = x * 1664525u + 1013904223u;
    sink32 = x; return 0;
}
static void thread_scaling(void)
{
    pthread_t th[2]; double t1, t2; int i;
    t1 = now_s(); spin(0); t1 = now_s() - t1;
    t2 = now_s();
    for (i = 0; i < 2; i++) pthread_create(&th[i], 0, spin, 0);
    for (i = 0; i < 2; i++) pthread_join(th[i], 0);
    t2 = now_s() - t2;
    printf("threads: 1 job %.2f s, 2 jobs in parallel %.2f s -> %s (ratio %.2f; ~1.0 means two real cores, ~2.0 means one core)\n",
           t1, t2, t2 < t1 * 1.4 ? "TWO CORES USABLE" : "ONE core effective", t2 / t1);
}

int main(int argc, char **argv)
{
    if (argc > 1) {   /* single kernel, for the device timing model */
        if (!strcmp(argv[1], "add")) printf("%.1f\n", bench_clock());
        else if (!strcmp(argv[1], "blend")) printf("%.1f\n", bench_blend_pixels());
        else if (!strcmp(argv[1], "memcpy")) printf("%.1f\n", bench_memcpy(8));
        else if (!strcmp(argv[1], "latency8m")) printf("%.1f\n", bench_latency(8192));
        else if (!strcmp(argv[1], "latency64k")) printf("%.1f\n", bench_latency(64));
        else if (!strcmp(argv[1], "latency256k")) printf("%.1f\n", bench_latency(256));
        else if (!strcmp(argv[1], "latency16k")) printf("%.1f\n", bench_latency(16));
        else if (!strcmp(argv[1], "mul")) printf("%.1f\n", bench_int_mul());
        else if (!strcmp(argv[1], "mul64")) printf("%.1f\n", bench_mul64());
        else if (!strcmp(argv[1], "dadd")) printf("%.1f\n", bench_double_add());
        else if (!strcmp(argv[1], "read16")) printf("%.1f\n", bench_read(16));
        else if (!strcmp(argv[1], "memcpy1")) printf("%.1f\n", bench_memcpy(1));
        return 0;
    }
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    printf("online cpus (sysconf): %ld\n", ncpu);
    fflush(stdout);
    printf("dependent add loop: %.0f M iterations/s  (~ CPU clock in MHz if 1 cycle/iter)\n", bench_clock()); fflush(stdout);
    printf("int32 multiply chain: %.1f M/s\n", bench_int_mul()); fflush(stdout);
    printf("int64 multiply-shift (fixed-point mul): %.1f M/s\n", bench_mul64()); fflush(stdout);
    printf("double multiply-add chain: %.1f M/s\n", bench_double_add()); fflush(stdout);
    printf("double sqrt: %.2f M/s\n", bench_float_sqrt()); fflush(stdout);
    printf("double sin (libm): %.2f M/s\n", bench_sin()); fflush(stdout);
    printf("memcpy 1 MB: %.0f MB/s, 8 MB: %.0f MB/s\n", bench_memcpy(1), bench_memcpy(8)); fflush(stdout);
    printf("sequential read 16 MB: %.0f MB/s\n", bench_read(16)); fflush(stdout);
    printf("memory latency (ns per dependent load): 16KB %.1f | 64KB %.1f | 256KB %.1f | 1MB %.1f | 8MB %.1f\n",
           bench_latency(16), bench_latency(64), bench_latency(256), bench_latency(1024), bench_latency(8192)); fflush(stdout);
    printf("PORT KERNEL alpha blend 640x480: %.1f M pixels/s  (=> %.1f full-screen layers per second)\n", bench_blend_pixels(), bench_blend_pixels() * 1e6 / (640 * 480)); fflush(stdout);
    thread_scaling();
    return 0;
}
