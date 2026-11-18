#!/bin/bash
# Decompiles every .dtb in a folder and prints the memory-related nodes.
cd "${1:-.}"
for f in *.dtb; do
  dtc -I dtb -O dts -o "${f%.dtb}.dts" "$f" 2>/dev/null
  echo "===== $f"
  grep -nE 'model|compatible = "hichip|memory|mmz|reserved|mapping|ramsize|ddr|size =' "${f%.dtb}.dts" | head -40
done
