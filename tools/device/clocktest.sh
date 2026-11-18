#!/bin/sh
# R36 Clock Test: tries the CPU clock sources listed in the device tree (cpu_clk mux at 0x18800070/0x18800074,
# select field bits 10:8, trigger bit 16). Sources: 0=594 1=396 2=297 3=198 4=900 5=1188 6=24 7=fdig(900) MHz.
# Every step is written and synced to /mnt/sdcard/clock_test.txt first, so a freeze still leaves a record.
# A reboot always restores the stock clock. At the end it switches back to the clock it started with.
OUT=/mnt/sdcard/clock_test.txt
DIR=/mnt/sdcard/roms/ports/r36probe
CTRL=0x18800074
INFO=0x18800070
log() { echo "$*" >> $OUT; sync; }
: > $OUT
log "clock test $(date 2>/dev/null)"
c0=$(devmem $CTRL 32); i0=$(devmem $INFO 32)
log "before: ctrl($CTRL)=$c0 info($INFO)=$i0"
log "speed now: $($DIR/spin)"
orig_sel=$(( (i0 >> 8) & 7 ))
log "current source index from info bits 10:8 = $orig_sel"

setsel() {
  sel=$1
  c=$(devmem $CTRL 32)
  new=$(( (c & ~(7 << 8)) | (sel << 8) ))
  log "set source $sel: write ctrl=$(printf 0x%08x $new)"
  devmem $CTRL 32 $new
  devmem $CTRL 32 $(( new | (1 << 16) ))
  usleep 20000
  devmem $CTRL 32 $new
  usleep 20000
  log "  after: ctrl=$(devmem $CTRL 32) info=$(devmem $INFO 32)"
  log "  speed: $($DIR/spin)"
}

for s in ${CLOCKS:-4 5}; do setsel $s; done
log "restoring source $orig_sel"
setsel $orig_sel
log "DONE"
sync
exit 0
