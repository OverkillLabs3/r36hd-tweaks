/* prints million dependent adds per second over ~0.4 s (tracks the CPU clock directly) */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main(void)
{
    uint32_t x = 1, i, n = 100000000u;
    double t = now_s();
    for (i = 0; i < n; i++) __asm__ volatile("addu %0, %0, %1" : "+r"(x) : "r"(i));
    t = now_s() - t;
    printf("%.0f M adds/s (%.3f s)\n", n / t / 1e6, t);
    return (int)(x & 0);
}
