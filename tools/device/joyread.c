/* joyread: print the button word cubevol keeps in SysV shared memory (ftok("/tmp/joy_key", 'a'), 4 bytes) whenever it
 * changes, for <seconds> seconds.   joyread [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    key_t k = ftok("/tmp/joy_key", 'a');
    int id = shmget(k, 4, 0);
    if (id < 0) { perror("shmget"); return 1; }
    volatile uint32_t *p = shmat(id, NULL, SHM_RDONLY);
    if (p == (void *)-1) { perror("shmat"); return 1; }
    uint32_t last = 0xdeadbeef;
    long n = 0, changes = 0;
    for (int t = 0; t < secs * 200; t++) {
        uint32_t v = *p; n++;
        if (v != last) { printf("%6.2fs %08x\n", t / 200.0, v); fflush(stdout); last = v; changes++; }
        usleep(5000);
    }
    printf("done, %ld changes\n", changes);
    return 0;
}
