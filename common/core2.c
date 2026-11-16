/* core2 Linux side. See core2.h. */
#define _GNU_SOURCE
#include "core2.h"
#include "stepslog.h"
#include "c2obj.h"
#include "../tools/device/core2/core2_abi.h"
#include "../tools/device/core2/tramp.h"
#include <stdio.h>
#include <stdlib.h>
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
#include <poll.h>

#define C2_BLOCK (8u << 20)                /* shared block: mailbox, code, job data */

static int c2_mz = -1, c2_ok;
static uint32_t c2_rq[4];
static uint8_t *c2_blk;
static uint32_t c2_base;                   /* second-core address of c2_blk */
static size_t c2_used;
static size_t c2_size = C2_BLOCK;        /* bytes of the shared block */
static int c2_external;                    /* the block belongs to the caller (core2_start_at) */
static volatile struct c2_mailbox *c2_mb;
static uint32_t c2_req;
static char c2_text[8192];

static void c2_say(const char *f, ...)
{
    size_t n = strlen(c2_text);
    va_list ap; va_start(ap, f);
    if (n < sizeof c2_text - 1) vsnprintf(c2_text + n, sizeof c2_text - n, f, ap);
    va_end(ap);
    steps_log("core2: %s", c2_text + n);
    const char *lp = getenv("R36_CORE2_LOG");        /* step-by-step log that survives a freeze */
    if (lp) {
        FILE *lf = fopen(lp, "a");
        if (lf) { fputs(c2_text + n, lf); fflush(lf); fsync(fileno(lf)); fclose(lf); }
    }
}
const char *core2_log(void) { return c2_text; }

static int c2_vu;                          /* defined below (tentative definition) */
/* text the second core printed since the last call (its console); empty when nothing */
const char *core2_console(void)
{
    static char out[8192];
    size_t n = 0;
    out[0] = 0;
    if (c2_vu < 0) return out;
    for (;;) {
        struct pollfd pf = { c2_vu, POLLIN, 0 };
        if (poll(&pf, 1, 0) <= 0 || !(pf.revents & POLLIN) || n >= sizeof out - 1025) break;
        ssize_t r = read(c2_vu, out + n, 512);   /* the driver may copy 512 bytes whatever the length asked */
        if (r <= 0) break;
        n += (size_t)r;
    }
    out[n] = 0;
    return out;
}

static double c2_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

void core2_writeback(const void *p, size_t n) { if (n) cacheflush((void *)p, (int)n, BCACHE); }
void core2_invalidate(const void *p, size_t n) { if (n) cacheflush((void *)p, (int)n, BCACHE); }

static uint64_t c2_phys(void *va)
{
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) return 0;
    uint64_t e = 0;
    if (pread(fd, &e, 8, ((uintptr_t)va / 4096) * 8) != 8) e = 0;
    close(fd);
    if (!(e >> 63)) return 0;
    return (e & ((1ull << 55) - 1)) * 4096 + ((uintptr_t)va & 4095);
}

void *core2_alloc(size_t bytes)
{
    if (!c2_blk) return NULL;
    size_t a = (c2_used + 63) & ~(size_t)63;
    if (a + bytes > c2_size) return NULL;
    c2_used = a + bytes;
    return c2_blk + a;
}
uint32_t core2_addr(const void *p) { return c2_base + (uint32_t)((const uint8_t *)p - c2_blk); }

uint32_t core2_load(const void *code, size_t len)
{
    uint8_t *p = core2_alloc(len + 64);
    if (!p) return 0;
    memcpy(p, code, len);
    core2_writeback(p, len);
    return core2_addr(p);
}

/* ---- the firmware shell, only for the bootstrap ---- */
static int c2_vu = -1;
static char c2_rx[16384];
static size_t c2_rxn;
static int c2_shell(const char *cmd, const char *want, int ms, char *out, size_t outsz)
{
    char line[160]; snprintf(line, sizeof line, "%s\n", cmd);
    c2_rxn = 0; c2_rx[0] = 0;
    if (write(c2_vu, line, strlen(line)) < 0) return -1;
    double end = c2_now() + ms / 1000.0;
    while (c2_now() < end) {
        struct pollfd pf = { c2_vu, POLLIN, 0 };
        ssize_t n = 0;
        char b[1024];                          /* the virtuart driver copies up to 512 bytes whatever the length asked */
        if (poll(&pf, 1, 20) > 0 && (pf.revents & POLLIN)) n = read(c2_vu, b, 512);
        if (n > 0 && c2_rxn + n < sizeof c2_rx - 1) { memcpy(c2_rx + c2_rxn, b, n); c2_rxn += n; c2_rx[c2_rxn] = 0; }
        if (n <= 0) usleep(5000);
        if (want && strstr(c2_rx, want)) break;
    }
    if (out) snprintf(out, outsz, "%s", c2_rx);
    return 0;
}
static int __attribute__((unused)) c2_peek(uint32_t a, uint32_t *v)
{
    char c[48], o[1024]; snprintf(c, sizeof c, "mw %08x", a);
    c2_shell(c, "= 0x", 1500, o, sizeof o);
    const char *p = strstr(o, "= 0x");
    if (!p) { c2_say("no answer to '%s': [%s]\n", c, o); return -1; }
    *v = (uint32_t)strtoul(p + 2, NULL, 16);
    return 0;
}
static int __attribute__((unused)) c2_poke(uint32_t a, uint32_t v)
{
    char c[48]; snprintf(c, sizeof c, "mw %08x=%08x", a, v);
    c2_shell(c, "-> 0x", 1500, NULL, 0);
    uint32_t b;
    return (c2_peek(a, &b) || b != v) ? -1 : 0;
}

