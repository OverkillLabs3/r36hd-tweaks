/* bigtlb: map a game's mmz pool with large TLB pages (wired entries) on the R36HD's MIPS 74K.
 * Measured on the device: a 24 MB pointer chase 399 -> 219 ns per
 * load with 4 MB pages instead of 4 KB pages (the 32-entry TLB only reaches 256 KB with 4 KB pages).
 *
 * How: the pool is mapped at VA = 0x40000000 + (PA mod 8 MB), so VA and PA line up for pages up to 4 MB. Up to
 * BIGTLB_MAX global wired entries cover the pool with 8 MB, 2 MB or 512 KB page pairs (largest that fits, aligned).
 * The entries are written by a small kernel-mode payload (common/tlbwire_bin.h, source tweaks/tlbwire.S),
 * run once through the virtuart driver's poll pointer (kernel fops at physical 0x0039b1b8, slot +28, stock 0x802bc8f4),
 * which is put back immediately. The payload lives in the first 4 KB of the pool (kernel address 0x80000000 | PA).
 * The entries are global (G bit), so they are removed at deinit, and the launcher runs `tlbclear` before every game,
 * so a crashed game cannot leave a stale mapping. Off switches: R36_BIGTLB=0 or the file cubegm/bigtlb_off.
 */
#ifndef BIGTLB_C
#define BIGTLB_C
#if defined(__mips__)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/cachectl.h>
#include "tlbwire_bin.h"

#define BIGTLB_VA_BASE 0x40000000u
#define BIGTLB_MAX 6
#define BIGTLB_FOPS_POLL_PHYS (0x0039b1b8u + 7 * 4)
#define BIGTLB_POLL_STOCK 0x802bc8f4u
#define BIGTLB_PARAM 0x8200u                      /* parameter block offset from the payload */

static int bigtlb_wired_base = -1, bigtlb_n;

/* va for a block at physical pa: same offset modulo 8 MB */
static inline uintptr_t bigtlb_va_for(uint32_t pa) { return BIGTLB_VA_BASE + (pa & 0x7fffffu); }

/* run the payload placed at `pl` (user pointer into the pool, kernel address kpl) once in kernel mode */
static int bigtlb_call(uint8_t *pl, uint32_t kpl)
{
    int mfd = open("/dev/mem", O_RDWR);
    if (mfd < 0) return -1;
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, BIGTLB_FOPS_POLL_PHYS & ~4095u);
    close(mfd);
    if (m == MAP_FAILED) return -1;
    volatile uint32_t *slot = (volatile uint32_t *)((uint8_t *)m + (BIGTLB_FOPS_POLL_PHYS & 4095u));
    int rc = -2;
    if (*slot == BIGTLB_POLL_STOCK) {
        cacheflush(pl, BIGTLB_PARAM + 0x100, BCACHE);
        int vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
        if (vu >= 0) {
            *slot = kpl; __sync_synchronize();
            struct pollfd pf = { vu, POLLIN, 0 };
            poll(&pf, 1, 0);
            *slot = BIGTLB_POLL_STOCK; __sync_synchronize();
            close(vu);
            cacheflush(pl, BIGTLB_PARAM + 0x100, BCACHE);
            rc = *slot == BIGTLB_POLL_STOCK ? 0 : -3;
        }
    }
    munmap(m, 4096);
    return rc;
}

/* wire [va, va + size) mapped to [pa, ...); scratch = first 64 KB of the pool (payload + parameters).
   Returns the number of bytes covered by large pages. */
static __attribute__((unused)) size_t bigtlb_wire(uint8_t *scratch, uint32_t pa, uintptr_t va, size_t size)
{
    if (getenv("R36_BIGTLB") && !strcmp(getenv("R36_BIGTLB"), "0")) return 0;
    if (access("/mnt/sdcard/cubegm/bigtlb_off", F_OK) == 0) return 0;
    uint32_t kpl = 0x80000000u | (pa + (uint32_t)(scratch - (uint8_t *)va));
    memcpy(scratch, tlbwire_bin, sizeof tlbwire_bin);
    volatile uint32_t *pb = (volatile uint32_t *)(scratch + BIGTLB_PARAM);
    memset((void *)pb, 0, 0x100);
    static const uint32_t span[3] = { 0x800000u, 0x200000u, 0x80000u };
    static const uint32_t mask[3] = { 0x007fe000u, 0x001fe000u, 0x0007e000u };
    uintptr_t cur = va, end = va + size;
    size_t covered = 0;
    uint32_t n = 0;
    while (cur < end && n < BIGTLB_MAX) {
        int k = 0;
        while (k < 3 && ((cur & (span[k] - 1)) || cur + span[k] > end)) k++;
        if (k == 3) { cur = (cur + 0x7ffffu) & ~(uintptr_t)0x7ffffu; continue; }
        uint32_t p = pa + (uint32_t)(cur - va), half = span[k] / 2;
        pb[4 + 4 * n] = (uint32_t)cur;
        pb[5 + 4 * n] = ((p >> 12) << 6) | (3u << 3) | 7u;            /* C=3 cached, D, V, G */
        pb[6 + 4 * n] = (((p + half) >> 12) << 6) | (3u << 3) | 7u;
        pb[7 + 4 * n] = mask[k];
        covered += span[k];
        cur += span[k];
        n++;
    }
    if (!n) return 0;
    pb[0] = 1; pb[1] = n;
    if (bigtlb_call(scratch, kpl) || pb[0xC8 / 4] != 0x424C5457u) return 0;
    bigtlb_wired_base = (int)pb[0xC0 / 4];
    bigtlb_n = (int)n;
    return covered;
}

/* remove our entries (scratch must still be mapped) */
static __attribute__((unused)) void bigtlb_unwire(uint8_t *scratch, uint32_t pa, uintptr_t va)
{
    if (bigtlb_wired_base < 0) return;
    uint32_t kpl = 0x80000000u | (pa + (uint32_t)(scratch - (uint8_t *)va));
    memcpy(scratch, tlbwire_bin, sizeof tlbwire_bin);
    volatile uint32_t *pb = (volatile uint32_t *)(scratch + BIGTLB_PARAM);
    memset((void *)pb, 0, 0x100);
    pb[0] = 2; pb[1] = (uint32_t)bigtlb_n; pb[3] = (uint32_t)bigtlb_wired_base;
    bigtlb_call(scratch, kpl);
    bigtlb_wired_base = -1;
}
#else   /* other hosts: no large pages */
#include <stdint.h>
#include <stddef.h>
#define BIGTLB_VA_BASE 0x40000000u
static inline uintptr_t bigtlb_va_for(uint32_t pa) { return BIGTLB_VA_BASE + (pa & 0x7fffffu); }
static size_t bigtlb_wire(uint8_t *s, uint32_t pa, uintptr_t va, size_t size) { (void)s; (void)pa; (void)va; (void)size; return 0; }
static void bigtlb_unwire(uint8_t *s, uint32_t pa, uintptr_t va) { (void)s; (void)pa; (void)va; }
#endif
#endif
