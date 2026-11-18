/* efuse_read: read the 256 readable eFuse bits the same way the kernel does (efuse_raw_read_for_check):
 * controller at 0x18818e00: +0x00 control (bits 1:0 = 1 starts a read), +0x08 status (bit 31 = done, written back to
 * clear), +0x10..+0x2f data. Read-only.
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    volatile uint8_t *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x18818000);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    volatile uint32_t *r = (volatile uint32_t *)(m + 0xe00);
    printf("before: ctrl %08x status %08x\n", r[0], r[2]);
    r[0] = (r[0] & ~3u) | 1u;
    int i;
    for (i = 0; i < 5000 && !(r[2] & 0x80000000u); i++) usleep(1000);
    printf("status %08x after %d ms\n", r[2], i);
    for (int k = 0; k < 8; k++) printf("%08x%s", r[4 + k], k == 7 ? "\n" : " ");
    r[2] = r[2];
    return 0;
}