static int c2_read_file(const char *dir, const char *name, uint8_t *buf, size_t max)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, max, f);
    fclose(f);
    return n;
}

static int core2_boot(const char *dir);

int core2_start(const char *dir)
{
    c2_text[0] = 0;
    if (c2_ok) return 0;
    c2_external = 0; c2_size = C2_BLOCK;
    return core2_boot(dir);
}

/* use memory the caller owns: `mem` must be physically contiguous, `kseg0` its second-core address (0x80000000 | PA) */
int core2_start_at(void *mem, uint32_t kseg0, size_t size, const char *dir)
{
    c2_text[0] = 0;
    if (c2_ok) return 0;
    if (!mem || kseg0 < 0x80000000u || kseg0 >= 0x88000000u || size < (256u << 10)) { c2_say("bad memory for core2_start_at\n"); return -1; }
    c2_external = 1; c2_size = size;
    c2_blk = mem; c2_base = kseg0;
    return core2_boot(dir);
}

static int core2_boot(const char *dir)
{
    if (getenv("R36_CORE2") && !strcmp(getenv("R36_CORE2"), "0")) { c2_say("off by R36_CORE2=0\n"); return -1; }
    if (access("/mnt/sdcard/cubegm/core2_off", F_OK) == 0) { c2_say("off by cubegm/core2_off\n"); return -1; }
    static uint8_t boot[4096], worker[4096];
    int nb = c2_read_file(dir, "boot.bin", boot, sizeof boot), nw = c2_read_file(dir, "worker.bin", worker, sizeof worker);
    if (nb <= 0 || nw <= 0) { c2_say("boot.bin/worker.bin missing in %s\n", dir); return -1; }

    if (c2_external) { memset(c2_blk, 0, c2_size); goto have_block; }
    c2_mz = open("/dev/mmz", O_RDWR);
    if (c2_mz < 0) { c2_say("no /dev/mmz\n"); return -1; }
    c2_rq[0] = 0; c2_rq[1] = 0x20; c2_rq[2] = C2_BLOCK; c2_rq[3] = 0;
    if (ioctl(c2_mz, 0xC0101901u, c2_rq) < 0) { c2_say("mmz alloc failed: %s\n", strerror(errno)); close(c2_mz); c2_mz = -1; return -1; }
    c2_blk = mmap(NULL, C2_BLOCK, PROT_READ | PROT_WRITE, MAP_SHARED, c2_mz, 0);
    if (c2_blk == MAP_FAILED) { c2_blk = NULL; c2_say("mmap failed\n"); goto fail; }
    memset(c2_blk, 0, C2_BLOCK);               /* also faults every page in, so pagemap knows them */
    uint64_t ph = c2_phys(c2_blk), ph2 = c2_phys(c2_blk + C2_BLOCK - 4096);
    if (!ph && c2_rq[3] >= 0x80000000u && c2_rq[3] < 0x88000000u) {   /* pagemap cannot see mmz pages; the ioctl returns the KSEG0 address */
        ph = c2_rq[3] & 0x1fffffffu; ph2 = ph + C2_BLOCK - 4096;
    }
    if (!ph || ph2 - ph != C2_BLOCK - 4096 || ph >= 0x08000000u) { c2_say("block not contiguous (0x%llx..0x%llx)\n", (unsigned long long)ph, (unsigned long long)ph2); goto fail; }
    c2_base = 0x80000000u | (uint32_t)ph;
have_block:
    c2_used = 0;
    c2_mb = core2_alloc(sizeof(struct c2_mailbox));
    struct c2_boot *bt = core2_alloc(sizeof(struct c2_boot));
    uint32_t w = core2_load(worker, (size_t)nw), bfn = core2_load(boot, (size_t)nb);
    uint32_t key = (uint32_t)(c2_now() * 1e6) ^ (uint32_t)getpid() * 2654435761u;
    c2_mb->magic = C2_MAGIC; c2_mb->version = C2_VERSION; c2_mb->key = key; c2_mb->spin_ms = 0xffffffffu;   /* no busy-poll: sleep one tick when idle */
    bt->magic = C2_MAGIC; bt->worker = w; bt->mailbox = core2_addr((void *)c2_mb); bt->key = key;
    bt->prio = 1; bt->stack_words = 1024;
    core2_writeback(c2_blk, (size_t)c2_used);
    c2_say("block at 0x%08x, worker 0x%08x, boot 0x%08x\n", c2_base, w, bfn);

    c2_vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (c2_vu < 0) { c2_say("no /dev/virtuart: %s\n", strerror(errno)); goto fail; }
    c2_shell("", NULL, 300, NULL, 0);
    c2_shell("nsh", "nsh)#", 2000, NULL, 0);   /* mw lives in the NSH sub-shell */
    c2_say("shell ready, installing the entry jump\n");
    int tr = c2_tramp_install(bfn);
    if (tr) { c2_say("entry jump not installed (%d), nothing changed\n", tr); goto fail; }
    char cmd[48]; snprintf(cmd, sizeof cmd, "mv %08x 0", core2_addr(bt));
    c2_say("running %s\n", cmd);
    c2_shell(cmd, "nsh)#", 2000, NULL, 0);
    c2_say("bootstrap returned, removing the entry jump\n");
    if (c2_tramp_remove()) c2_say("WARNING: mv handler code not restored (a power cycle restores it)\n");
    for (int i = 0; i < 200; i++) {
        core2_invalidate((void *)c2_mb, 128); core2_invalidate(bt, sizeof *bt);
        if (bt->done && c2_mb->state == 1) break;
        usleep(10000);
    }
    /* keep the shell open: core2_console() shows what the second core prints (crash reports) */
    if (!bt->done || bt->result != 1 || c2_mb->state != 1) { c2_say("worker did not start (done %u result %d state %u)\n", bt->done, bt->result, c2_mb->state); goto fail_leave; }
    c2_req = c2_mb->ack;
    c2_ok = 1;
    c2_say("worker running\n");
    return 0;
fail:
    if (c2_vu >= 0) { close(c2_vu); c2_vu = -1; }
    if (!c2_external) {
        if (c2_blk) { munmap(c2_blk, C2_BLOCK); c2_blk = NULL; }
        ioctl(c2_mz, 0xC0101902u, c2_rq); close(c2_mz); c2_mz = -1;
    }
    return -1;
fail_leave:
    /* keep the block: a worker might still start later and must not find reused memory */
    c2_mb->quit = 1; core2_writeback((void *)c2_mb, 64);
    return -1;
}

