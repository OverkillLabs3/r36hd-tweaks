/* regscan: read-only dump of SoC register pages through /dev/mem.
 *   regscan <out dir> <phys addr hex> [<phys addr hex> ...]
 * Each address is one 4 KB page, written to <out dir>/<addr>.bin. Before each page "addr start" is logged and synced
 * to <out dir>/scan.log, after it "addr ok" (or "addr fault"), so after a bus hang the last "start" line names the page.
 * A bus error (SIGBUS) is caught; a hard hang needs a power cycle.
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
    if (argc < 3) return 1;
    char p[512];
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_fault;
    sigaction(SIGBUS, &sa, NULL); sigaction(SIGSEGV, &sa, NULL);
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    snprintf(p, sizeof p, "%s/scan.log", argv[1]);
    int lg = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
    for (int i = 2; i < argc; i++) {
        uint32_t a = (uint32_t)strtoul(argv[i], NULL, 16);
        char l[64]; int n = snprintf(l, sizeof l, "%08x start\n", a); write(lg, l, n); fsync(lg);
        volatile uint32_t *m = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, a);
        if (m == MAP_FAILED) { n = snprintf(l, sizeof l, "%08x mmap failed\n", a); write(lg, l, n); continue; }
        uint32_t buf[1024]; int ok = 1;
        if (sigsetjmp(jb, 1) == 0) { for (int k = 0; k < 1024; k++) buf[k] = m[k]; } else ok = 0;
        munmap((void *)m, 4096);
        if (ok) {
            snprintf(p, sizeof p, "%s/%08x.bin", argv[1], a);
            FILE *o = fopen(p, "wb"); if (o) { fwrite(buf, 1, 4096, o); fclose(o); }
        }
        n = snprintf(l, sizeof l, "%08x %s\n", a, ok ? "ok" : "fault"); write(lg, l, n); fsync(lg);
    }
    return 0;
}
