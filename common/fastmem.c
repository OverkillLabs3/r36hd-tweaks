/* libr36mem.so: faster memset and memcpy for the R36HD's MIPS 74K, loaded with LD_PRELOAD before each port.
 * The 74K's "prepare for store" prefetch (pref 30) claims a 32-byte cache line without reading it from DRAM first.
 * Measured: memset 197-269 MB/s with glibc, 808-831 MB/s this way;
 * memcpy +5 to +30 %. pref 30 leaves the line's old contents undefined (zeroed), so it is only issued for lines
 * that are completely overwritten before anything reads them. Everything small, unaligned or overlapping goes to the
 * glibc function (looked up with dlsym(RTLD_NEXT)). Off switch: do not preload it. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

typedef void *(*memset_fn)(void *, int, size_t);
typedef void *(*memcpy_fn)(void *, const void *, size_t);
static memset_fn real_memset;
static memcpy_fn real_memcpy;

static void resolve(void)
{
    real_memset = (memset_fn)dlsym(RTLD_NEXT, "memset");
    real_memcpy = (memcpy_fn)dlsym(RTLD_NEXT, "memcpy");
}

#if defined(__mips__)
#define PREF_STORE(p) __asm__ volatile("pref 30, 0(%0)" :: "r"(p))
#define PREF_LOAD(p) __asm__ volatile("pref 0, 0(%0)" :: "r"(p))
#else
#define PREF_STORE(p) ((void)(p))
#define PREF_LOAD(p) ((void)(p))
#endif

void *memset(void *d, int c, size_t n)
{
    if (__builtin_expect(!real_memset, 0)) resolve();
    if (n < 256) return real_memset(d, c, n);
    uint8_t *p = d, *end = p + n;
    uint8_t *a = (uint8_t *)(((uintptr_t)p + 31) & ~(uintptr_t)31);      /* first whole line */
    uint8_t *z = (uint8_t *)((uintptr_t)end & ~(uintptr_t)31);           /* end of the last whole line */
    if (a > p) real_memset(p, c, (size_t)(a - p));
    uint32_t v = (uint8_t)c; v |= v << 8; v |= v << 16;
    for (uint32_t *w = (uint32_t *)a; (uint8_t *)w < z; w += 8) {
        PREF_STORE(w);
        w[0] = v; w[1] = v; w[2] = v; w[3] = v; w[4] = v; w[5] = v; w[6] = v; w[7] = v;
    }
    if (end > z) real_memset(z, c, (size_t)(end - z));
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    if (__builtin_expect(!real_memcpy, 0)) resolve();
    uintptr_t du = (uintptr_t)d, su = (uintptr_t)s;
    /* fast path only for large, non-overlapping, word-compatible copies */
    if (n < 4096 || ((du ^ su) & 3) || (du < su + n && su < du + n)) return real_memcpy(d, s, n);
    uint8_t *p = d, *end = p + n;
    const uint8_t *q = s;
    uint8_t *a = (uint8_t *)((du + 31) & ~(uintptr_t)31);
    uint8_t *z = (uint8_t *)((uintptr_t)end & ~(uintptr_t)31);
    size_t head = (size_t)(a - p);
    if (head) { real_memcpy(p, q, head); q += head; }
    const uint32_t *qw = (const uint32_t *)q;                               /* word aligned: same alignment mod 4 */
    for (uint32_t *w = (uint32_t *)a; (uint8_t *)w < z; w += 8, qw += 8) {
        PREF_LOAD(qw + 32);
        uint32_t x0 = qw[0], x1 = qw[1], x2 = qw[2], x3 = qw[3], x4 = qw[4], x5 = qw[5], x6 = qw[6], x7 = qw[7];
        PREF_STORE(w);
        w[0] = x0; w[1] = x1; w[2] = x2; w[3] = x3; w[4] = x4; w[5] = x5; w[6] = x6; w[7] = x7;
    }
    if (end > z) real_memcpy(z, (const uint8_t *)s + (z - p), (size_t)(end - z));
    return d;
}
