/* c2bench: start the second-core worker, measure job latency and throughput, run Linux and the second core in parallel.
 * Usage: c2bench <report> <dir with boot.bin worker.bin jobs.bin> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "../../../common/core2.h"

struct c2_test { uint32_t op, buf, words, result, pad[4]; };

static FILE *rep;
static void say(const char *f, ...)
{
    va_list ap; va_start(ap, f); vfprintf(rep, f, ap); va_end(ap);
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    fflush(rep); fsync(fileno(rep));
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
    rep = fopen(argc > 1 ? argv[1] : "/mnt/sdcard/c2bench_report.txt", "w");
    const char *dir = argc > 2 ? argv[2] : ".";
    if (!rep) return 1;
    say("c2bench\n");
    double t0 = now();
    int r = core2_start(dir);
    say("core2_start = %d in %.2f s\n%s", r, now() - t0, core2_log());
    say("second core console: [%s]\n", core2_console());
    if (r) { say("stop\n"); return 0; }
    static uint8_t jb[8192];
    char p[512]; snprintf(p, sizeof p, "%s/jobs.bin", dir);
    FILE *f = fopen(p, "rb"); size_t jn = f ? fread(jb, 1, sizeof jb, f) : 0; if (f) fclose(f);
    uint32_t fn = core2_load(jb, jn);
    struct c2_test *t = core2_alloc(sizeof *t);
    uint32_t words = 1u << 20;                         /* 4 MB */
    uint32_t *buf = core2_alloc(words * 4);
    say("jobs.bin %zu bytes at 0x%08x, test block 0x%08x, buffer 0x%08x\n", jn, fn, core2_addr(t), core2_addr(buf));

    /* 1. latency of empty jobs */
    t->op = 0; core2_writeback(t, sizeof *t);
    int fails = 0; double a = now();
    for (int i = 0; i < 1000; i++) { if (core2_run(fn, core2_addr(t)) || core2_wait(500)) { fails++; break; } }
    double b = now();
    say("1000 empty jobs: %.1f us per round trip (%d failures)\n", (b - a) * 1e6 / 1000, fails);
    if (fails) goto end;

    /* 2. second core fills 4 MB, Linux checks */
    t->op = 1; t->buf = core2_addr(buf); t->words = words; core2_writeback(t, sizeof *t);
    a = now(); core2_run(fn, core2_addr(t)); r = core2_wait(5000); b = now();
    core2_invalidate(buf, words * 4);
    uint32_t bad = 0; for (uint32_t i = 0; i < words; i++) if (buf[i] != i * 2654435761u) bad++;
    say("second core fill 4 MB: %.1f ms (%.0f MB/s), wait %d, %u wrong words\n", (b - a) * 1e3, 4.0 / (b - a), r, bad);

    /* 3. add loop alone on each core */
    uint32_t n = 100000000u;
    t->op = 3; t->words = n; core2_writeback(t, sizeof *t);
    a = now(); core2_run(fn, core2_addr(t)); core2_wait(10000); b = now();
    double c2t = b - a;
    a = now(); { volatile uint32_t x = 1; for (uint32_t i = 0; i < n; i++) x += i; } b = now();
    double c1t = b - a;
    say("100 M adds: second core %.3f s, Linux core %.3f s (Linux loop has a volatile store, so it is slower per add)\n", c2t, c1t);

    /* 4. both at once */
    t->op = 3; t->words = n; core2_writeback(t, sizeof *t);
    a = now(); core2_run(fn, core2_addr(t));
    { volatile uint32_t x = 1; for (uint32_t i = 0; i < n; i++) x += i; }
    double lin = now() - a; core2_wait(10000); b = now();
    say("both cores at once: Linux part %.3f s, both done after %.3f s (alone: %.3f + %.3f)\n", lin, b - a, c1t, c2t);
end:
    core2_stop();
    say("second core console: [%s]\n", core2_console());
    say("stopped. done\n");
    return 0;
}
