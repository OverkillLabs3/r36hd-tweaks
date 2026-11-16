/* Entry into the second core: a 4-instruction jump written over the start of the firmware's "mv" shell handler.
 * The firmware copies its shell command table at start-up, so changing the table does nothing (run 10: "pwd" still ran
 * the stock handler); changing the handler's code does. The code is written from Linux through /dev/mem at the
 * physical address, which Linux maps uncached (it is outside Linux RAM), so it goes straight to RAM. The handler's
 * 32-byte line holds only handler code and is never executed unless someone types "mv", so the second core's
 * instruction cache has no stale copy. The 4 original words are checked before and put back after.
 * Runtime only: a power cycle reloads the firmware. */
#ifndef C2_TRAMP_H
#define C2_TRAMP_H
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define C2_MV_PHYS 0x03e503a0u               /* second-core address 0x83e503a0 */
static const uint32_t c2_mv_orig[4] = { 0x3c028403u, 0x00a03025u, 0x00802825u, 0x3c048402u };

static volatile uint32_t *c2_tramp_map(int *fd_out, volatile uint32_t **page)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return NULL;
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, C2_MV_PHYS & ~4095u);
    if (p == MAP_FAILED) { close(fd); return NULL; }
    *fd_out = fd; *page = p;
    return (volatile uint32_t *)((volatile uint8_t *)p + (C2_MV_PHYS & 4095u));
}
static void c2_tramp_unmap(int fd, volatile uint32_t *page) { munmap((void *)page, 4096); close(fd); }

/* 0 = installed; -1 = /dev/mem failed; -2 = unexpected firmware (nothing written) */
static int c2_tramp_install(uint32_t target)
{
    int fd; volatile uint32_t *pg, *w = c2_tramp_map(&fd, &pg);
    if (!w) return -1;
    for (int i = 0; i < 4; i++) if (w[i] != c2_mv_orig[i]) { c2_tramp_unmap(fd, pg); return -2; }
    uint32_t hi = target >> 16, lo = target & 0xffffu;
    w[3] = 0x00000000u;                          /* nop (delay slot) */
    w[2] = 0x03200008u;                          /* jr $t9 */
    w[1] = 0x37390000u | lo;                     /* ori $t9, $t9, lo */
    w[0] = 0x3c190000u | hi;                     /* lui $t9, hi      (written last: the jump is complete before it is live) */
    __sync_synchronize();
    c2_tramp_unmap(fd, pg);
    return 0;
}
/* 0 = original code back */
static int c2_tramp_remove(void)
{
    int fd; volatile uint32_t *pg, *w = c2_tramp_map(&fd, &pg);
    if (!w) return -1;
    w[0] = c2_mv_orig[0];                        /* first word first: the jump is dead before the rest changes */
    __sync_synchronize();
    for (int i = 1; i < 4; i++) w[i] = c2_mv_orig[i];
    __sync_synchronize();
    int ok = 1;
    for (int i = 0; i < 4; i++) if (w[i] != c2_mv_orig[i]) ok = 0;
    c2_tramp_unmap(fd, pg);
    return ok ? 0 : -1;
}
#endif
