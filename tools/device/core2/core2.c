/* core2: run a small job on the second CPU core (the core of the video/audio firmware) and measure it.
 * Runtime only: nothing is written to the card's boot files and everything is undone when the program ends; a reboot
 * also undoes it. How it works:
 *  1. Allocate a block from /dev/mmz (memory the second core can reach), find its physical address (/proc/self/pagemap).
 *     The second core sees RAM at 0x80000000 | physical (cached, KSEG0).
 *  2. Copy job.bin (tools/device/core2/job.c, position independent) into the block, plus a control block.
 *     cacheflush() writes the data back to RAM.
 *  3. Through /dev/virtuart (the firmware shell): read the handler of the shell command "pwd" with "mw ADDR",
 *     check it is the expected stock value, point it at the job with "mw ADDR=JOB", run "pwd CB", put it back.
 *  4. Wait for the job's done flag and report the second core's speed.
 * Usage: core2 <report> <dir with job.bin> [add iterations] [mem bytes]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/cachectl.h>

#define CB_MAGIC 0x43325231u
#include "tramp.h"

struct cb {
    uint32_t magic, cmd, n, buf, done, c0_start, c0_end, t_start, t_end, result, prid, config, config1, config7, pad[2];
};

static FILE *rep;
static void say(const char *f, ...)
{
    va_list ap; va_start(ap, f); vfprintf(rep, f, ap); va_end(ap);
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    fflush(rep); fsync(fileno(rep));
}

/* ---- firmware shell over /dev/virtuart ---- */
static int vu = -1;
static char rxbuf[65536];
static volatile size_t rxlen;
static pthread_mutex_t rxm = PTHREAD_MUTEX_INITIALIZER;
static volatile int stop_rx;

static void *rx_thread(void *a)
{
    (void)a;
    char b[512];
    while (!stop_rx) {
        ssize_t n = read(vu, b, sizeof b);
        if (n <= 0) { usleep(10000); continue; }
        pthread_mutex_lock(&rxm);
        if (rxlen + n < sizeof rxbuf - 1) { memcpy(rxbuf + rxlen, b, n); rxlen += n; rxbuf[rxlen] = 0; }
        pthread_mutex_unlock(&rxm);
    }
    return NULL;
}

/* send a command, wait up to ms for `want` to appear in the new output; returns pointer into a copy of the output */
static char out_copy[65536];
static const char *shell(const char *cmd, const char *want, int ms)
{
    size_t start;
    pthread_mutex_lock(&rxm); start = rxlen; pthread_mutex_unlock(&rxm);
    char line[256]; snprintf(line, sizeof line, "%s\n", cmd);
    if (write(vu, line, strlen(line)) < 0) say("write to virtuart failed: %s\n", strerror(errno));
    for (int t = 0; t < ms; t += 20) {
        usleep(20000);
        pthread_mutex_lock(&rxm);
        size_t n = rxlen - start;
        memcpy(out_copy, rxbuf + start, n); out_copy[n] = 0;
        pthread_mutex_unlock(&rxm);
        if (want && strstr(out_copy, want)) break;
    }
    return out_copy;
}

static int read_word(uint32_t addr, uint32_t *val)
{
    char c[64]; snprintf(c, sizeof c, "mw %08x", addr);
    const char *o = shell(c, "= 0x", 1500);
    const char *p = strstr(o, "= 0x");
    say("  > %s\n  < %s\n", c, o);
    if (!p) return -1;
    *val = (uint32_t)strtoul(p + 2, NULL, 16);
    return 0;
}

static int write_word(uint32_t addr, uint32_t v)
{
    char c[64]; snprintf(c, sizeof c, "mw %08x=%08x", addr, v);
    const char *o = shell(c, "-> 0x", 1500);
    say("  > %s\n  < %s\n", c, o);
    uint32_t back;
    if (read_word(addr, &back) || back != v) return -1;
    return 0;
}

/* ---- physical address of a mapped page ---- */
static uint64_t phys_of(void *va)
{
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) return 0;
    uint64_t e = 0, off = ((uintptr_t)va / 4096) * 8;
    if (pread(fd, &e, 8, off) != 8) e = 0;
    close(fd);
    if (!(e >> 63)) return 0;                /* not present */
    return (e & ((1ull << 55) - 1)) * 4096 + ((uintptr_t)va & 4095);
}

/* ---- second-core clock (HC1600, boot loader FUN_89db8c68): mux 0x1880009c bits 10:8, bit 19 = PLL source,
        applied by bit 22 of 0x18800074 ---- */
