/* Second-core job: replay Vice City opaque span records (vc/libretro/vcspan_exec.h) from a ring until the main core
 * marks the epoch finished. Position independent by construction (see build.sh). Built for MIPS32 release 1: the
 * second core faults on some release 2 instructions (seh, found by bisecting the job on the device).
 *
 * All memory is reached through KSEG1 (uncached), so there is no cache protocol on this side: reads see what the main
 * core has written back, writes go straight to RAM. The main core writes back its caches before an epoch and drops
 * them after it (c2raster.h).
 *
 * Control block (arg, any KSEG0 or KSEG1 address), two 64-byte lines:
 *   line 0, written by the main core: [0] magic 0x53504356 "VCPS", [1] head (records published), [2] end (1 = no more),
 *          [3] ring (address of record 0), [4] capacity (records)
 *   line 1, written here: [16] done (records replayed), [17] finished (1 at the end), [18] cp0 cycles spent */
#include <stdint.h>
#define VCS_PTR(a) (((uintptr_t)(a) & 0x1fffffffu) | 0xa0000000u)
#define VCS_TPTR(a) ((uintptr_t)(a))
#include "../../../vc/libretro/vcspan_exec.h"

/* write back and drop the whole D-cache (Index_Writeback_Inv_D over every line, geometry from Config1) */
static inline __attribute__((always_inline)) void blast_d(void)
{
    uint32_t c1; __asm__ volatile("mfc0 %0, $16, 1" : "=r"(c1));
    uint32_t sets = 64u << ((c1 >> 13) & 7), line = 2u << ((c1 >> 10) & 7), ways = ((c1 >> 7) & 7) + 1;
    uint32_t size = sets * line * ways;
    for (uint32_t p = 0x80000000u; p < 0x80000000u + size; p += line) __asm__ volatile("cache 0x01, 0(%0)" :: "r"(p) : "memory");
    __asm__ volatile("sync" ::: "memory");
}
static inline __attribute__((always_inline)) uint32_t count0(void) { uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }

void vcspan_job(uint32_t arg)
{
    volatile uint32_t *c = (volatile uint32_t *)VCS_PTR(arg);
    uint32_t t0 = count0();
    blast_d();   // textures are read cached: drop what an earlier epoch left
    if (c[0] != 0x53504356u) return;
    const uint32_t ring = c[3], cap = c[4];
    uint32_t done = 0;
    for (;;) {
        uint32_t head = c[1], end = c[2];
        if (head > cap) head = cap;
        while (done < head) {
            vcspan_exec((const uint32_t *)VCS_PTR(ring + done * (VCS_WORDS * 4)));
            done++;
            if (!(done & 15)) c[16] = done;
        }
        if (end && done >= head) break;
    }
    c[16] = done; c[18] = count0() - t0; c[17] = 1;
}
