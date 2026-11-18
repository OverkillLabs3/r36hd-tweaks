/* mmzmax: what /dev/mmz grants. Tries the heap selector word (req[0]) 0..3 with a 1 MB block and prints the
 * kernel address it returns (tells which zone each selector uses), then the largest single block of mmz0 in 1 MB steps,
 * then how much more a second block can get while the first is held. Every block is freed; nothing else is touched.
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define ALLOC 0xC0101901u
#define FREE  0xC0101902u

static int grab(int fd, uint32_t sel, uint32_t size, uint32_t r[4])
{
    r[0] = sel; r[1] = 0x20; r[2] = size; r[3] = 0;
    return ioctl(fd, ALLOC, r);
}

int main(void)
{
    int fd = open("/dev/mmz", O_RDWR);
    if (fd < 0) { perror("/dev/mmz"); return 1; }
    uint32_t r[4], r2[4];
    for (uint32_t sel = 0; sel < 4; sel++) {
        int rc = grab(fd, sel, 1u << 20, r);
        printf("selector %u, 1 MB: rc %d addr 0x%08x\n", sel, rc, r[3]);
        if (rc == 0) ioctl(fd, FREE, r);
    }
    uint32_t best = 0;
    for (uint32_t mb = 52; mb >= 40; mb--) {
        if (grab(fd, 0, mb << 20, r) == 0) { best = mb; printf("largest mmz0 block: %u MB at 0x%08x\n", mb, r[3]); break; }
    }
    if (best) {
        uint32_t kb;
        for (kb = 8192; kb >= 256; kb -= 256) if (grab(fd, 0, kb << 10, r2) == 0) break;
        if (kb >= 256) { printf("second block beside it: %u KB at 0x%08x\n", kb, r2[3]); ioctl(fd, FREE, r2); }
        else printf("second block beside it: none\n");
        for (kb = 8192; kb >= 256; kb -= 256) if (grab(fd, 1, kb << 10, r2) == 0) break;
        if (kb >= 256) { printf("selector 1 block while mmz0 is held: %u KB at 0x%08x\n", kb, r2[3]); ioctl(fd, FREE, r2); }
        ioctl(fd, FREE, r);
    }
    close(fd);
    return 0;
}
