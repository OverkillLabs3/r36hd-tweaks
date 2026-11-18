/* core2: run jobs on the second CPU core of the R36HD (the core of the video/audio firmware, about 800 MHz, with FPU).
 * Runtime only; nothing on the card changes. See tools/device/core2/ and section 8 of docs/KERNEL_ANALYSIS.md.
 *
 *   if (core2_start(dir) == 0) {                    // dir holds boot.bin and worker.bin
 *       uint32_t fn = core2_load(job_bin, job_len);  // copy a position-independent job blob, returns its entry
 *       void *p = core2_alloc(bytes);                // memory both cores reach (from the shared block)
 *       ... fill p, then core2_writeback(p, bytes);
 *       core2_run(fn, core2_addr(p));                 // the job gets the second-core address of p
 *       ... do other work on Linux ...
 *       core2_wait(1000);  core2_invalidate(out, n); // then read the results
 *   }
 *   core2_stop();
 * Jobs: void job(uint32_t arg), built like tools/device/core2/jobs.c (no relocations), they must invalidate their
 * inputs (cache 0x11) and write back their outputs (cache 0x15) themselves.
 */
#ifndef CORE2_H
#define CORE2_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int core2_start(const char *dir);              /* 0 = worker running */
/* same, in memory the caller owns (physically contiguous; kseg0 = 0x80000000 | its physical address) */
int core2_start_at(void *mem, uint32_t kseg0, size_t size, const char *dir);
void core2_stop(void);
int core2_ready(void);
uint32_t core2_load(const void *code, size_t len);
int core2_load_obj(const void *obj, size_t len, uint32_t *entries, unsigned max_entries);   /* relocatable .c2o, any C */
void *core2_alloc(size_t bytes);               /* 64-byte aligned, from the shared block; NULL when full */
uint32_t core2_addr(const void *p);            /* second-core address of a pointer inside the shared block */
int core2_run(uint32_t fn, uint32_t arg);      /* 0 = submitted; one job at a time */
int core2_wait(int timeout_ms);                /* 0 = finished */
int core2_busy(void);
void core2_writeback(const void *p, size_t n); /* make Linux writes visible to the second core */
void core2_invalidate(const void *p, size_t n);/* drop Linux's cached copy before reading second-core output */
const char *core2_log(void);                   /* text of the last start attempt */
const char *core2_console(void);               /* what the second core printed since the last call */
#ifdef __cplusplus
}
#endif
#endif
