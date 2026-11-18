/* Records every change of the button word the OS shares through SysV shared memory (ftok("/tmp/joy_key", 'a'), 4 bytes,
   the same way TreeFrogUI's own apps read it), plus any /dev/input event devices.
   Run it for N seconds (default 60) and press the buttons slowly one at a time. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
    double secs = argc > 1 ? atof(argv[1]) : 60, t0 = now_s();
    volatile uint32_t *keys = 0;
    uint32_t prev = 0;
    int nev = 0, i, first = 1;
    struct pollfd pf[16];
    char names[16][64];
    setvbuf(stdout, 0, _IOLBF, 0);
    printf("button log for %.0f s\n", secs);
    {
        key_t k = ftok("/tmp/joy_key", 'a');
        int id = k == (key_t)-1 ? -1 : shmget(k, 4, 0666);
        void *m = id < 0 ? (void *)-1 : shmat(id, 0, SHM_RDONLY);
        printf("ftok=0x%x shmget=%d shmat=%s\n", (unsigned)k, id, m == (void *)-1 ? strerror(errno) : "ok");
        if (m != (void *)-1) keys = (volatile uint32_t *)m;
    }
    {
        DIR *d = opendir("/dev/input");
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)) && nev < 16) {
                if (strncmp(e->d_name, "event", 5)) continue;
                snprintf(names[nev], sizeof(names[nev]), "/dev/input/%s", e->d_name);
                pf[nev].fd = open(names[nev], O_RDONLY | O_NONBLOCK); pf[nev].events = POLLIN;
                if (pf[nev].fd >= 0) {
                    char name[128] = "?";
                    ioctl(pf[nev].fd, EVIOCGNAME(sizeof(name)), name);
                    printf("%s = '%s'\n", names[nev], name);
                    nev++;
                }
            }
            closedir(d);
        } else printf("/dev/input: %s\n", strerror(errno));
    }
    while (now_s() - t0 < secs) {
        if (keys) {
            uint32_t v = *keys;
            if (first || v != prev) {
                int b;
                printf("%7.3f keys=0x%08x bits:", now_s() - t0, v);
                for (b = 0; b < 32; b++) if (v & (1u << b)) printf(" %d", b);
                printf("\n");
                prev = v; first = 0;
            }
        }
        if (nev && poll(pf, nev, 5) > 0) {
            for (i = 0; i < nev; i++) {
                struct input_event ev;
                while (pf[i].fd >= 0 && read(pf[i].fd, &ev, sizeof(ev)) == sizeof(ev))
                    if (ev.type == EV_KEY || ev.type == EV_ABS)
                        printf("%7.3f %s type=%u code=%u value=%d\n", now_s() - t0, names[i], ev.type, ev.code, ev.value);
            }
        } else usleep(4000);
    }
    printf("done\n");
    return 0;
}
