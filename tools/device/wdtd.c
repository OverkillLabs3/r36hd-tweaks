/* wdtd: hardware watchdog for the console (registers at 0x18818500, found in the firmware's own code: reload value
 * -(ms * 27000 >> 7) at +0x00 (the counter runs at 27 MHz / 128), control 0x26 at +0x04 arms it, 0 disarms).
 *   wdtd start <ms>   fork a feeder that rewrites the registers every ms/3; if the feeder dies or the console hangs,
 *                      the SoC resets after <ms> milliseconds
 *   wdtd stop         stop the feeder and disarm
 *   wdtd arm <ms>     arm and exit without feeding: the console resets after <ms> (test of the reset path)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>

static volatile uint32_t *r;
static void arm(unsigned ms) { r[0] = (uint32_t)(-(int32_t)(((uint64_t)ms * 27000u) >> 7)); r[1] = 0x26; }
int main(int argc, char **argv)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    uint8_t *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x18818000);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    r = (volatile uint32_t *)(m + 0x500);
    if (argc >= 2 && !strcmp(argv[1], "stop")) {
        FILE *pf = fopen("/tmp/wdtd.pid", "r");
        if (pf) { int pid = 0; if (fscanf(pf, "%d", &pid) == 1 && pid > 1) kill(pid, SIGKILL); fclose(pf); remove("/tmp/wdtd.pid"); }
        r[1] = 0;
        printf("disarmed, control=%08x\n", r[1]);
        return 0;
    }
    unsigned ms = argc >= 3 ? (unsigned)atoi(argv[2]) : 20000;
    if (argc >= 2 && !strcmp(argv[1], "arm")) { arm(ms); printf("armed %u ms, no feeder\n", ms); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "start")) {
        if (fork()) { printf("watchdog armed, %u ms, feeder running\n", ms); return 0; }
        setsid();
        { FILE *pf = fopen("/tmp/wdtd.pid", "w"); if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); } }
        /* let go of the caller's stdio: the console agent reads a command's output until every holder of the pipe has
           closed it, so a feeder that keeps it open blocks the agent for good */
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
        for (;;) { arm(ms); usleep(ms * 1000 / 3); }
    }
    fprintf(stderr, "usage: wdtd start|arm <ms> | stop\n");
    return 1;
}
