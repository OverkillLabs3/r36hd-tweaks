#!/bin/sh
# cpuclock.sh - optional CPU overclock set at every start by the start-up script, through the SoC clock registers.
# Mechanism copied from the console's own kernel and boot loader (hc16xx_set_cpu_clk):
#   0x18800380  PLL divider  = ((MHz*10 - 0x18) / 0x18) << 16 | 0x80000000
#   0x18800074  CPU source   = bits 10:8 (7 = that PLL, 5 = the 1188 MHz preset), then bit 22 applies the change
#   0x1880007c  bit 7 set when source 7 is used
#   0x18800070  reads back the source that is really in use (bits 10:8)
# Config: /mnt/sdcard/cubegm/cpu_mhz.txt holds one number, e.g. 1250. No file = nothing happens (stock 1188 preset).
# Safety: a flag file is written (and synced) BEFORE the switch and removed 20 s after a stable start. If the console
# hangs, the next start finds the flag, renames the config to cpu_mhz.txt.failed and leaves the clock alone.
# Everything is lost at power off, so a hang is cured by switching the console off and on.
CFG=/mnt/sdcard/cubegm/cpu_mhz.txt
FLAG=/mnt/sdcard/cubegm/cpu_try.flag
PROBE=/mnt/sdcard/r36probe/cpuclk_probe
CLOG=/mnt/sdcard/cubegm/logs/cpuclock.log
mkdir -p /mnt/sdcard/cubegm/logs 2>/dev/null
cl() { echo "$(date '+%H:%M:%S' 2>/dev/null) $*" >> "$CLOG"; sync; }

if [ -f "$FLAG" ]; then
    mv -f "$CFG" "$CFG.failed" 2>/dev/null
    rm -f "$FLAG"
    : > "$CLOG"
    cl "previous start did not finish after the clock change: config disabled (cpu_mhz.txt.failed). Clock left at stock."
elif [ -f "$CFG" ]; then
    MHZ=$(tr -dc '0-9' < "$CFG" | head -c 5)
    : > "$CLOG"
    if [ -z "$MHZ" ] || [ "$MHZ" -lt 600 ] || [ "$MHZ" -gt 1500 ]; then
        cl "bad value in cpu_mhz.txt ('$MHZ'): allowed 600 to 1500. Nothing done."
    else
        base=$($PROBE 2>/dev/null | sed -n 's/.*hardware timer clock:.*CPU ~ \([0-9.]*\) MHz.*/\1/p')
        r74=$(devmem 0x18800074 32); r70=$(devmem 0x18800070 32); r380=$(devmem 0x18800380 32); r7c=$(devmem 0x1880007c 32)
        cl "before: mux74=$r74 info70=$r70 pll380=$r380 reg7c=$r7c measured=${base}MHz target=${MHZ}MHz"
        echo "$MHZ" > "$FLAG"; sync
        DIV=$(( (MHZ * 10 - 24) / 24 ))
        NEW380=$(( (DIV << 16) | 0x80000000 | (r380 & 0xffff) ))
        devmem 0x18800380 32 $NEW380
        usleep 20000
        NEW74=$(( (r74 & ~0x700) | 0x700 ))
        devmem 0x18800074 32 $NEW74
        devmem 0x18800074 32 $(( NEW74 | 0x400000 ))
        devmem 0x1880007c 32 $(( r7c | 0x80 ))
        usleep 50000
        r70n=$(devmem 0x18800070 32)
        src=$(( (r70n >> 8) & 7 ))
        now=$($PROBE 2>/dev/null | sed -n 's/.*hardware timer clock:.*CPU ~ \([0-9.]*\) MHz.*/\1/p')
        cl "after: info70=$r70n source=$src divider=$DIV measured=${now}MHz"
        # keep the new clock only when it is really the PLL source and measurably faster than before
        ok=$(awk -v a="$base" -v b="$now" -v s="$src" 'BEGIN{ print (s==7 && a > 0 && b > a*1.02) ? 1 : 0 }')
        if [ "$ok" != 1 ]; then
            devmem 0x18800074 32 $r74
            devmem 0x18800074 32 $(( r74 | 0x400000 ))
            usleep 50000
            cl "not faster or not applied: switched back to the stock source ($(devmem 0x18800070 32))"
            rm -f "$FLAG"
        else
            cl "clock kept at ${MHZ} MHz (measured ${now})"
            ( sleep 20; rm -f "$FLAG"; sync ) &
        fi
    fi
fi
