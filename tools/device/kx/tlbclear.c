/* tlbclear: remove every wired TLB entry (Wired = 0, entries 0..7 invalidated). Run before each game by
 * the launcher, so a game that crashed without removing its large-page entries cannot leave a global mapping behind.
 * The kernel itself uses no wired entries (Wired reads 0 at boot).
 * Usage: tlbclear [log]     exit 0 always */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "../../../common/bigtlb.c"

int main(int argc, char **argv)
{
    FILE *lf = argc > 1 ? fopen(argv[1], "a") : NULL;
    int fd = open("/dev/mmz", O_RDWR);
    uint32_t rq[4] = { 0, 0x20, 64 * 1024, 0 };
    if (fd < 0 || ioctl(fd, 0xC0101901u, rq) < 0 || rq[3] < 0x80000000u) { if (lf) fprintf(lf, "tlbclear: no mmz block\n"); return 0; }
    uint8_t *b = mmap(NULL, 64 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b == MAP_FAILED) { if (lf) fprintf(lf, "tlbclear: map failed\n"); return 0; }
    memcpy(b, tlbwire_bin, sizeof tlbwire_bin);
    volatile uint32_t *pb = (volatile uint32_t *)(b + BIGTLB_PARAM);
    memset((void *)pb, 0, 0x100);
    pb[0] = 2; pb[1] = 8; pb[3] = 0;
    int rc = bigtlb_call(b, rq[3]);
    if (lf) fprintf(lf, "tlbclear: rc %d, Wired %u -> %u (magic %08x)\n", rc, pb[0xC0 / 4], pb[0xC4 / 4], pb[0xC8 / 4]);
    munmap(b, 64 * 1024);
    ioctl(fd, 0xC0101902u, rq);
    return 0;
}
