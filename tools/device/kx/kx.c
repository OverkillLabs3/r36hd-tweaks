/* kx: run small position-independent payloads in kernel mode on the main CPU at run time, read the CPU's CP0
 * configuration, and measure what the writable Config7 controls change. Everything is put back before exit, and a
 * reboot restores everything anyway.
 *
 * Entry: the virtuart driver's file_operations (kernel .rodata, virtual 0x8039b1b8, physical 0x0039b1b8) is in RAM
 * that /dev/mem can write. Its poll entry (offset 7 words, stock 0x802bc8f4) is pointed at a payload for exactly one
 * poll() call on /dev/virtuart, then put back. Payloads live in an mmz block (kernel address 0x80000000 | physical).
 * Cache rules (32 KB 4-way VIPT data cache with aliases): the fops word is written through a cached /dev/mem mapping
 * at a virtual address of the same cache colour as the kernel address (same cache line as the kernel's); payloads and
 * their parameter blocks are written back with cacheflush() before the call and read after another cacheflush().
 *
 * Experiments (each measured with the same benchmark set, then restored):
 *   A. stock Config7
 *   B. Config7.ES = 0 (bit 8; set by the boot loader; makes every SYNC a bus transaction when the SoC accepts it)
 *   C. Config7.PREF = 3 (bits 12:11; instruction-cache prefetch of 2 lines instead of 1)
 * Usage: kx <report> <dir with cp0read.bin cfg7write.bin>
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
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/cachectl.h>

#define FOPS_PHYS 0x0039b1b8u
#define POLL_SLOT (FOPS_PHYS + 7 * 4)
#define POLL_STOCK 0x802bc8f4u

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

static volatile uint32_t *map_same_colour(uint32_t phys)
{
    uint8_t *r = mmap(NULL, 4 * 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) return NULL;
    uintptr_t va = (((uintptr_t)r + 8191) & ~(uintptr_t)8191) | (phys & 0x1000u);
    void *m = mmap((void *)va, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, memfd, phys & ~4095u);
    if (m == MAP_FAILED || (uintptr_t)m != va) return NULL;
    return (volatile uint32_t *)((uint8_t *)m + (phys & 4095u));
}

/* copy a payload to offset `off` of the block and run it once in kernel mode; returns 0 when the call happened */
static int run_payload(const char *dir, const char *name, uint32_t off)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) { say("missing %s\n", p); return -1; }
    size_t n = fread(blk + off, 1, 0x8000, f); fclose(f);
    if (!n) return -1;
    cacheflush(blk, 1u << 20, BCACHE);
    int vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (vu < 0) { say("no /dev/virtuart\n"); return -1; }
    if (*slot != POLL_STOCK) { say("poll slot not stock (0x%08x), stop\n", *slot); close(vu); return -1; }
    *slot = kbase + off; __sync_synchronize();
    struct pollfd pf = { vu, POLLIN, 0 };
    poll(&pf, 1, 0);
    *slot = POLL_STOCK; __sync_synchronize();
    close(vu);
    cacheflush(blk, 1u << 20, BCACHE);
    return *slot == POLL_STOCK ? 0 : -2;
}

