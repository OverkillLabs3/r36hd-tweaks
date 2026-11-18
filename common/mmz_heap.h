/* mmz_heap: move the C heap of a libretro core into the console's idle video pool (/dev/mmz).
 *
 * The R36HD has 42 MB of normal RAM, about 28 MB free for a game. The video co-processor keeps a 52 MB pool
 * ("mmz") that games never touch. On the device /dev/mmz hands out up to 48 MB as one cached, CPU-usable block.
 * This module maps one block and makes glibc's malloc grow inside it (the __morecore hook). When the block is
 * full, glibc falls back to normal memory by itself, so nothing breaks if the pool is smaller or missing.
 *
 * Cores include common/mmz_heap.c directly from their main source file (no build-system change), then\n * call mmz_heap_install() once, early in retro_init(), after the core's own mallopt() calls.
 * Settings (environment or picoarch core option exported as environment):
 *   R36_MMZ_MB=0      turn it off (an empty file /mnt/sdcard/cubegm/mmz_off does the same)
 *   R36_MMZ_MB=n      largest block to try, in MB (default 48; it steps down by 4 MB until one is granted)
 *   R36_MMZ_FAKE=1    test mode: use ordinary anonymous memory instead of /dev/mmz (tests on a PC)
 *   R36_MMZ_LOG=path  append one status line to this file (default /mnt/sdcard/cubegm/logs/mmz_heap.log)
 */
#ifndef MMZ_HEAP_H
#define MMZ_HEAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the size of the block now used as heap in bytes, or 0 when the normal heap stays in use. */
size_t mmz_heap_install(void);

/* Bytes of the block currently handed to malloc, and its total size. Both 0 when not installed. */
size_t mmz_heap_used(void);
size_t mmz_heap_size(void);

/* Append "used/size" to the log file; call it from a periodic or exit hook if wanted. */
void mmz_heap_log_usage(const char *tag);

/* Remove the large-page TLB entries of the pool (registered with atexit; safe to call more than once). */
void mmz_heap_uninstall(void);

/* Keep `bytes` of the block out of malloc for the caller (call before mmz_heap_install). */
void mmz_heap_set_reserve(size_t bytes);
/* The reserved part (physically contiguous) and its second-core address (0x80000000 | PA); NULL when not available. */
void *mmz_heap_reserved(size_t *size, uint32_t *kseg0);
/* Second-core address of a pointer inside the block, 0 when the pointer is outside it or the address is unknown. */
uint32_t mmz_heap_kseg0(const void *p);

#ifdef __cplusplus
}
#endif

#endif
