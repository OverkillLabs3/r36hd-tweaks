#!/bin/sh
# R36 RAM Check (read-only): is the physical RAM 128 MB or 256 MB?
# Reads words at address A and A+128 MB with devmem. If the chip has only 128 MB, the upper address mirrors the
# lower one (same values). If it has 256 MB, the values differ. Writes /mnt/sdcard/ram_check.txt, then powers off.
OUT=/mnt/sdcard/ram_check.txt
{
  echo "ram check $(date 2>/dev/null)"
  same=0; diff=0; fail=0
  for base in 0x01000000 0x01800000 0x02000000 0x05000000 0x05800000 0x06000000 0x06800000 0x07000000 0x07800000; do
    i=0
    while [ $i -lt 8 ]; do
      lo=$(printf '0x%08x' $((base + i * 4096)))
      hi=$(printf '0x%08x' $((base + i * 4096 + 0x08000000)))
      top=$(printf '0x%08x' $((base + i * 4096 + 0x10000000)))
      a=$(devmem $lo 32 2>&1); b=$(devmem $hi 32 2>&1); c=$(devmem $top 32 2>&1)
      echo "$lo=$a  +128MB $hi=$b  +256MB $top=$c"
      case "$a$b" in *rror*|*ail*|*nvalid*) fail=$((fail+1));; *) if [ "$a" = "$b" ]; then same=$((same+1)); else diff=$((diff+1)); fi;; esac
      i=$((i+1))
    done
  done
  echo "pairs equal: $same  different: $diff  failed: $fail"
  if [ $fail -gt 0 ] && [ $same -eq 0 ] && [ $diff -eq 0 ]; then echo "RESULT: devmem could not read physical memory"
  elif [ $diff -gt $((same * 4)) ]; then echo "RESULT: upper 128 MB holds different data -> the chip has MORE than 128 MB (likely 256 MB)"
  else echo "RESULT: upper addresses mirror the lower 128 MB -> the chip has 128 MB"; fi
} > "$OUT" 2>&1
sync
sleep 2
[ -x /mnt/sdcard/cubegm/powergpio ] && /mnt/sdcard/cubegm/powergpio
exit 0
