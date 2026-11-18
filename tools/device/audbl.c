/* audbl: read or set the backlight and the speaker volume the way cubevol does.
 *   audbl bl            print the raw backlight value (/dev/backlight, 4-byte read)
 *   audbl bl <raw>      set it (4-byte write; the stock UI uses raw 23..255)
 *   audbl vol           print the volume (/dev/sndC0i2so ioctl 0x4001080c)
 *   audbl vol <n>       set it (ioctl 0x8001080b)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    if (!strcmp(argv[1], "bl")) {
        int fd = open("/dev/backlight", O_RDWR);
        if (fd < 0) { perror("/dev/backlight"); return 1; }
        uint32_t v = 0xffffffffu;
        if (argc > 2) { v = (uint32_t)strtoul(argv[2], NULL, 0); if (write(fd, &v, 4) != 4) perror("write"); fsync(fd); }
        v = 0xffffffffu; lseek(fd, 0, SEEK_SET);
        ssize_t n = read(fd, &v, 4);
        printf("backlight raw %u (read %zd)\n", v, n);
        return 0;
    }
    if (!strcmp(argv[1], "vol")) {
        int fd = open("/dev/sndC0i2so", O_RDONLY);
        if (fd < 0) { perror("/dev/sndC0i2so"); return 1; }
        uint32_t v = 0xffffffffu;
        if (argc > 2) { v = (uint32_t)strtoul(argv[2], NULL, 0); printf("set rc %d\n", ioctl(fd, 0x8001080bu, &v)); }
        v = 0xffffffffu;
        printf("get rc %d", ioctl(fd, 0x4001080cu, &v));
        printf(", volume %u\n", v);
        return 0;
    }
    return 1;
}
