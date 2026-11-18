#!/bin/sh
# R36 Buttons: for 60 seconds records every button/axis change into /mnt/sdcard/buttons_report.txt.
# Press the buttons slowly, one at a time, in this order, pausing about 1 second between presses:
#   UP, DOWN, LEFT, RIGHT, A, B, X, Y, L1, R1, L2, R2, START, SELECT, VOLUME UP, VOLUME DOWN, MENU/POWER if any,
#   then SELECT+START together, L1+R1 together, and move the analog stick(s) if there are any.
SD=${SD:-/mnt/sdcard}
DIR=${DIR:-$SD/roms/ports/r36probe}
{
  echo "buttons report $(date 2>/dev/null)"
  echo "--- /proc/bus/input/devices"
  cat /proc/bus/input/devices 2>&1
  echo "--- capture"
  "$DIR/joylog" ${SECS:-60} 2>&1
} > "$SD/buttons_report.txt" 2>&1
sync
exit 0
