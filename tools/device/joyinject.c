/* joyinject: hold a button mask in cubevol's shared-memory button word for <ms> milliseconds, then release.
 *   joyinject <mask hex> [ms]            e.g. joyinject 40 120   (bit 6 = DOWN for 120 ms)
 * cubevol rewrites the word every 16 ms, so the mask is written again every 2 ms for the duration.
 * Default raw bits: UP 4, RIGHT 5, DOWN 6, LEFT 7, L2 8, L1 10, R1 11, X 12, A 13, B 14, Y 15, START 3, SELECT 0, FN 16.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
int main(int argc, char **argv)
{
    uint32_t mask = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 16) : 0;
    int ms = argc > 2 ? atoi(argv[2]) : 100;
    int id = shmget(ftok("/tmp/joy_key", 'a'), 4, 0666);
    if (id < 0) { perror("shmget"); return 1; }
    volatile uint32_t *p = shmat(id, NULL, 0);
    if (p == (void *)-1) { perror("shmat"); return 1; }
    for (int t = 0; t < ms; t += 2) { *p = *p | mask; usleep(2000); }
    return 0;
}
