/* mmz_probe: can a game use the video co-processor's 52 MB "mmz" pool as extra RAM?
 * ABI recovered from the TreeFrogUI driver (cube_malloc.c): open("/dev/mmz"), ioctl(0xC0101901) with
 * struct {u32 a=0, b=0x20, size, d=0}, then mmap(size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0).
 * free = munmap + ioctl(0xC0101902) + close. This probe allocates 1..48 MB, prints the words the ioctl returns,
 * times sequential write/read and a pointer-chase against normal malloc memory (is the mapping cached?), then frees.
 * Everything is released at exit; a reboot clears it in any case.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

struct req { uint32_t w[4]; };

static void bench(const char *tag, volatile uint32_t *p, size_t bytes)
{
    size_t n = bytes / 4;
    double t0 = now();
    for (int r = 0; r < 4; r++) for (size_t i = 0; i < n; i++) p[i] = (uint32_t)i;
    double tw = now() - t0;
    t0 = now();
    uint32_t s = 0;
    for (int r = 0; r < 4; r++) for (size_t i = 0; i < n; i++) s += p[i];
    double tr = now() - t0;
    /* pointer chase with a 64 KB stride pattern: latency */
    size_t steps = 200000, idx = 0;
    for (size_t i = 0; i < n; i++) p[i] = (uint32_t)((i + 16411) % n);
    t0 = now();
    for (size_t i = 0; i < steps; i++) idx = p[idx];
    double tl = (now() - t0) / steps * 1e9;
    printf("  %-8s write %6.1f MB/s  read %6.1f MB/s  chase %6.0f ns/load  (sum %u idx %zu)\n", tag,
           bytes * 4 / tw / 1e6, bytes * 4 / tr / 1e6, tl, s, idx);
}

/* mode "leak": allocate 40 MB, map and touch it, then exit WITHOUT freeing (what a crashing core does).
 * mode "check": try to allocate 48 MB and 1 MB; if the leak run kept the memory, this fails. */
static int try_alloc(int fd, size_t mb, int keep)
{
    struct req rq = { { 0, 0x20, (uint32_t)(mb << 20), 0 } };
    int r = ioctl(fd, 0xC0101901, &rq);
    printf("try alloc %zu MB: ioctl=%d errno=%d\n", mb, r, r < 0 ? errno : 0);
    if (r < 0) return -1;
    void *p = mmap(NULL, mb << 20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { printf("  mmap failed: %s\n", strerror(errno)); ioctl(fd, 0xC0101902, &rq); return -1; }
    memset(p, 0x5a, mb << 20);
    if (!keep) { munmap(p, mb << 20); ioctl(fd, 0xC0101902, &rq); }
    return 0;
}

int main(int argc, char **argv)
{
    int fd = open("/dev/mmz", O_RDWR);
    if (fd < 0) { printf("open /dev/mmz failed: %s\n", strerror(errno)); return 0; }
    if (argc > 1 && !strcmp(argv[1], "leak")) {
        int r = try_alloc(fd, 40, 1);
        printf("leak mode: 40 MB %s, exiting without free\n", r ? "FAILED" : "held");
        fflush(stdout);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "check")) {
        int a = try_alloc(fd, 48, 0), b = a ? try_alloc(fd, 8, 0) : 0;
        printf("check mode: 48 MB %s, 8 MB %s -> %s\n", a ? "FAILED" : "ok", a ? (b ? "FAILED" : "ok") : "-",
               a ? "the exited process still holds its memory: it LEAKS until reboot" : "memory is freed when the process exits");
        return 0;
    }
    static const int sizes_mb[] = { 1, 2, 4, 8, 16, 24, 32, 40, 48, 52 };
    size_t last_ok = 0;
    for (unsigned k = 0; k < sizeof sizes_mb / sizeof sizes_mb[0]; k++) {
        size_t bytes = (size_t)sizes_mb[k] << 20;
        struct req rq = { { 0, 0x20, (uint32_t)bytes, 0 } };
        int r = ioctl(fd, 0xC0101901, &rq);
        printf("alloc %2d MB: ioctl=%d errno=%d  words %08x %08x %08x %08x\n", sizes_mb[k], r, r < 0 ? errno : 0,
               rq.w[0], rq.w[1], rq.w[2], rq.w[3]);
        fflush(stdout);
        if (r < 0) break;
        void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) { printf("  mmap failed: %s\n", strerror(errno)); ioctl(fd, 0xC0101902, &rq); break; }
        last_ok = bytes;
        if (sizes_mb[k] == 1 || sizes_mb[k] == 8) {
            bench("mmz", (volatile uint32_t *)p, bytes < (4u << 20) ? bytes : (4u << 20));
            void *m = malloc(bytes < (4u << 20) ? bytes : (4u << 20));
            if (m) { bench("malloc", (volatile uint32_t *)m, bytes < (4u << 20) ? bytes : (4u << 20)); free(m); }
        }
        munmap(p, bytes);
        int fr = ioctl(fd, 0xC0101902, &rq);
        printf("  freed (ioctl %d)\n", fr);
        fflush(stdout);
    }
    printf("largest single block that mapped: %zu MB\n", last_ok >> 20);
    close(fd);
    return 0;
}
