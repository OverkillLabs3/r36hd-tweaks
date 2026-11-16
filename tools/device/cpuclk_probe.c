/* cpuclk_probe: read-only measurement of the real CPU clock and the clock registers.
 *  1. times a chain of dependent addu instructions (1 cycle each on a MIPS 74K) -> CPU MHz
 *  2. reads the SoC clock registers through /dev/mem (read only): 0x18800070..0x1880007c, 0x18800380, 0x188003b0
 *  3. prints the cp0 count rate seen through clock_gettime for comparison
 * Output goes to stdout; the launcher script redirects it to the SD card root.
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

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* 1024 dependent adds per call; 1 cycle each, so cycles = calls * 1024 (+ ~3 loop cycles per call) */
static inline uint32_t chain1024(uint32_t x, uint32_t y)
{
#define A4 "addu %0,%0,%1\n addu %0,%0,%1\n addu %0,%0,%1\n addu %0,%0,%1\n"
#define A16 A4 A4 A4 A4
#define A64 A16 A16 A16 A16
#define A256 A64 A64 A64 A64
    __asm__ volatile(A256 A256 A256 A256 : "+r"(x) : "r"(y));
    return x;
}

/* independent 24 MHz timer block (DT: hichip,south-bridge-timer @0x18818a00). The boot loader reads
 * ms = reg[0xa0c] * 1000 + (u16)reg[0xa0a]. The kernel clock is the cp0 count, which runs from the CPU clock itself,
 * so a kernel-time measurement can never see a CPU clock change. This one can. */
static volatile uint32_t *hwt;
static double hw_ms(void)
{
    uint32_t s1, s2; uint16_t ms;
    do { s1 = *(volatile uint32_t *)((volatile uint8_t *)hwt + 0xa0c); ms = *(volatile uint16_t *)((volatile uint8_t *)hwt + 0xa0a); s2 = *(volatile uint32_t *)((volatile uint8_t *)hwt + 0xa0c); } while (s1 != s2);
    return s1 * 1000.0 + ms;
}

int main(void)
{
    const uint32_t calls = 20000;     /* 20.48 M dependent adds */
    double best = 1e9;
    uint32_t x = 1, y = 3;
    for (int rep = 0; rep < 7; rep++) {
        double t0 = now();
        for (uint32_t i = 0; i < calls; i++) x = chain1024(x, y);
        double t = now() - t0;
        if (t < best) best = t;
    }
    double cycles = (double)calls * (1024.0 + 3.0);
    printf("dependent add chain: best %.4f s for %.0f adds -> CPU ~ %.1f MHz (checksum %u)\n", best, (double)calls * 1024, cycles / best / 1e6, x);
    printf("(that number uses the kernel clock, which follows the CPU clock; see the hardware timer line below)\n");

    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { printf("/dev/mem open failed (read only): cannot read clock registers\n"); return 0; }
    hwt = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x18818000);
    if (hwt != MAP_FAILED) {
        double a = hw_ms(); usleep(200000); double b = hw_ms();
        printf("hardware timer check: %.0f ms ticked during a 200 ms sleep (expect about 200; less than 200 means the kernel sleep clock runs fast, i.e. the CPU is faster than 1188)\n", b - a);
        a = hw_ms(); usleep(1000000); b = hw_ms();
        if (b - a > 500 && b - a < 2000)
            printf("sleep cross-check: a 1000 ms kernel sleep lasted %.0f ms on the 24 MHz timer -> implied CPU ~ %.0f MHz (kernel assumes 1188, cp0 = CPU/2)\n", b - a, 1188.0 * 1000.0 / (b - a));
        if (b - a > 500 && b - a < 2000) {
            const uint32_t n2 = 400000;   /* 410 M adds, about 0.35 s */
            double bestt = 1e18;
            for (int rep = 0; rep < 5; rep++) {
                double h0 = hw_ms();
                uint32_t z = 1;
                for (uint32_t i = 0; i < n2; i++) z = chain1024(z, y);
                double dt = hw_ms() - h0;
                printf("  sample %d: %.0f ms -> %.1f MHz\n", rep, dt, (double)n2 * 1027 / (dt / 1000.0) / 1e6);
                if (dt < bestt) bestt = dt;
                x += z;
            }
            printf("hardware timer clock: %.0f ms for %.0f adds -> CPU ~ %.1f MHz (true, 24 MHz timer)\n", bestt, (double)n2 * 1027, (double)n2 * 1027 / (bestt / 1000.0) / 1e6);
        } else {
            printf("hardware timer line skipped: the timer did not behave as milliseconds\n");
        }
    } else {
        printf("mmap of the timer block failed\n");
    }

    volatile uint32_t *r = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x18800000);
    if (r == MAP_FAILED) { printf("mmap /dev/mem failed\n"); close(fd); return 0; }
    static const struct { uint32_t off; const char *name; } regs[] = {
        { 0x000, "chip id" }, { 0x070, "clk70 (HW sel)" }, { 0x074, "clk74 (cpu source mux: bits10:8, apply bit22)" },
        { 0x078, "clk78" }, { 0x07c, "clk7c (PLL direct bit7)" }, { 0x094, "clk94" }, { 0x09c, "clk9c (scpu mux, 1600)" },
        { 0x380, "pll380 (divider bits31:16, enable bit31)" }, { 0x3b0, "pll3b0 (scpu PLL, 1600)" },
    };
    for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++)
        printf("reg 0x%08x = 0x%08x   %s\n", 0x18800000 + regs[i].off, r[regs[i].off / 4], regs[i].name);
    uint32_t r74 = r[0x74 / 4], r380 = r[0x380 / 4];
    printf("decoded: cpu mux (r74>>8)&7 = %u, PLL divider (r380>>16)&0x7fff = %u -> PLL ~ %.1f MHz\n",
           (r74 >> 8) & 7, (r380 >> 16) & 0x7fff, ((r380 >> 16) & 0x7fff) * 2.4 + 2.4);
    munmap((void *)r, 0x1000);
    close(fd);
    return 0;
}