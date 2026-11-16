/* Code that runs on the second CPU core (the core of the video/audio firmware).
 * It is entered through the firmware shell: the handler pointer of the shell command "pwd" is pointed at core2_entry
 * for one call, and "pwd <hex address>" passes the address of the control block (a second-core address, 0x80000000 |
 * physical). Built position independent by construction: one function, everything inlined, no globals, no tables,
 * no calls (tools/device/core2/build.sh checks the object for jal/lui).
 * The block lives in memory that Linux also maps (an mmz block); the two cores do not share caches, so the block is
 * invalidated before reading and written back after writing.
 */
#include <stdint.h>

#define CB_MAGIC 0x43325231u   /* "1R2C" */

struct cb {                      /* control block, 64-byte aligned, shared with tools/device/core2/core2.c */
    uint32_t magic;
    uint32_t cmd;                /* 1 = add loop benchmark, 2 = memory read/write benchmark */
    uint32_t n;                  /* iterations or bytes */
    uint32_t buf;                /* second-core address of a work buffer (cmd 2) */
    uint32_t done;               /* set to 1 when finished */
    uint32_t c0_start, c0_end;   /* cp0 Count before and after */
    uint32_t t_start, t_end;     /* 24 MHz south-bridge timer: u32 seconds << 16 | u16 ms, before and after */
    uint32_t result;
    uint32_t prid, config, config1, config7;
    uint32_t pad[2];
};

static inline __attribute__((always_inline)) void inv_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16)
        __asm__ volatile("cache 0x11, 0(%0)" :: "r"(p) : "memory");    /* Hit_Invalidate_D */
}
static inline __attribute__((always_inline)) void wb_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16)
        __asm__ volatile("cache 0x15, 0(%0)" :: "r"(p) : "memory");    /* Hit_Writeback_Inv_D */
    __asm__ volatile("sync" ::: "memory");
}
static inline __attribute__((always_inline)) uint32_t count(void)
{
    uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c;
}
static inline __attribute__((always_inline)) uint32_t hwtime(void)
{
    /* uncached KSEG1 view of the timer block at physical 0x18818a00 */
    volatile uint8_t *t = (volatile uint8_t *)(0xA0000000u | 0x18818000u);
    uint32_t s1, s2, ms;
    do { s1 = *(volatile uint32_t *)(t + 0xa0c); ms = *(volatile uint16_t *)(t + 0xa0a); s2 = *(volatile uint32_t *)(t + 0xa0c); } while (s1 != s2);
    return (s1 << 16) | (ms & 0xffff);
}

int core2_entry(int argc, char **argv)
{
    /* drop the entry jump from the instruction cache: it is removed from RAM right after this call */
    __asm__ volatile("cache 0x10, 0(%0)" :: "r"(0x83e503a0u) : "memory");
    if (argc < 2) return -1;
    uint32_t a = 0;
    for (const char *s = argv[1]; *s; s++) {
        char c = *s;
        if (c == 'x' || c == 'X') { a = 0; continue; }
        uint32_t v = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) : (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10) : 99;
        if (v == 99) return -2;
        a = (a << 4) | v;
    }
    if ((a & 63) || a < 0x80000000u || a >= 0x88000000u) return -3;
    inv_d(a, sizeof(struct cb));
    struct cb *cb = (struct cb *)a;
    if (cb->magic != CB_MAGIC) return -4;

    uint32_t v;
    __asm__ volatile("mfc0 %0, $15, 0" : "=r"(v)); cb->prid = v;
    __asm__ volatile("mfc0 %0, $16, 0" : "=r"(v)); cb->config = v;
    __asm__ volatile("mfc0 %0, $16, 1" : "=r"(v)); cb->config1 = v;
    __asm__ volatile("mfc0 %0, $16, 7" : "=r"(v)); cb->config7 = v;

    uint32_t r = 0;
    cb->t_start = hwtime();
    cb->c0_start = count();
    if (cb->cmd == 1) {
        uint32_t x = 1;
        for (uint32_t i = 0; i < cb->n; i++) {
            __asm__ volatile("addu %0, %0, %1" : "+r"(x) : "r"(i));   /* one dependent add per iteration */
        }
        r = x;
    } else if (cb->cmd == 2 && cb->buf >= 0x80000000u) {
        inv_d(cb->buf, cb->n);
        volatile uint32_t *b = (volatile uint32_t *)cb->buf;
        uint32_t words = cb->n / 4;
        for (uint32_t i = 0; i < words; i++) b[i] = i * 2654435761u;
        for (uint32_t i = 0; i < words; i++) r += b[i];
        wb_d(cb->buf, cb->n);
    }
    cb->c0_end = count();
    cb->t_end = hwtime();
    cb->result = r;
    cb->done = 1;
    wb_d(a, sizeof(struct cb));
    return 0;
}
