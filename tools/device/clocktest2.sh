#!/bin/sh
# R36 Clock Test 2 - corrected trigger. The earlier clock test set bit 16 as the apply bit; the real kernel
# (hc16xx_set_cpu_clk) and the bootloader (hcboot) use bit 22 (0x400000) after writing the source into bits 10:8.
# Source table (from the device tree and hcboot): 0=594 1=396 2=297 3=198 4=900 5=1188 6=24 7=PLL direct.
# Every step is logged and synced to /mnt/sdcard/clock_test2.txt BEFORE the write, so a freeze leaves a record.
# A power cycle restores the stock clock. It restores the starting source at the end.
# CAUTION: this changes the live CPU clock. Run it alone, with nothing else running, and wait for DONE.
OUT=/mnt/sdcard/clock_test2.txt
DIR=/mnt/sdcard/roms/ports/r36probe
CTRL=0x18800074
log() { echo "$*" >> $OUT; sync; }
: > $OUT
log "clock test 2 $(date 2>/dev/null)"
c0=$(devmem $CTRL 32)
log "before: ctrl=$c0 speed=$($DIR/cpuclk_probe 2>&1 | head -1)"
orig_sel=$(( (c0 >> 8) & 7 ))
log "current mux source = $orig_sel"

setsel() {
  sel=$1
  c=$(devmem $CTRL 32)
  new=$(( (c & ~(7 << 8)) | (sel << 8) ))
  log "NEXT: source $sel, write ctrl=$(printf 0x%08x $new) then apply bit 22"
  devmem $CTRL 32 $new
  devmem $CTRL 32 $(( new | 0x400000 ))
  usleep 50000
  log "  after: ctrl=$(devmem $CTRL 32)"
  log "  speed: $($DIR/cpuclk_probe 2>&1 | head -1)"
}

for s in ${CLOCKS:-0 1 2 3}; do setsel $s; done
log "restoring source $orig_sel"
setsel $orig_sel
log "DONE"
