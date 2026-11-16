/* See mmz_heap.h. ABI of /dev/mmz recovered from the TreeFrogUI driver and measured on the device
 * open, ioctl 0xC0101901 {0,0x20,size,0}, mmap MAP_SHARED. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mmz_heap.h"
#include "bigtlb.c"

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* glibc removed __morecore in 2.34. The ship toolchain has 2.20; newer hosts just keep the normal heap. */
#if defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 34))
#define MMZ_HAVE_MORECORE 1
#endif

#define MMZ_IOC_ALLOC 0xC0101901u
#define MMZ_IOC_FREE 0xC0101902u

static unsigned char *g_base;
static size_t g_size;
static size_t g_used;
static int g_fd = -1;
static uint32_t g_req[4];
static unsigned char *g_blk;       /* whole block (the first 64 KB are scratch for the large-page payload) */
static size_t g_blk_size;
static uint32_t g_pa;            /* physical address of the block when it sits at its large-page address */
static uint32_t g_pa_real;       /* physical address of the block (0 = unknown or fake) */
static size_t g_reserve;          /* bytes kept out of malloc for the caller (mmz_heap_set_reserve) */

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...)
{
    const char *path = getenv("R36_MMZ_LOG");
    if (!path) path = "/mnt/sdcard/cubegm/logs/mmz_heap.log";
    FILE *f = fopen(path, "a");
    if (!f) return;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    __builtin_va_end(ap);
    fputc('\n', f);
    fclose(f);
}

#ifdef MMZ_HAVE_MORECORE
/* sbrk look-alike: returns the old break, grows or shrinks inside the block, NULL when full (glibc then uses mmap). */
static void *mmz_morecore(ptrdiff_t inc)
{
    unsigned char *old = g_base + g_used;
    if (inc >= 0) {
        if ((size_t)inc > g_size - g_used) return NULL;
        /* sbrk memory is zero and glibc calloc relies on it. The pool holds whatever earlier users left. */
        memset(old, 0, (size_t)inc);
        g_used += (size_t)inc;
    } else {
        size_t dec = (size_t)(-inc);
        if (dec > g_used) return NULL;
        g_used -= dec;
    }
    return old;
}
#endif

