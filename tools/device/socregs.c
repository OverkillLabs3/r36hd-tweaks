/* socregs: read-only dump of the system-control registers (clocks, PLLs, straps, DDR info) through /dev/mem.
 * Only plain configuration ranges are read (no interrupt-status or FIFO registers), so reading has no side effects.
 * Usage: socregs <report> */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static void dump(FILE *f, volatile uint32_t *m, uint32_t base, uint32_t from, uint32_t to)
{
    for (uint32_t o = from; o < to; o += 16) {
        fprintf(f, "%08x:", base + o);
        for (int i = 0; i < 4; i++) fprintf(f, " %08x", m[(o >> 2) + i]);
        fprintf(f, "\n");
    }
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "/mnt/sdcard/socregs.txt", "w");
    if (!f) return 1;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { fprintf(f, "no /dev/mem\n"); return 0; }
    volatile uint32_t *m = mmap(NULL, 0x2000, PROT_READ, MAP_SHARED, fd, 0x18800000);
    if (m == MAP_FAILED) { fprintf(f, "mmap failed\n"); return 0; }
    uint32_t id = m[0], info70 = m[0x70 / 4], ddr = m[0x1000 / 4];
    uint16_t dpll = *(volatile uint16_t *)((volatile uint8_t *)m + 0x48a);
    static const int mts[8] = { 800, 1066, 1333, 1600, 600, 0, 0, 0 };
    fprintf(f, "chip id 0x%08x (0x1600xxxx = HC1600)\n", id);
    fprintf(f, "DDR: type DDR%u, size %u MB, info70 0x%08x -> %d MT/s by strap", ((ddr >> 23) & 1) + 2, 16u << (ddr & 7), info70, mts[(info70 >> 4) & 7]);
    if (dpll & 0x8000) fprintf(f, ", DPLL override 0x%04x -> %u MT/s", dpll, (dpll & 0x7fff) * 12);
    fprintf(f, "\nCPU mux 0x%08x, PLL380 0x%08x\n", m[0x74 / 4], m[0x380 / 4]);
    fprintf(f, "-- 0x18800000..0x18800500\n"); dump(f, m, 0x18800000, 0, 0x500);
    fprintf(f, "-- 0x18801000..0x18801100\n"); dump(f, m, 0x18800000, 0x1000, 0x1100);
    fprintf(f, "done\n");
    fclose(f);
    return 0;
}
