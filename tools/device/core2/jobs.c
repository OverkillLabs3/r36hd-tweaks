/* Test jobs for the second core. Position independent by construction (see build.sh).
 * job arg = second-core address of struct c2_test. */
#include <stdint.h>

struct c2_test {
    uint32_t op;      /* 0 nothing, 1 fill words with i*K, 2 sum words, 3 add loop */
    uint32_t buf;     /* second-core address */
    uint32_t words;
    uint32_t result;
    uint32_t pad[4];
};

static inline __attribute__((always_inline)) void inv_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16) __asm__ volatile("cache 0x11, 0(%0)" :: "r"(p) : "memory");
}
static inline __attribute__((always_inline)) void wb_d(uint32_t a, uint32_t len)
{
    for (uint32_t p = a & ~15u; p < a + len; p += 16) __asm__ volatile("cache 0x15, 0(%0)" :: "r"(p) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

void c2_test_job(uint32_t arg)
{
    inv_d(arg, sizeof(struct c2_test));
    struct c2_test *t = (struct c2_test *)arg;
    uint32_t r = 0;
    if (t->op == 1) {
        uint32_t *b = (uint32_t *)t->buf;
        for (uint32_t i = 0; i < t->words; i++) b[i] = i * 2654435761u;
        wb_d(t->buf, t->words * 4);
    } else if (t->op == 2) {
        inv_d(t->buf, t->words * 4);
        const uint32_t *b = (const uint32_t *)t->buf;
        for (uint32_t i = 0; i < t->words; i++) r += b[i];
    } else if (t->op == 3) {
        uint32_t x = 1;
        for (uint32_t i = 0; i < t->words; i++) __asm__ volatile("addu %0, %0, %1" : "+r"(x) : "r"(i));
        r = x;
    }
    t->result = r;
    wb_d(arg, sizeof(struct c2_test));
}
