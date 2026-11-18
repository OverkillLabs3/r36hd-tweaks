/* spiid: read-only identification of the SPI NOR flash through spidev: JEDEC ID, status registers, SFDP, unique ID,
 * security (OTP) registers. Only read commands (0x9F 0x05 0x35 0x15 0x5A 0x4B 0x48) are sent.
 *   spiid [/dev/spidevX.Y]
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

static int fd;
static int xfer(const uint8_t *tx, uint8_t *rx, int n)
{
    struct spi_ioc_transfer t; memset(&t, 0, sizeof t);
    t.tx_buf = (unsigned long)tx; t.rx_buf = (unsigned long)rx; t.len = n; t.speed_hz = 1000000; t.bits_per_word = 8;
    return ioctl(fd, SPI_IOC_MESSAGE(1), &t);
}
static void cmd(const char *name, const uint8_t *c, int clen, int rlen)
{
    uint8_t tx[600] = {0}, rx[600] = {0};
    memcpy(tx, c, clen);
    int r = xfer(tx, rx, clen + rlen);
    printf("%s (rc %d):", name, r);
    for (int i = clen; i < clen + rlen; i++) printf(" %02x", rx[i]);
    printf("\n");
}
int main(int argc, char **argv)
{
    fd = open(argc > 1 ? argv[1] : "/dev/spidev32766.1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    uint8_t m = 0; ioctl(fd, SPI_IOC_WR_MODE, &m);
    cmd("JEDEC 9F", (uint8_t[]){0x9f}, 1, 6);
    cmd("RDSR1 05", (uint8_t[]){0x05}, 1, 1);
    cmd("RDSR2 35", (uint8_t[]){0x35}, 1, 1);
    cmd("RDSR3 15", (uint8_t[]){0x15}, 1, 1);
    cmd("UID 4B", (uint8_t[]){0x4b, 0, 0, 0, 0}, 5, 8);
    cmd("SFDP 5A", (uint8_t[]){0x5a, 0, 0, 0, 0}, 5, 64);
    for (int r = 1; r <= 3; r++) {
        uint8_t c[5] = {0x48, 0, (uint8_t)(r << 4), 0, 0};
        char n[32]; snprintf(n, sizeof n, "SECREG%d 48", r);
        cmd(n, c, 5, 64);
    }
    close(fd);
    return 0;
}
