/* stepslog: one line per step, written and fsynced at once, so the last line survives a freeze.
 * Enabled by the empty file /mnt/sdcard/cubegm/c2_debug. Log: /mnt/sdcard/cubegm/logs/c2r_steps.log */
#ifndef STEPSLOG_H
#define STEPSLOG_H
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
static __attribute__((unused)) void steps_log(const char *fmt, ...)
{
    static int on = -1;
    if (on < 0) on = access("/mnt/sdcard/cubegm/c2_debug", F_OK) == 0;
    if (!on) return;
    FILE *f = fopen("/mnt/sdcard/cubegm/logs/c2r_steps.log", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc(10, f); fflush(f); fsync(fileno(f)); fclose(f);
}
#endif
