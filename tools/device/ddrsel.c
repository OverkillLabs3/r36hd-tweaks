/* ddrpll: live test of the DDR PLL multiplier (speed select bits 6:4 of 0x18800074, applied with bit 22; stock 1 = DDR3-1066, the
 * firmware table is 0 = 800, 1 = 1066, 2 = 1333, 3 = 1600). Always restores the stock value before it exits.
 *   ddrsel get
 *   ddrsel try <sel>     set N, wait 200 ms, write and verify 8 MB of patterns, measure read/write speed, restore
 * Run with the watchdog fed (wdtd start 6000): a hang resets the console and the PLL returns to stock.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static volatile uint32_t *r;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void setn(uint32_t n) { uint32_t v = (r[0] & ~0x70u & ~0x400000u) | ((n & 7) << 4); r[0] = v; r[0] = v | 0x400000u; }

static int memtest(uint32_t *buf, size_t words, double *wmb, double *rmb)
{
    double t0 = now();
    for (size_t i = 0; i < words; i++) buf[i] = (uint32_t)(i * 2654435761u) ^ 0xa5a5a5a5u;
    double t1 = now();
    uint32_t bad = 0, sum = 0;
    for (size_t i = 0; i < words; i++) { uint32_t v = buf[i]; sum += v; if (v != ((uint32_t)(i * 2654435761u) ^ 0xa5a5a5a5u)) bad++; }
    double t2 = now();
    (void)sum;
    *wmb = words * 4 / 1048576.0 / (t1 - t0);
    *rmb = words * 4 / 1048576.0 / (t2 - t1);
    return (int)bad;
}

static double chase(uint32_t *buf, size_t words)
{
    /* random cycle over one pointer per 64 bytes (32 MB), so every load misses the caches and the TLB */
    size_t n = words / 16;
    uint32_t *idx = malloc(n * 4);
    for (size_t i = 0; i < n; i++) idx[i] = (uint32_t)i;
    srand(1);
    for (size_t i = n - 1; i > 0; i--) { size_t j = (size_t)rand() % (i + 1); uint32_t t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    for (size_t i = 0; i < n; i++) buf[(size_t)idx[i] * 16] = (uint32_t)idx[(i + 1) % n] * 16;
    free(idx);
    uint32_t p = buf[0];
    double t0 = now();
    for (int k = 0; k < 1000000; k++) p = buf[p];
    double t1 = now();
    if (p == 0xffffffffu) printf("x");
    return (t1 - t0) * 1e9 / 1000000.0;
}

int main(int argc, char **argv)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    uint8_t *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x18800000);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    r = (volatile uint32_t *)(m + 0x74);
    uint32_t stock = (r[0] >> 4) & 7;
    printf("0x488 = %08x (N = %u, %.0f MT/s)\n", r[0], stock, stock * 8.0);
    if (argc < 2 || strcmp(argv[1], "try")) return 0;
    uint32_t n = (uint32_t)strtoul(argv[2], NULL, 0);
    size_t words = (8u << 20) / 4;
    uint32_t *buf = malloc(words * 4);
    double w, rd;
    int bad = memtest(buf, words, &w, &rd);
    printf("stock: bad %d, write %.0f MB/s, read %.0f MB/s\n", bad, w, rd);
    fflush(stdout);
    printf("stock: pointer chase %.0f ns per load\n", chase(buf, words)); fflush(stdout);
    setn(n);
    usleep(200000);
    printf("N=%u: pointer chase %.0f ns per load\n", n, chase(buf, words)); fflush(stdout);
    printf("set N=%u (%.0f MT/s), readback %08x\n", n, n * 8.0, r[0]);
    fflush(stdout);
    for (int k = 0; k < 3; k++) {
        bad = memtest(buf, words, &w, &rd);
        printf("N=%u pass %d: bad %d, write %.0f MB/s, read %.0f MB/s\n", n, k, bad, w, rd);
        fflush(stdout);
    }
    setn(stock);
    usleep(200000);
    printf("restored N=%u, readback %08x\n", stock, r[0]);
    return 0;
}
