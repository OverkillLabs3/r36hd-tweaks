/* Bootstrap, runs once on the second core as the "pwd" shell handler: creates the worker task and returns.
 * Position independent by construction (see build.sh): one function, firmware calls through constant addresses. */
#include "core2_abi.h"

typedef int (*task_create_t)(void (*)(void *), const char *, uint32_t, void *, uint32_t, void *);

static inline __attribute__((always_inline)) void inv_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16) __asm__ volatile("cache 0x11, 0(%0)" :: "r"(p) : "memory");
}
static inline __attribute__((always_inline)) void wb_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16) __asm__ volatile("cache 0x15, 0(%0)" :: "r"(p) : "memory");
    __asm__ volatile("sync" ::: "memory");
}
static inline __attribute__((always_inline)) void inv_i(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16) __asm__ volatile("cache 0x10, 0(%0)" :: "r"(p) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

int c2_boot_entry(int argc, char **argv)
{
    /* drop the entry jump from the instruction cache: it is removed from RAM right after this call */
    __asm__ volatile("cache 0x10, 0(%0)" :: "r"(0x83e503a0u) : "memory");
    if (argc < 2) return -1;
    uint32_t a = 0;
    for (const char *s = argv[1]; *s; s++) {
        char c = *s; uint32_t v;
        if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else return -2;
        a = (a << 4) | v;
    }
    if ((a & 31) || a < 0x80000000u || a >= 0x88000000u) return -3;
    inv_d(a, sizeof(struct c2_boot));
    struct c2_boot *b = (struct c2_boot *)a;
    if (b->magic != C2_MAGIC || b->worker < 0x80000000u || b->mailbox < 0x80000000u) return -4;
    inv_i(b->worker, 4096);                      /* the worker code was just written by Linux */
    volatile uint32_t name[2];          /* "core2", built in registers: a string literal would need a relocation */
    name[0] = 0x65726f63u; name[1] = 0x32u;  /* copied by xTaskCreate into the task control block */
    task_create_t create = (task_create_t)C2_FW_TASK_CREATE;
    b->result = create((void (*)(void *))b->worker, (const char *)name, b->stack_words, (void *)b->mailbox, b->prio, 0);
    b->done = 1;
    wb_d(a, sizeof(struct c2_boot));
    return 0;
}
