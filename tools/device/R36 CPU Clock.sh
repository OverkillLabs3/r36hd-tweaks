#!/bin/sh
# R36 CPU Clock - read-only. Measures the real CPU clock and reads the SoC clock registers.
# Writes /mnt/sdcard/cpuclk_report.txt. Never starts picoarch.
PROBE=/mnt/sdcard/roms/ports/r36probe/cpuclk_probe
OUT=/mnt/sdcard/cpuclk_report.txt
{
    echo "=== R36 CPU Clock ==="
    date 2>/dev/null
    echo "-- /proc/cpuinfo --"
    grep -E 'cpu model|BogoMIPS|ASEs|isa|system type' /proc/cpuinfo
    echo "-- chip clock registers and measured clock --"
} > "$OUT" 2>&1
sync
if [ -x "$PROBE" ]; then
    "$PROBE" >> "$OUT" 2>&1
else
    echo "probe binary missing: $PROBE" >> "$OUT"
fi
echo "-- device tree cpu node --" >> "$OUT"
for f in /proc/device-tree/cpus/cpu@0/clock /proc/device-tree/cpus/cpu@0/mcpu-dig-pll-clk /proc/device-tree/hcrtos/mcpu/clock /proc/device-tree/hcrtos/mcpu/mcpu-dig-pll-clk /proc/device-tree/hcrtos/scpu/clock /proc/device-tree/hcrtos/scpu/scpu-dig-pll-clk; do
    [ -f "$f" ] && echo "$f = 0x$(xxd -p "$f" 2>/dev/null)" >> "$OUT"
done
echo done >> "$OUT"
sync
