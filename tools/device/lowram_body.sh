#!/bin/sh
# lowram_body.sh - the real work of lowram.sh. Runs as its OWN process (started by lowram.sh), so nothing in here can
# stop a port from starting. Every write goes through wr(): it only writes if the file exists and is writable.
# (Crash found 2026-11-15: a sourced `echo 1 > /proc/sys/vm/compact_memory` failed because this kernel has no such
#  file, and the shell of run.sh exited with rc=1 before picoarch started, for every hooked port.)
wr() { [ -w "$2" ] && echo "$1" > "$2" 2>/dev/null; return 0; }
# lowram.sh - free RAM and tune the VM just before a port starts. Sourced from a port run.sh.
# Writes only /proc and /sys, which reset on reboot, so it is fully reversible with no boot risk.
# Logs RAM before/after to /mnt/sdcard/lowram_report.txt.
RPT=/mnt/sdcard/lowram_report.txt
{
    echo "lowram $(date 2>/dev/null) port=${1:-?}"
    echo "-- before --"
    grep -E 'MemTotal|MemFree|MemAvailable|Cached|SwapFree|SwapTotal' /proc/meminfo 2>/dev/null
    cat /proc/swaps 2>/dev/null
} > "$RPT" 2>&1

# 1. do not spill port pages to the slow SD swap unless truly out of memory
wr 0 /proc/sys/vm/swappiness
wr 200 /proc/sys/vm/vfs_cache_pressure
wr 2048 /proc/sys/vm/min_free_kbytes
# 0. drop any wired large-page TLB entries a crashed game may have left (the next game installs its own)
[ -x /mnt/sdcard/cubegm/tlbclear ] && /mnt/sdcard/cubegm/tlbclear /mnt/sdcard/cubegm/logs/tlbclear.log </dev/null >/dev/null 2>&1
# 1a. kernel messages are not drawn on the framebuffer console during a game (console=tty1, stock level 7);
#     dmesg still keeps them. Resets on reboot.
wr "1 4 1 7" /proc/sys/kernel/printk
# 1b. bigger SD read-ahead for asset streaming (stock 128 KB; deep_probe.txt 2026-11-15). Resets on reboot.
wr 512 /sys/block/mmcblk0/queue/read_ahead_kb

# 2. zram: if the kernel has it, make a compressed RAM swap and drop the slow SD swap.
#    Compressed RAM swap at ~2-3x gives a port effective headroom without touching the SD card.
#    Reversible: zram and swapon state are gone on reboot.
if [ -e /sys/class/zram-control/hot_add ] || [ -e /sys/block/zram0 ]; then
    ZN=0
    [ -e /sys/block/zram0 ] || ZN=$(cat /sys/class/zram-control/hot_add 2>/dev/null)
    ZDEV=/dev/zram${ZN:-0}
    if ! grep -q "zram${ZN:-0}" /proc/swaps 2>/dev/null; then
        [ -w /sys/block/zram${ZN:-0}/comp_algorithm ] && echo lz4 > /sys/block/zram${ZN:-0}/comp_algorithm 2>/dev/null
        # 24 MB compressed pool (holds ~48-72 MB of pages); tiny real-RAM cost until used
        echo $((24 * 1024 * 1024)) > /sys/block/zram${ZN:-0}/disksize 2>/dev/null
        if [ -e "$ZDEV" ] && command -v mkswap >/dev/null 2>&1; then
            mkswap "$ZDEV" >/dev/null 2>&1
            # prefer zram (higher priority) over the SD swap
            swapon -p 100 "$ZDEV" 2>/dev/null && echo "zram swap on: $ZDEV" >> "$RPT"
            # turn off the slow SD-card swap file if present
            for s in $(awk 'NR>1 && $1 !~ /zram/ {print $1}' /proc/swaps 2>/dev/null); do
                swapoff "$s" 2>/dev/null && echo "sd swap off: $s" >> "$RPT"
            done
            wr 60 /proc/sys/vm/swappiness   # with fast zram, allow some swapping
        fi
    fi
fi

# 2b. this kernel has no SHM, so /tmp is ramfs: files there can never be swapped or evicted.
#     Free what the boot scripts left behind (comes back at the next boot).
echo "unevictable before ramfree: $(grep '^Unevictable' /proc/meminfo)" >> "$RPT"
rm -f /tmp/cubegm/rkgame /hcdemo_files/game_corrupted_480_854.bmp /hcdemo_files/game_corrupted_640_480.bmp 2>/dev/null
echo "unevictable after ramfree:  $(grep '^Unevictable' /proc/meminfo)" >> "$RPT"
ls -l /tmp/cubegm >> "$RPT" 2>&1

# 3. flush and drop clean caches the UI left behind, then defragment free memory
sync
wr 3 /proc/sys/vm/drop_caches
wr 1 /proc/sys/vm/compact_memory

{
    echo "-- after --"
    grep -E 'MemTotal|MemFree|MemAvailable|Cached|SwapFree|SwapTotal' /proc/meminfo 2>/dev/null
    cat /proc/swaps 2>/dev/null
} >> "$RPT" 2>&1
sync
