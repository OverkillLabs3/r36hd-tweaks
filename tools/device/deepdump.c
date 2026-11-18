/* deepdump: read-only dump of things that exist only on the console, for offline study.
 *   deepdump <out dir>
 * Writes into <out dir>:
 *   bootrom.bin   physical 0x1fc00000.. (the reset vector area: the SoC's boot ROM), stops at the first fault
 *   sram.bin      physical 0x1fe00000.. (the on-chip RAM the SPI boot stage runs from)
 *   sysregs.bin   physical 0x18800000..0x18801fff (system control, clocks, pin mux, DDR controller)
 *   fw_shell.txt  the second core's shell: OS "help", then "nsh" and its "help"
 * Nothing is written to the hardware: /dev/mem is opened read-only (except the shell, which only gets help commands).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

static sigjmp_buf jb;
static void on_fault(int s) { siglongjmp(jb, s); }

/* copy words from physical memory until `len` bytes or a bus fault; returns bytes copied */
static size_t dump_phys(int fd, uint32_t phys, size_t len, const char *path)
{
    FILE *o = fopen(path, "wb");
    if (!o) return 0;
    size_t done = 0;
    while (done < len) {
        uint32_t page = (phys + done) & ~4095u;
        volatile uint32_t *m = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, page);
        if (m == MAP_FAILED) break;
        uint32_t buf[1024];
        int ok = 1;
        if (sigsetjmp(jb, 1) == 0) {
            for (int i = 0; i < 1024; i++) buf[i] = m[i];
        } else ok = 0;
        munmap((void *)m, 4096);
        if (!ok) break;
        fwrite(buf, 1, 4096, o);
        done += 4096;
    }
    fclose(o);
    return done;
}

static void shell(FILE *o, int fd, const char *cmd)
{
    char buf[1024];
    fprintf(o, ">>> %s\n", cmd);
    if (*cmd) { if (write(fd, cmd, strlen(cmd)) < 0 || write(fd, "\r\n", 2) < 0) { fprintf(o, "write failed\n"); return; } }
    for (int idle = 0; idle < 8;) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 150) > 0 && (p.revents & POLLIN)) {
            ssize_t n = read(fd, buf, 512);              /* the driver may return up to 512 bytes whatever is asked */
            if (n > 0) { fwrite(buf, 1, (size_t)n, o); idle = 0; continue; }
        }
        idle++;
    }
    fprintf(o, "\n");
    fflush(o);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/mnt/sdcard/deep_dump";
    char p[512];
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_fault;
    sigaction(SIGBUS, &sa, NULL); sigaction(SIGSEGV, &sa, NULL);
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    /* the boot ROM window (0x1fc00000) froze the console on the first read (2026-11-17), so the boot ROM and the
     * on-chip RAM next to it are not read any more */
    snprintf(p, sizeof p, "%s/sysregs.bin", out); printf("sysregs: %zu bytes\n", dump_phys(fd, 0x18800000u, 0x2000u, p));
    close(fd);
    snprintf(p, sizeof p, "%s/fw_shell.txt", out);
    FILE *o = fopen(p, "w");
    int vu = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (o && vu >= 0) {
        shell(o, vu, "");
        shell(o, vu, "help");
        shell(o, vu, "mem");        /* "memory status entry": a command group */
        shell(o, vu, "help");
        shell(o, vu, "exit");
        shell(o, vu, "os");
        shell(o, vu, "help");
        shell(o, vu, "exit");
    } else if (o) fprintf(o, "no /dev/virtuart\n");
    if (vu >= 0) close(vu);
    if (o) fclose(o);
    printf("done\n");
    return 0;
}
