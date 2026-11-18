/* pmu: the main CPU's four hardware performance counters (MIPS 74K, CP0 register 25), read from user space through
 * the kernel-mode gadget kx uses (the virtuart driver's poll entry pointed at a payload for one call).
 *
 *   pmu bench <report> <dir>          check the counters on known loops (ALU, stream, pointer chase, 2D texture walk)
 *   pmu watch <report> <dir> <secs>   count user-mode events of every process for <secs> seconds, one event set per
 *                                     second in rotation, and print the per-second numbers (run it next to a game)
 *
 * Counter events (74K manual MD00519 table 7.49; counters 0/2 and 1/3 have different event lists):
 *   set A: c0 cycles (0)               c1 instructions (1)        c2 D-cache writebacks (24)   c3 D-cache misses (24)
 *   set B: c0 cacheable loads (23)     c1 all D-cache accesses (23) c2 JTLB data accesses (25) c3 JTLB data misses (25)
 *   set C: c0 cycles stalled on I-cache miss (7) c1 I-cache misses (6) c2 ALU no issue, operands not ready (17)
 *          c3 AGEN no issue, operands not ready (17)
 *   set D: c0 fill store buffer full stalls (30) c1 mispredicted conditional branches (39)
 *          c2 load data queue full stalls (31)   c3 cycles with the writeback buffer more than half full (32)
 * Only user mode is counted (U bit). The counters are stopped when the tool ends. A reboot clears everything.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/cachectl.h>

#define FOPS_PHYS 0x0039b1b8u
#define POLL_SLOT (FOPS_PHYS + 7 * 4)
#define POLL_STOCK 0x802bc8f4u
#define BLK_SIZE (128u << 10)
#define OFF_SET 0x0000u
#define OFF_READ 0x10000u

static FILE *rep;
static void say(const char *f, ...)
{
    va_list ap; va_start(ap, f); vfprintf(rep, f, ap); va_end(ap);
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    fflush(rep); fsync(fileno(rep));
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static int mz = -1, memfd = -1;
static uint32_t rq[4];
static uint8_t *blk;
static uint32_t kbase;
static volatile uint32_t *slot;
static const char *dir;

static volatile uint32_t *map_same_colour(uint32_t phys)
{
    uint8_t *r = mmap(NULL, 4 * 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) return NULL;
    uintptr_t va = (((uintptr_t)r + 8191) & ~(uintptr_t)8191) | (phys & 0x1000u);
    void *m = mmap((void *)va, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, memfd, phys & ~4095u);
    if (m == MAP_FAILED || (uintptr_t)m != va) return NULL;
    return (volatile uint32_t *)((uint8_t *)m + (phys & 4095u));
}

static int load_payload(const char *name, uint32_t off)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) { say("missing %s\n", p); return -1; }
    size_t n = fread(blk + off, 1, 0x8000, f); fclose(f);
    return n ? 0 : -1;
}

/* run the payload at `off` once in kernel mode; 0 when the call happened and the slot is stock again */
static int call(uint32_t off)
{
    cacheflush(blk, BLK_SIZE, BCACHE);
    int vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (vu < 0) return -1;
    if (*slot != POLL_STOCK) { close(vu); return -3; }
    *slot = kbase + off; __sync_synchronize();
    struct pollfd pf = { vu, POLLIN, 0 };
    poll(&pf, 1, 0);
    *slot = POLL_STOCK; __sync_synchronize();
    close(vu);
    cacheflush(blk, BLK_SIZE, BCACHE);
    return *slot == POLL_STOCK ? 0 : -2;
}

#define CTL(ev) (((uint32_t)(ev) << 5) | 0x8u)   /* count the event in user mode */
static const uint32_t sets[4][4] = {
    { CTL(0), CTL(1), CTL(24), CTL(24) },
    { CTL(23), CTL(23), CTL(25), CTL(25) },
    { CTL(7), CTL(6), CTL(17), CTL(17) },
    { CTL(30), CTL(39), CTL(31), CTL(32) },
};
static const char *names[4][4] = {
    { "cycles", "instructions", "dcache_writebacks", "dcache_misses" },
    { "cacheable_loads", "dcache_accesses", "jtlb_data_accesses", "jtlb_data_misses" },
    { "icache_miss_stall_cycles", "icache_misses", "alu_wait_operand_cycles", "agen_wait_operand_cycles" },
    { "store_buffer_full_stalls", "branch_mispredicts", "load_queue_full_stalls", "writeback_buffer_half_cycles" },
};

static int pmu_set(int s)
{
    volatile uint32_t *pb = (volatile uint32_t *)(blk + OFF_SET + 0x8000);
    memset((void *)pb, 0, 64);
    for (int i = 0; i < 4; i++) pb[i] = sets[s][i];
    if (call(OFF_SET)) return -1;
    return pb[15] == 0x504D5553u ? 0 : -1;
}
static int pmu_stop(void)
{
    volatile uint32_t *pb = (volatile uint32_t *)(blk + OFF_SET + 0x8000);
    memset((void *)pb, 0, 64);
    return call(OFF_SET);
}
static int pmu_read(uint32_t c[5])
{
    volatile uint32_t *pb = (volatile uint32_t *)(blk + OFF_READ + 0x8000);
    memset((void *)pb, 0, 64);
    if (call(OFF_READ) || pb[15] != 0x504D5552u) return -1;
    for (int i = 0; i < 5; i++) c[i] = pb[i];
    return 0;
}