static unsigned char *grab_block(size_t bytes)
{
    if (getenv("R36_MMZ_FAKE")) {
        void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return NULL;
        if (getenv("R36_MMZ_DIRTY")) memset(p, 0xa5, bytes);   /* the real pool is not zeroed: it holds what earlier users left */
        return (unsigned char *)p;
    }
    if (g_fd < 0) {
        g_fd = open("/dev/mmz", O_RDWR | O_CLOEXEC);
        if (g_fd < 0) return NULL;
    }
    g_req[0] = 0; g_req[1] = 0x20; g_req[2] = (uint32_t)bytes; g_req[3] = 0;
    if (ioctl(g_fd, MMZ_IOC_ALLOC, g_req) < 0) return NULL;
    void *p = MAP_FAILED;
    g_pa = (g_req[3] >= 0x80000000u && g_req[3] < 0x88000000u) ? (g_req[3] & 0x1fffffffu) : 0;
    g_pa_real = g_pa;
    if (g_pa) {   /* place the block where its virtual address lines up with its physical address (large pages) */
        uintptr_t want = bigtlb_va_for(g_pa);
        p = mmap((void *)want, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
        if (p != MAP_FAILED && (uintptr_t)p != want) { munmap(p, bytes); p = MAP_FAILED; g_pa = 0; }
    }
    if (p == MAP_FAILED) p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
    if (p == MAP_FAILED) {
        ioctl(g_fd, MMZ_IOC_FREE, g_req);
        return NULL;
    }
    return (unsigned char *)p;
}

size_t mmz_heap_install(void)
{
    if (g_base) return g_size;
#ifndef MMZ_HAVE_MORECORE
    return 0;
#else
    const char *e = getenv("R36_MMZ_MB");
    long want = e ? strtol(e, NULL, 10) : 48;
    if (access("/mnt/sdcard/cubegm/mmz_off", F_OK) == 0) want = 0; /* switch: create this empty file to turn the pool off */
    if (want <= 0) { log_line("mmz_heap: off (R36_MMZ_MB=%s)", e ? e : "0"); return 0; }
    if (want > 52) want = 52;

    unsigned char *blk = NULL;
    size_t bytes = 0;
    for (long mb = want; mb >= 8; mb -= 4) {
        bytes = (size_t)mb << 20;
        blk = grab_block(bytes);
        if (blk) break;
    }
    if (!blk) {
        log_line("mmz_heap: no block granted (errno %d), normal heap stays", errno);
        if (g_fd >= 0) { close(g_fd); g_fd = -1; }
        return 0;
    }
    /* A fresh mapping is page aligned; glibc aligns the break itself. Touch nothing: the pages are ours. */
    g_blk = blk;
    g_blk_size = bytes;
    g_base = blk;
    g_size = bytes;
    g_used = 0;
    if (g_pa_real && !getenv("R36_MMZ_FAKE")) {
        /* layout: [64 KB large-page payload scratch][reserve for the caller, e.g. the second core][malloc heap] */
        size_t head = 0x10000 + ((g_reserve + 0xffffu) & ~(size_t)0xffffu);
        if (head < bytes / 2) { g_base = blk + head; g_size = bytes - head; }
        if (g_pa) {   /* large TLB pages over the block */
            size_t big = bigtlb_wire(blk, g_pa, (uintptr_t)blk, bytes);
            if (big) atexit(mmz_heap_uninstall);
            log_line("mmz_heap: large pages %zu MB of %zu MB (block pa 0x%08x va %p)", big >> 20, bytes >> 20, g_pa, (void *)blk);
        }
    }

    int o1 = mallopt(M_ARENA_MAX, 1);                    /* threads share the one arena, which uses __morecore */
    /* glibc 2.20 on 32 bit caps M_MMAP_THRESHOLD at 512 KB, so the only way to keep big blocks in the pool is no mmap
       at all. When the pool is full glibc still falls back to mmap by itself (that path ignores M_MMAP_MAX). */
    int o2 = mallopt(M_MMAP_MAX, 0);
    int o3 = mallopt(M_TRIM_THRESHOLD, 64 * 1024 * 1024); /* never shrink: shrinking and regrowing only costs time */
    int o4 = mallopt(M_TOP_PAD, 256 * 1024);
    log_line("mallopt results %d %d %d %d", o1, o2, o3, o4);
    __morecore = mmz_morecore;
    log_line("mmz_heap: %zu MB block at %p%s", bytes >> 20, (void *)blk, getenv("R36_MMZ_FAKE") ? " (fake)" : "");
    return g_size;
#endif
}

size_t mmz_heap_used(void) { return g_used; }
size_t mmz_heap_size(void) { return g_size; }

void mmz_heap_log_usage(const char *tag)
{
    if (!g_base) return;
    struct mallinfo mi = mallinfo();
    log_line("mmz_heap %s: break %zu of %zu KB, in use %d KB, mmapped %d KB", tag ? tag : "", g_used >> 10, g_size >> 10,
             mi.uordblks >> 10, mi.hblkhd >> 10);
}

void mmz_heap_uninstall(void)
{
    if (g_blk && g_pa) bigtlb_unwire(g_blk, g_pa, (uintptr_t)g_blk);
}

void mmz_heap_set_reserve(size_t bytes) { g_reserve = bytes; }

void *mmz_heap_reserved(size_t *size, uint32_t *kseg0)
{
    if (!g_blk || !g_pa_real || !g_reserve || g_base == g_blk) return NULL;
    if (size) *size = g_reserve;
    if (kseg0) *kseg0 = 0x80000000u | (g_pa_real + 0x10000u);
    return g_blk + 0x10000;
}

uint32_t mmz_heap_kseg0(const void *p)
{
    const unsigned char *c = (const unsigned char *)p;
    if (!g_blk || !g_pa_real || c < g_blk || c >= g_blk + g_blk_size) return 0;
    return 0x80000000u | (g_pa_real + (uint32_t)(c - g_blk));
}
