/* membench: glibc memset/memcpy against versions that use the 74K "prepare for store" prefetch (pref 30), which claims
 * a 32-byte cache line without reading it from DRAM first (only used on lines that are completely overwritten).
 * Also checks that the results are byte-identical. Usage: membench <report> */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* memset of 32-byte aligned, 32-byte multiple ranges with pref 30 one line ahead */
static void set32_pref(void *d, uint32_t v, size_t n)
{
    uint32_t *p = d, *e = (uint32_t *)((uint8_t *)d + n);
    while (p < e) {
        __asm__ volatile("pref 30, 0(%0)" :: "r"(p));
        p[0] = v; p[1] = v; p[2] = v; p[3] = v; p[4] = v; p[5] = v; p[6] = v; p[7] = v;
        p += 8;
    }
}
/* copy of 32-byte aligned, 32-byte multiple ranges: pref 30 on the destination, pref 0 (load) 4 lines ahead on the source */
static void cpy32_pref(void *d, const void *s, size_t n)
{
    uint32_t *p = d; const uint32_t *q = s, *e = (const uint32_t *)((const uint8_t *)s + n);
    while (q < e) {
        __asm__ volatile("pref 0, 128(%0)" :: "r"(q));
        __asm__ volatile("pref 30, 0(%0)" :: "r"(p));
        uint32_t a = q[0], b = q[1], c = q[2], dd = q[3], e0 = q[4], f = q[5], g = q[6], h = q[7];
        p[0] = a; p[1] = b; p[2] = c; p[3] = dd; p[4] = e0; p[5] = f; p[6] = g; p[7] = h;
        p += 8; q += 8;
    }
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "/mnt/sdcard/membench.txt", "w");
    if (!f) return 1;
    static const size_t sizes[] = { 64 * 1024, 300 * 1024, 2 << 20 };
    for (unsigned k = 0; k < 3; k++) {
        size_t n = sizes[k];
        uint8_t *a, *b, *c;
        if (posix_memalign((void **)&a, 64, n) || posix_memalign((void **)&b, 64, n) || posix_memalign((void **)&c, 64, n)) return 1;
        memset(a, 0x5a, n); memset(b, 0, n); memset(c, 0, n);
        int reps = (int)((64u << 20) / n);
        double t0 = now(); for (int r = 0; r < reps; r++) memset(b, r, n); double t_gs = now() - t0;
        t0 = now(); for (int r = 0; r < reps; r++) set32_pref(c, (uint32_t)r * 0x01010101u, n); double t_ps = now() - t0;
        int same_set = !memcmp(b, c, n);
        t0 = now(); for (int r = 0; r < reps; r++) memcpy(b, a, n); double t_gc = now() - t0;
        t0 = now(); for (int r = 0; r < reps; r++) cpy32_pref(c, a, n); double t_pc = now() - t0;
        int same_cpy = !memcmp(b, c, n);
        double mb = reps * (double)n / 1e6;
        fprintf(f, "%7zu KB: memset glibc %.0f MB/s, pref30 %.0f MB/s (%s) | memcpy glibc %.0f MB/s, pref %.0f MB/s (%s)\n",
                n >> 10, mb / t_gs, mb / t_ps, same_set ? "same" : "DIFFERENT", mb / t_gc, mb / t_pc, same_cpy ? "same" : "DIFFERENT");
        fflush(f);
        free(a); free(b); free(c);
    }
    fprintf(f, "done\n");
    fclose(f);
    sync();
    return 0;
}
