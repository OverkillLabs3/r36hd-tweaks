/* Shared layout between Linux (common/core2.c) and the second-core worker (tools/device/core2/worker.c).
 * All addresses inside are second-core addresses: 0x80000000 | physical (cached KSEG0). */
#ifndef CORE2_ABI_H
#define CORE2_ABI_H
#include <stdint.h>

#define C2_MAGIC 0x43325257u            /* "WR2C" */
#define C2_VERSION 1

/* firmware addresses (ApplicationFrame.dll, decrypted md5 starts d2921c30) */
#define C2_FW_TASK_CREATE 0x83de80d0u   /* xTaskCreate(fn, name, stack_words, param, prio, handle*) -> 1 */
#define C2_FW_TASK_DELAY  0x83dea58cu   /* vTaskDelay(ticks) */
#define C2_FW_PWD_SLOT    0x84028ea8u   /* handler word of the shell command "pwd" */
#define C2_FW_PWD_STOCK   0x83e50d08u

/* one 64-byte cache line per direction so neither core writes a line the other one writes */
struct c2_mailbox {
    /* line 0: written by Linux */
    uint32_t magic, version, key;       /* key: random per session, the worker ignores a mailbox with another key */
    uint32_t req;                        /* incremented by Linux for every job */
    uint32_t fn, arg;                    /* job entry (second-core address) and its argument */
    uint32_t code_lo, code_hi;           /* range to invalidate in the instruction cache before the call */
    uint32_t quit;                       /* 1: the worker stops polling (sleeps forever) */
    uint32_t spin_ms;                    /* busy-poll time after a job before sleeping 1 tick (default 4) */
    uint32_t pad0[6];
    /* line 1: written by the worker */
    uint32_t ack;                        /* = req when the job finished */
    uint32_t alive;                      /* counts up while the worker runs */
    uint32_t jobs;                       /* jobs done */
    uint32_t c0_last;                    /* cp0 count spent in the last job */
    uint32_t state;                      /* 1 running, 2 stopped */
    uint32_t pad1[11];
};

/* argument block of the bootstrap (passed as "mv <hex>", see tramp.h), lives in the same mmz block */
struct c2_boot {
    uint32_t magic;                      /* C2_MAGIC */
    uint32_t worker;                     /* entry of worker.bin */
    uint32_t mailbox;                    /* struct c2_mailbox */
    uint32_t key;
    uint32_t prio;                       /* FreeRTOS priority of the worker task (0 lowest, 31 highest) */
    uint32_t stack_words;
    int32_t result;                      /* written by the bootstrap: xTaskCreate return value */
    uint32_t done;
};
#endif
