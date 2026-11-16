/* Worker task on the second core: polls the mailbox, runs jobs, sleeps one tick when idle.
 * Position independent by construction (see build.sh): one function, firmware calls through constant addresses. */
#include "core2_abi.h"

typedef void (*task_delay_t)(uint32_t);
typedef void (*job_t)(uint32_t);

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
static inline __attribute__((always_inline)) uint32_t count(void)
{
    uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c;
}

void c2_worker(void *param)
{
    uint32_t mb = (uint32_t)param;
    volatile struct c2_mailbox *m = (volatile struct c2_mailbox *)mb;
    task_delay_t delay = (task_delay_t)C2_FW_TASK_DELAY;
    inv_d(mb, sizeof(struct c2_mailbox));
    uint32_t key = m->key, done = m->req, alive = 0, idle_since = count();
    m->ack = done; m->state = 1; m->jobs = 0;
    wb_d(mb + 64, 64);
    for (;;) {
        inv_d(mb, 64);
        if (m->magic != C2_MAGIC || m->key != key || m->quit) {
            /* only an orderly quit gets an answer; a mailbox that changed under us (process gone, memory reused by
               someone else) is never written again */
            if (m->magic == C2_MAGIC && m->key == key) { m->state = 2; wb_d(mb + 64, 64); }
            for (;;) delay(100000);             /* stopped: never touch the mailbox again */
        }
        if (m->req != done) {
            uint32_t fn = m->fn, arg = m->arg, lo = m->code_lo, hi = m->code_hi, req = m->req;
            if (fn >= 0x80000000u && fn < 0x88000000u) {
                if (hi > lo && hi - lo <= (1u << 20)) inv_i(lo, hi - lo);
                uint32_t c0 = count();
                ((job_t)fn)(arg);
                m->c0_last = count() - c0;
            }
            done = req;
            m->ack = done; m->jobs = m->jobs + 1; m->alive = ++alive;
            wb_d(mb + 64, 64);
            idle_since = count();
            continue;
        }
        uint32_t spin = m->spin_ms;
        if (spin == 0xffffffffu) { m->alive = ++alive; wb_d(mb + 64, 64); delay(1); continue; }
        /* cp0 count runs at half the core clock: about 400 counts per microsecond at 800 MHz */
        if (count() - idle_since < spin * 400000u) continue;
        m->alive = ++alive; wb_d(mb + 64, 64);
        delay(1);
    }
}