/* ---- benchmarks (user mode) ---- */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile uint32_t sink;
static void bench(const char *tag)
{
    double t0, t;
    t0 = now(); for (int i = 0; i < 200000; i++) __asm__ volatile("sync" ::: "memory"); t = now() - t0;
    double sync_ns = t / 200000 * 1e9;
    t0 = now(); for (int i = 0; i < 300000; i++) { pthread_mutex_lock(&mtx); sink++; pthread_mutex_unlock(&mtx); } t = now() - t0;
    double mutex_ns = t / 300000 * 1e9;
    t0 = now(); for (int i = 0; i < 200000; i++) { void *p = malloc(32 + (i & 255)); sink += (uintptr_t)p; free(p); } t = now() - t0;
    double malloc_ns = t / 200000 * 1e9;
    /* branchy dispatch loop: a small interpreter over a pseudo-random opcode stream */
    static uint8_t ops[1 << 16];
    uint32_t s = 12345; for (int i = 0; i < (1 << 16); i++) { s = s * 1103515245u + 12345u; ops[i] = (s >> 16) & 15; }
    uint32_t acc = 1;
    t0 = now();
    for (int r = 0; r < 40; r++) for (int i = 0; i < (1 << 16); i++) {
        switch (ops[i]) {
        case 0: acc += i; break; case 1: acc ^= i; break; case 2: acc *= 3; break; case 3: acc -= i; break;
        case 4: acc = (acc << 1) | (acc >> 31); break; case 5: acc += 7; break; case 6: acc ^= 0x55; break;
        case 7: acc += acc >> 3; break; case 8: acc -= 9; break; case 9: acc |= 1; break; case 10: acc &= ~2u; break;
        case 11: acc += i * 3; break; case 12: acc ^= i << 2; break; case 13: acc -= acc >> 5; break;
        case 14: acc += 11; break; default: acc ^= 0xa5a5; break;
        }
    }
    t = now() - t0; sink += acc;
    double branchy_ns = t / (40.0 * 65536) * 1e9;
    /* pointer chase over 8 MB */
    static uint32_t *chain;
    const uint32_t N = (8u << 20) / 4;
    if (!chain) {
        chain = malloc(N * 4);
        for (uint32_t i = 0; i < N; i++) chain[i] = i;
        for (uint32_t i = N - 1; i > 0; i--) { s = s * 1103515245u + 12345u; uint32_t j = s % i; uint32_t x = chain[i]; chain[i] = chain[j]; chain[j] = x; }
    }
    uint32_t pz = 0; t0 = now(); for (int i = 0; i < 1000000; i++) pz = chain[pz]; t = now() - t0; sink += pz;
    double chase_ns = t / 1e6 * 1e9;
    /* memcpy 1 MB x 50 */
    static uint8_t *ma, *mb2; if (!ma) { ma = malloc(1 << 20); mb2 = malloc(1 << 20); memset(ma, 1, 1 << 20); }
    t0 = now(); for (int i = 0; i < 50; i++) { memcpy(mb2, ma, 1 << 20); sink += mb2[i]; } t = now() - t0;
    double memcpy_mbs = 50.0 / t;
    say("%-10s sync %.1f ns | mutex lock+unlock %.1f ns | malloc+free %.1f ns | branchy op %.2f ns | chase 8MB %.1f ns | memcpy %.0f MB/s\n",
        tag, sync_ns, mutex_ns, malloc_ns, branchy_ns, chase_ns, memcpy_mbs);
}

static int cfg7(const char *dir, uint32_t value, uint32_t mask, uint32_t *oldv, uint32_t *newv)
{
    volatile uint32_t *pb = (volatile uint32_t *)(blk + 0x4000 + 0x8100);
    memset((void *)pb, 0, 32);
    pb[0] = value; pb[1] = mask;
    if (run_payload(dir, "cfg7write.bin", 0x4000)) return -1;
    if (pb[4] != 0x37474643u) { say("cfg7write did not report\n"); return -1; }
    *oldv = pb[2]; *newv = pb[3];
    return 0;
}