int core2_ready(void) { return c2_ok; }

int core2_busy(void)
{
    core2_invalidate((const uint8_t *)c2_mb + 64, 64);
    return c2_mb->ack != c2_req;
}

int core2_run(uint32_t fn, uint32_t arg)
{
    if (!c2_ok || core2_busy()) return -1;
    c2_mb->fn = fn; c2_mb->arg = arg;
    c2_mb->code_lo = fn; c2_mb->code_hi = fn + 4096;
    c2_mb->req = ++c2_req;
    core2_writeback((void *)c2_mb, 64);
    return 0;
}

int core2_wait(int timeout_ms)
{
    double end = c2_now() + timeout_ms / 1000.0;
    for (;;) {
        if (!core2_busy()) return 0;
        if (c2_now() > end) return -1;
    }
}

void core2_stop(void)
{
    if (!c2_mb) return;
    if (c2_ok) core2_wait(1000);
    c2_mb->quit = 1;
    core2_writeback((void *)c2_mb, 64);
    for (int i = 0; i < 100; i++) { core2_invalidate((const uint8_t *)c2_mb + 64, 64); if (c2_mb->state == 2) break; usleep(2000); }
    int stopped = c2_mb->state == 2;
    c2_ok = 0;
    if (stopped && c2_blk && !c2_external) { munmap(c2_blk, C2_BLOCK); c2_blk = NULL; ioctl(c2_mz, 0xC0101902u, c2_rq); close(c2_mz); c2_mz = -1; }
    /* not stopped: keep the block mapped until exit so the worker never reads reused memory */
}

/* Place a relocatable object (c2link.py) in the shared block at a 64 KB aligned address; entries[] gets the second-core
   addresses of the entry symbols. Returns 0, or a negative code. */
int core2_load_obj(const void *obj, size_t len, uint32_t *entries, unsigned max_entries)
{
    struct c2obj_info in;
    int r = c2obj_info_get(obj, len, &in);
    if (r) return r;
    size_t a = (c2_used + 0xffffu + (c2_base & 0xffffu)) & ~(size_t)0xffffu;
    /* the block address itself may not be 64 KB aligned: align the second-core address, not the offset */
    uint32_t base = (c2_base + (uint32_t)c2_used + 0xffffu) & ~0xffffu;
    a = base - c2_base;
    if (a + in.total_size > c2_size) return -6;
    c2_used = a + in.total_size;
    r = c2obj_apply(c2_blk + a, base, obj, len, entries, max_entries);
    if (r) return r;
    core2_writeback(c2_blk + a, in.total_size);
    return 0;
}
