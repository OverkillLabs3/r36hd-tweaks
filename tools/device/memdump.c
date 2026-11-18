/* memdump: copy a physical memory range to a file through mmap of /dev/mem (read() is refused outside Linux RAM).
 *   memdump <phys hex> <length hex> <out file>
 * Page by page with a bus-error guard; stops at the first faulting page and reports it.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
static sigjmp_buf jb;
static void on_fault(int s) { siglongjmp(jb, s); }
int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    uint32_t pa = (uint32_t)strtoul(argv[1], NULL, 16) & ~4095u, len = (uint32_t)strtoul(argv[2], NULL, 16);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_fault;
    sigaction(SIGBUS, &sa, NULL); sigaction(SIGSEGV, &sa, NULL);
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    FILE *o = fopen(argv[3], "wb");
    if (fd < 0 || !o) { perror("open"); return 1; }
    uint32_t done = 0;
    while (done < len) {
        uint32_t n = len - done > 0x100000 ? 0x100000 : len - done;
        volatile uint8_t *m = mmap(NULL, n, PROT_READ, MAP_SHARED, fd, pa + done);
        if (m == MAP_FAILED) { printf("mmap failed at %08x\n", pa + done); break; }
        static uint8_t buf[4096];
        int ok = 1;
        for (uint32_t k = 0; k < n && ok; k += 4096) {
            if (sigsetjmp(jb, 1) == 0) { for (int i = 0; i < 4096; i += 4) *(uint32_t *)(buf + i) = *(volatile uint32_t *)(m + k + i); }
            else { printf("fault at %08x\n", pa + done + k); ok = 0; break; }
            fwrite(buf, 1, 4096, o);
        }
        munmap((void *)m, n);
        if (!ok) break;
        done += n;
    }
    fclose(o);
    printf("wrote to %08x\n", pa + done);
    return 0;
}