static volatile uint32_t *sysreg(void)
{
    static volatile uint32_t *m;
    if (!m) {
        int fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (fd < 0) return NULL;
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x18800000);
        close(fd);
        if (p == MAP_FAILED) return NULL;
        m = p;
    }
    return m;
}
static uint32_t scpu_mux_read(void) { volatile uint32_t *m = sysreg(); return m ? m[0x9c / 4] : 0; }
static uint32_t scpu_mux_set(int src)
{
    volatile uint32_t *m = sysreg();
    if (!m) return 0;
    uint32_t v = (m[0x9c / 4] & ~0x700u & ~0x80000u) | ((uint32_t)(src & 7) << 8) | (src == 7 ? 0x80000u : 0);
    m[0x9c / 4] = v; __sync_synchronize();
    m[0x74 / 4] |= 0x400000u; __sync_synchronize();
    return m[0x9c / 4];
}
static void scpu_mux_restore(uint32_t v)
{
    volatile uint32_t *m = sysreg();
    if (!m) return;
    m[0x9c / 4] = v; __sync_synchronize();
    m[0x74 / 4] |= 0x400000u; __sync_synchronize();
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/mnt/sdcard/core2_report.txt";
    const char *dir = argc > 2 ? argv[2] : ".";
    uint32_t iters = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 200000000u;
    uint32_t membytes = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 4u << 20;
    rep = fopen(out, "w");
    if (!rep) return 1;
    say("core2 proof of concept\n");

    char jp[512]; snprintf(jp, sizeof jp, "%s/job.bin", dir);
    FILE *jf = fopen(jp, "rb");
    if (!jf) { say("missing %s\n", jp); return 0; }
    static uint8_t job[16384]; size_t jlen = fread(job, 1, sizeof job, jf); fclose(jf);
    say("job.bin %zu bytes\n", jlen);

    /* 1. memory the second core can reach */
    int mz = open("/dev/mmz", O_RDWR);
    if (mz < 0) { say("cannot open /dev/mmz: %s\n", strerror(errno)); return 0; }
    uint32_t size = 64 * 1024 + membytes;
    size = (size + 0xfffff) & ~0xfffffu;
    uint32_t rq[4] = { 0, 0x20, size, 0 };
    if (ioctl(mz, 0xC0101901u, rq) < 0) { say("mmz alloc %u failed: %s\n", size, strerror(errno)); return 0; }
    uint8_t *blk = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mz, 0);
    if (blk == MAP_FAILED) { say("mmap failed\n"); ioctl(mz, 0xC0101902u, rq); return 0; }
    memset(blk, 0, size);
    uint64_t ph = phys_of(blk), ph_end = phys_of(blk + size - 4096);
    if (!ph && rq[3] >= 0x80000000u && rq[3] < 0x88000000u) {   /* pagemap cannot see mmz pages; the ioctl returns the KSEG0 address */
        ph = rq[3] & 0x1fffffffu; ph_end = ph + size - 4096;
    }
    say("mmz block %u KB at %p, physical 0x%llx .. 0x%llx (ioctl words %08x %08x %08x %08x)\n", size >> 10, (void *)blk,
        (unsigned long long)ph, (unsigned long long)ph_end, rq[0], rq[1], rq[2], rq[3]);
    if (!ph || ph_end - ph != size - 4096 || ph >= 0x08000000u) { say("block not contiguous or address unknown, stop\n"); goto out_free; }
    uint32_t c2 = 0x80000000u | (uint32_t)ph;             /* second-core address of the block */
    struct cb *cb = (struct cb *)blk;                     /* control block at offset 0 */
    uint32_t job_off = 4096, buf_off = 64 * 1024;
    memcpy(blk + job_off, job, jlen);
    cb->magic = CB_MAGIC;
    cb->cmd = 1; cb->n = iters; cb->buf = c2 + buf_off;
    cacheflush(blk, size, BCACHE);

    /* 2. shell link */
    vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (vu < 0) { say("cannot open /dev/virtuart: %s\n", strerror(errno)); goto out_free; }
    pthread_t th; pthread_create(&th, NULL, rx_thread, NULL);
    shell("", NULL, 500);
    say("  > top\n  < %s\n", shell("top", "#", 3000));      /* second core CPU load since start (OS shell) */
    say("  > nsh\n  < %s\n", shell("nsh", "nsh)#", 2000));  /* mw lives in the NSH sub-shell */

    /* second-core clock sources to try (mux 0x1880009c bits 10:8; 7 = its PLL at 800 MHz, 4 = 900 MHz preset,
       5 = 1188 MHz preset); the stock source is always put back at the end */
    static const int srcs[3] = { 7, 4, 5 };
    int nsrc = getenv("C2_CLOCKS") ? atoi(getenv("C2_CLOCKS")) : 1;
    if (nsrc < 1) nsrc = 1; if (nsrc > 3) nsrc = 3;
    uint32_t mux_stock = scpu_mux_read();
    say("second-core clock mux 0x%08x (source %u)\n", mux_stock, (mux_stock >> 8) & 7);
    for (int ci = 0; ci < nsrc; ci++) {
    if (ci > 0) {
        uint32_t m = scpu_mux_set(srcs[ci]);
        say("== second core switched to source %d (%s): mux 0x%08x\n", srcs[ci], srcs[ci] == 4 ? "900 MHz" : "1188 MHz", m);
        usleep(200000);
    }
    for (int pass = 0; pass < 2; pass++) {
        memset(cb, 0, sizeof *cb);
        cb->magic = CB_MAGIC;
        cb->cmd = pass == 0 ? 1 : 2;
        cb->n = pass == 0 ? iters : membytes;
        cb->buf = c2 + buf_off;
        cacheflush(blk, 4096, BCACHE);
        int tr = c2_tramp_install(c2 + job_off);
        if (tr) { say("could not install the entry jump (%d: %s), nothing changed\n", tr, tr == -2 ? "unexpected firmware code" : "/dev/mem"); goto out_close; }
        say("entry jump installed at 0x%08x -> 0x%08x\n", 0x80000000u | C2_MV_PHYS, c2 + job_off);
        char c[64]; snprintf(c, sizeof c, "mv %08x 0", c2);
        double t0 = now();
        say("  > %s\n", c);
        const char *o = shell(c, "nsh)#", 3000);
        say("  < mv reply: [%s]\n", o);
        int ok = 0;
        for (int i = 0; i < 1500; i++) {                  /* up to 30 s */
            cacheflush(blk, 4096, BCACHE);                /* drop our cached copy of the block */
            if (((volatile struct cb *)cb)->done == 1) { ok = 1; break; }
            usleep(20000);
        }
        double t1 = now();
        (void)o;
        if (c2_tramp_remove()) say("WARNING: could not restore the mv handler code (a power cycle restores it)\n");
        else say("mv handler code restored\n");
        say("  < shell output: %s\n", out_copy);
        if (!ok) { say("pass %d: no answer from the second core within 30 s\n", pass); break; }
        uint32_t cyc = cb->c0_end - cb->c0_start;
        uint32_t ms0 = (cb->t_start >> 16) * 1000 + (cb->t_start & 0xffff), ms1 = (cb->t_end >> 16) * 1000 + (cb->t_end & 0xffff);
        uint32_t dms = ms1 - ms0;
        say("pass %d (cmd %u, n %u): result 0x%08x, cp0 count %u, hardware timer %u ms, Linux wall %.3f s\n",
            pass, cb->cmd, cb->n, cb->result, cyc, dms, t1 - t0);
        say("  second core: PRId 0x%08x Config 0x%08x Config1 0x%08x Config7 0x%08x\n", cb->prid, cb->config, cb->config1, cb->config7);
        if (dms) {
            if (cb->cmd == 1) say("  add loop: %.1f M iterations/s on the second core (cp0 count rate %.1f MHz)\n", cb->n / (dms * 1000.0), cyc / (dms * 1000.0));
            else say("  memory: %.1f MB/s write+read on the second core\n", 2.0 * cb->n / (dms * 1000.0));
        }
        if (cb->cmd == 2) {
            uint32_t s = 0, *w = (uint32_t *)(blk + buf_off);
            cacheflush(blk + buf_off, membytes, BCACHE);
            for (uint32_t i = 0; i < membytes / 4; i++) { s += w[i]; if (w[i] != i * 2654435761u) { say("  buffer word %u wrong\n", i); break; } }
            say("  buffer check from Linux: sum 0x%08x (%s)\n", s, s == cb->result ? "matches" : "DIFFERS");
        }
    }
    }
    if (scpu_mux_read() != mux_stock) { scpu_mux_restore(mux_stock); say("second-core clock back to stock: mux 0x%08x\n", scpu_mux_read()); }
out_close:
    stop_rx = 1;
    if (vu >= 0) close(vu);
out_free:
    munmap(blk, size);
    ioctl(mz, 0xC0101902u, rq);
    close(mz);
    say("done\n");
    return 0;
}
