#!/bin/sh
# lowram.sh - called from a port's run.sh as ". lowram.sh <port>". Starts the real work in a separate process and
# always returns success, so a failure in the tuning can never stop the port (see lowram_body.sh).
sh /mnt/sdcard/cubegm/lowram_body.sh "${1:-?}" >/dev/null 2>&1 </dev/null || true
# faster memset/memcpy for the port (common/fastmem.c, pref 30 on whole cache lines). Off: cubegm/fastmem_off.
# Written as an if-block so it can never fail inside the sourcing run.sh (set -eu).
if [ -f /mnt/sdcard/cubegm/libr36mem.so ] && [ ! -f /mnt/sdcard/cubegm/fastmem_off ]; then
    export LD_PRELOAD=/mnt/sdcard/cubegm/libr36mem.so
fi
# sampling profiler (common/r36prof.c): only when the file cubegm/prof_on exists. Output in cubegm/logs/prof_*.
if [ -f /mnt/sdcard/cubegm/libr36prof.so ] && [ -f /mnt/sdcard/cubegm/prof_on ]; then
    export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}/mnt/sdcard/cubegm/libr36prof.so"
fi