int main(int argc, char **argv)
{
    rep = fopen(argc > 1 ? argv[1] : "/mnt/sdcard/kx_report.txt", "w");
    const char *dir = argc > 2 ? argv[2] : ".";
    if (!rep) return 1;
    say("kx: kernel-mode payloads on the main CPU\n");
    mz = open("/dev/mmz", O_RDWR);
    rq[0] = 0; rq[1] = 0x20; rq[2] = 1u << 20; rq[3] = 0;
    if (mz < 0 || ioctl(mz, 0xC0101901u, rq) < 0) { say("mmz alloc failed: %s\n", strerror(errno)); return 0; }
    blk = mmap(NULL, 1u << 20, PROT_READ | PROT_WRITE, MAP_SHARED, mz, 0);
    if (blk == MAP_FAILED || rq[3] < 0x80000000u || rq[3] >= 0x88000000u) { say("mmz map failed (%08x)\n", rq[3]); return 0; }
    kbase = rq[3];
    memset(blk, 0, 1u << 20);
    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0) { say("no /dev/mem\n"); return 0; }
    slot = map_same_colour(POLL_SLOT);
    if (!slot) { say("colour-matched mapping failed\n"); return 0; }
    say("payload block at 0x%08x, virtuart poll slot 0x%08x (stock 0x%08x)\n", kbase, *slot, POLL_STOCK);
    if (*slot != POLL_STOCK) { say("unexpected kernel, nothing changed\n"); return 0; }

    /* 1. read the configuration */
    if (run_payload(dir, "cp0read.bin", 0)) { say("cp0read failed\n"); return 0; }
    volatile uint32_t *res = (volatile uint32_t *)(blk + 0x8000);
    if (res[15] != 0x4B584F4Bu) { say("cp0read did not report (magic %08x)\n", res[15]); return 0; }
    static const char *nm[15] = { "PRId", "EBase", "Config", "Config1", "Config2", "Config3", "Config6", "Config7",
                                  "Status", "Wired", "PageMask", "Config4", "Config5", "ErrCtl", "IntCtl" };
    for (int i = 0; i < 15; i++) say("  %-8s 0x%08x\n", nm[i], res[i]);
    uint32_t c7 = res[7];
    say("  Config7: ES(8)=%u PREF(12:11)=%u BP(3)=%u RPS(2)=%u BHT(1)=%u SL(0)=%u IHB(29)=%u FPR=%u%u AR(16)=%u\n",
        (c7 >> 8) & 1, (c7 >> 11) & 3, (c7 >> 3) & 1, (c7 >> 2) & 1, (c7 >> 1) & 1, c7 & 1, (c7 >> 29) & 1,
        (c7 >> 28) & 1, (c7 >> 17) & 1, (c7 >> 16) & 1);

    /* 2. benchmarks */
    bench("stock");
    uint32_t o, n;
    if ((c7 >> 8) & 1) {
        if (!cfg7(dir, 0, 1u << 8, &o, &n)) {
            say("ES off: Config7 0x%08x -> 0x%08x\n", o, n);
            bench("ES=0");
            if (!cfg7(dir, c7, 1u << 8, &o, &n)) say("ES restored: 0x%08x\n", n);
        }
    } else say("ES already 0 (the SoC does not accept external syncs, or the bit is clear)\n");
    if (!cfg7(dir, 3u << 11, 3u << 11, &o, &n)) {
        say("PREF=3: Config7 0x%08x -> 0x%08x\n", o, n);
        bench("PREF=3");
        if (!cfg7(dir, c7, 3u << 11, &o, &n)) say("PREF restored: 0x%08x\n", n);
    }
    bench("restored");

    /* 3. wired large-page TLB entries over a 32 MB mmz block mapped at a fixed address (like a game's pool) */
    {
        int fd2 = open("/dev/mmz", O_RDWR);
        uint32_t rq2[4] = { 0, 0x20, 32u << 20, 0 };
        if (fd2 >= 0 && ioctl(fd2, 0xC0101901u, rq2) == 0 && rq2[3] >= 0x80000000u) {
            uint32_t pa = rq2[3] & 0x1fffffffu, size = 32u << 20;
            uintptr_t va = 0x40000000u + (pa & 0x7fffffu);         /* va = pa (mod 8 MB), so 4 MB pages line up */
            uint8_t *pool = mmap((void *)va, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd2, 0);
            if (pool != MAP_FAILED && (uintptr_t)pool == va) {
                volatile uint32_t *pb = (volatile uint32_t *)(blk + 0x10000 + 0x8200);
                uint32_t n = 0;
                memset((void *)pb, 0, 0x100);
                for (uintptr_t s = (va + 0x7fffffu) & ~(uintptr_t)0x7fffffu; s + 0x800000u <= va + size && n < 8; s += 0x800000u, n++) {
                    uint32_t p = pa + (uint32_t)(s - va);
                    pb[4 + 4 * n] = (uint32_t)s;                                        /* EntryHi: VPN2, ASID ignored (G) */
                    pb[5 + 4 * n] = ((p >> 12) << 6) | (3u << 3) | 4u | 2u | 1u;         /* Lo0: PFN, C=3, D, V, G */
                    pb[6 + 4 * n] = (((p + 0x400000u) >> 12) << 6) | (3u << 3) | 4u | 2u | 1u;
                    pb[7 + 4 * n] = 0x007fe000u;                                        /* PageMask 4 MB */
                }
                /* pointer chain inside the wired part of the pool */
                uint32_t *ch = (uint32_t *)((va + 0x7fffffu) & ~(uintptr_t)0x7fffffu);
                uint32_t words = n * (0x800000u / 4), s = 777;
                say("TLB test: pool pa 0x%08x va 0x%08lx, %u wired pairs of 4 MB pages (%u MB)\n", pa, (unsigned long)va, n, n * 8);
                if (n) {
                    for (uint32_t i = 0; i < words; i++) ch[i] = i;
                    for (uint32_t i = words - 1; i > 0; i--) { s = s * 1103515245u + 12345u; uint32_t j = s % i; uint32_t x = ch[i]; ch[i] = ch[j]; ch[j] = x; }
                    double t0, t; uint32_t pz;
                    pz = 0; t0 = now(); for (int i = 0; i < 2000000; i++) pz = ch[pz]; t = now() - t0; sink += pz;
                    say("  chase %u MB, 4 KB pages: %.1f ns per load\n", n * 8, t / 2e6 * 1e9);
                    pb[0] = 1; pb[1] = n; pb[2] = 0x007fe000u;                         /* PageMask 4 MB */
                    if (run_payload(dir, "tlbwire.bin", 0x10000) == 0 && pb[0xC8 / 4] == 0x424C5457u) {
                        uint32_t wb = pb[0xC0 / 4];
                        say("  wired: Wired %u -> %u\n", wb, pb[0xC4 / 4]);
                        pz = 0; t0 = now(); for (int i = 0; i < 2000000; i++) pz = ch[pz]; t = now() - t0; sink += pz;
                        say("  chase %u MB, 4 MB wired pages: %.1f ns per load\n", n * 8, t / 2e6 * 1e9);
                        uint32_t sum = 0; for (uint32_t i = 0; i < words; i += 1024) sum += ch[i];
                        say("  data check through the wired mapping: sum 0x%08x\n", sum);
                        memset((void *)pb, 0, 0x100);
                        pb[0] = 2; pb[1] = n; pb[3] = wb;
                        if (run_payload(dir, "tlbwire.bin", 0x10000) == 0 && pb[0xC8 / 4] == 0x424C5457u)
                            say("  removed: Wired %u -> %u\n", pb[0xC0 / 4], pb[0xC4 / 4]);
                        else say("  WARNING: remove did not report (a reboot clears it)\n");
                        pz = 0; t0 = now(); for (int i = 0; i < 2000000; i++) pz = ch[pz]; t = now() - t0; sink += pz;
                        say("  chase after removal: %.1f ns per load\n", t / 2e6 * 1e9);
                    } else say("  install did not report\n");
                }
                munmap(pool, size);
            } else say("TLB test: fixed mapping failed\n");
            ioctl(fd2, 0xC0101902u, rq2);
        } else say("TLB test: 32 MB mmz block not available\n");
        if (fd2 >= 0) close(fd2);
    }
    if (!cfg7(dir, c7, 0xffffffffu, &o, &n)) say("final Config7 0x%08x (stock 0x%08x)\n", n, c7);
    say("poll slot at exit 0x%08x\ndone\n", *slot);
    munmap(blk, 1u << 20); ioctl(mz, 0xC0101902u, rq); close(mz);
    return 0;
}
