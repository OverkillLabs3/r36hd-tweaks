/* R36 Memory Test: how much RAM does Linux see, and does the memory hold what it is given?
 * Allocates most of the free memory, fills it with a pattern that depends on the address, and checks it again
 * every few seconds for about a minute (other users of the same RAM, such as the video firmware, would change it).
 * Prints a report to stdout; the launcher script copies it to the SD card. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

static long meminfo(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r"); char line[128]; long v = -1;
    while (f && fgets(line, sizeof line, f)) if (!strncmp(line, key, strlen(key))) { v = atol(line + strlen(key) + 1); break; }
    if (f) fclose(f);
    return v;
}
static uint32_t pat(size_t i) { uint32_t x = (uint32_t)i * 2654435761u + 0x9e3779b9u; x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; return x; }

int main(int argc, char **argv)
{
    long total = meminfo("MemTotal:"), avail = meminfo("MemAvailable:");
    long want_kb = argc > 1 ? atol(argv[1]) * 1024 : avail - 12288;
    printf("MemTotal %ld kB, MemAvailable %ld kB, testing %ld kB\n", total, avail, want_kb); fflush(stdout); fsync(1);
    size_t words = (size_t)want_kb * 1024 / 4;
    uint32_t *m = malloc(words * 4);
    if (!m) { printf("malloc failed\n"); return 1; }
    for (size_t i = 0; i < words; i++) m[i] = pat(i);
    long bad_total = 0; size_t first_bad = (size_t)-1;
    for (int pass = 1; pass <= 12; pass++) {
        sleep(5);
        long bad = 0;
        for (size_t i = 0; i < words; i++) if (m[i] != pat(i)) { bad++; if (first_bad == (size_t)-1) first_bad = i; m[i] = pat(i); }
        bad_total += bad;
        printf("pass %d: %ld bad words (MemFree %ld kB)\n", pass, bad, meminfo("MemFree:"));
        fflush(stdout); fsync(1);
    }
    printf("RESULT: %s (%ld bad words in total", bad_total ? "MEMORY CHANGED BY SOMETHING ELSE" : "memory holds", bad_total);
    if (first_bad != (size_t)-1) printf(", first at offset %zu kB", first_bad * 4 / 1024);
    printf(")\n");
    return 0;
}
