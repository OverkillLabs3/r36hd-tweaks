#!/bin/sh
# R36 Flash Dump (read-only): copies every partition of the internal SPI flash to /mnt/sdcard/flashdump/
# so the bootloader, kernel, device tree and memory setup can be studied on the PC. Nothing is written to the flash.
OUT=/mnt/sdcard/flashdump
mkdir -p $OUT
{
  echo "flash dump $(date 2>/dev/null)"
  echo "--- /proc/mtd"; cat /proc/mtd
  for d in /dev/mtd[0-9] /dev/mtd[0-9][0-9]; do
    [ -e "$d" ] || continue
    n=$(basename $d)
    case "$n" in *ro) continue;; esac
    dd if=$d of=$OUT/$n.bin bs=4096 2>&1 | tail -n 1
    echo "$n: $(wc -c < $OUT/$n.bin) bytes, md5 $(md5sum $OUT/$n.bin | cut -d' ' -f1)"
  done
  for d in /dev/mtdblock[0-9]; do [ -e "$d" ] && [ ! -e $OUT/$(basename $d | sed 's/block//').bin ] && dd if=$d of=$OUT/$(basename $d).bin bs=4096 2>&1 | tail -n 1; done
  echo "--- sysfs mtd"
  for m in /sys/class/mtd/mtd[0-9]; do echo "$m name=$(cat $m/name) size=$(cat $m/size) type=$(cat $m/type) erasesize=$(cat $m/erasesize)"; done
  echo "--- DDR / memory controller registers (read-only devmem, 0x18800000 system block)"
  a=0x18800000
  i=0
  while [ $i -lt 64 ]; do printf '%s ' "$(printf 0x%08x $((a + i * 4)))=$(devmem $((a + i * 4)) 32)"; i=$((i+1)); [ $((i % 4)) = 0 ] && echo; done
  echo "DONE"
} > $OUT/flashdump.txt 2>&1
sync
sleep 2
[ -x /mnt/sdcard/cubegm/powergpio ] && /mnt/sdcard/cubegm/powergpio
exit 0