static volatile uint32_t sink;
typedef void (*work_fn)(void);
static uint32_t *big;
static void w_alu(void) { uint32_t a = 1, b = 2; for (int i = 0; i < 20000000; i++) { a += b; b ^= a << 1; } sink = a + b; }
static void w_stream(void) { uint32_t s = 0; for (int r = 0; r < 8; r++) for (uint32_t i = 0; i < (8u << 20) / 4; i += 8) s += big[i]; sink = s; }
static void w_chase(void) { uint32_t p = 0; for (int i = 0; i < 2000000; i++) p = big[p]; sink = p; }
static void w_tex(void)
{   /* a 256x256 32-bit texture read along a rotated line per row, like an affine span walk */
    uint32_t s = 0, *t = big + (4u << 20) / 4;
    for (int r = 0; r < 200; r++) {
        int u = r * 37 << 16, v = r * 11 << 16, du = 46341, dv = 46341;
        for (int y = 0; y < 192; y++) { int uu = u + y * dv, vv = v - y * du;
            for (int x = 0; x < 256; x++) { s += t[((vv >> 16) & 255) * 256 + ((uu >> 16) & 255)]; uu += du; vv += dv; } }
    }
    sink = s;
}

static void run_bench(void)
{
    const uint32_t N = (8u << 20) / 4;
    big = malloc(N * 4 + (4u << 20) + 256 * 256 * 4);
    if (!big) { say("no memory\n"); return; }
    for (uint32_t i = 0; i < N; i++) big[i] = i;
    uint32_t s = 99;
    for (uint32_t i = N - 1; i > 0; i--) { s = s * 1103515245u + 12345u; uint32_t j = s % i; uint32_t x = big[i]; big[i] = big[j]; big[j] = x; }
    static const struct { const char *n; work_fn f; } w[] = {
        { "alu 20M adds", w_alu }, { "stream 8 MB x8", w_stream }, { "chase 8 MB 2M loads", w_chase }, { "texture walk 256x256", w_tex } };
    for (unsigned k = 0; k < 4; k++) {
        for (int st = 0; st < 4; st++) {
            uint32_t a[5], b[5];
            if (pmu_set(st) || pmu_read(a)) { say("counter setup failed\n"); return; }
            double t0 = now(); w[k].f(); double t = now() - t0;
            if (pmu_read(b)) { say("counter read failed\n"); return; }
            say("%-22s set %c %.3f s:", w[k].n, 'A' + st, t);
            for (int i = 0; i < 4; i++) say(" %s=%u", names[st][i], b[i] - a[i]);
            say("\n");
        }
    }
}

static volatile int stop_flag;
static void on_sig(int s) { (void)s; stop_flag = 1; }

static void run_watch(int secs)
{
    signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
    double t_end = now() + secs;
    int st = 0;
    while (!stop_flag && now() < t_end) {
        uint32_t a[5], b[5];
        if (pmu_set(st) || pmu_read(a)) { say("counter setup failed\n"); return; }
        double t0 = now();
        while (!stop_flag && now() - t0 < 1.0) usleep(100000);
        if (pmu_read(b)) { say("counter read failed\n"); return; }
        say("t=%.1f set %c %.3f s:", t0, 'A' + st, now() - t0);
        for (int i = 0; i < 4; i++) say(" %s=%u", names[st][i], b[i] - a[i]);
        say("\n");
        st = (st + 1) & 3;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: pmu bench|watch <report> <dir> [secs]\n"); return 2; }
    rep = fopen(argv[2], "a");
    dir = argv[3];
    if (!rep) return 1;
    say("pmu %s: main CPU performance counters\n", argv[1]);
    mz = open("/dev/mmz", O_RDWR);
    rq[0] = 0; rq[1] = 0x20; rq[2] = BLK_SIZE; rq[3] = 0;
    if (mz < 0 || ioctl(mz, 0xC0101901u, rq) < 0) { say("mmz alloc failed: %s\n", strerror(errno)); return 0; }
    blk = mmap(NULL, BLK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mz, 0);
    if (blk == MAP_FAILED || rq[3] < 0x80000000u || rq[3] >= 0x88000000u) { say("mmz map failed (%08x)\n", rq[3]); return 0; }
    kbase = rq[3];
    memset(blk, 0, BLK_SIZE);
    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0) { say("no /dev/mem\n"); return 0; }
    slot = map_same_colour(POLL_SLOT);
    if (!slot) { say("colour-matched mapping failed\n"); return 0; }
    if (*slot != POLL_STOCK) { say("poll slot 0x%08x is not the stock value, nothing changed\n", *slot); return 0; }
    if (load_payload("pmuset.bin", OFF_SET) || load_payload("pmuread.bin", OFF_READ)) return 0;
    if (pmu_set(0)) { say("pmuset did not report\n"); return 0; }
    {
        volatile uint32_t *pb = (volatile uint32_t *)(blk + OFF_SET + 0x8000);
        say("PerfCtl read back: %08x %08x %08x %08x\n", pb[4], pb[5], pb[6], pb[7]);
    }
    if (!strcmp(argv[1], "bench")) run_bench();
    else if (!strcmp(argv[1], "watch")) run_watch(argc > 4 ? atoi(argv[4]) : 60);
    pmu_stop();
    say("counters stopped, poll slot 0x%08x\ndone\n", *slot);
    munmap(blk, BLK_SIZE); ioctl(mz, 0xC0101902u, rq); close(mz);
    return 0;
}
