#!/bin/bash
# Build the kernel-mode gadget (kx) and its payloads. Payloads must have no relocations.
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
P=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R=$(cd "$(dirname "$0")/../../.." && pwd)
S=$R/tools/device/kx
O=$R/build/kx; mkdir -p $O
for n in cp0read cfg7write tlbwire; do
  ${P}gcc -EL -march=mips32r2 -mno-abicalls -fno-pic -c $S/$n.S -o $O/$n.o
  if ${P}readelf -r $O/$n.o | grep -q "rel.text"; then echo "$n.o has relocations"; exit 1; fi
  ${P}objcopy -O binary -j .text $O/$n.o $O/$n.bin
  echo "$n.bin $(stat -c %s $O/$n.bin) bytes"
done
${P}gcc -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall -o $O/kx $S/kx.c -lpthread
ls -la $O
${P}gcc -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall -o $O/tlbclear $S/tlbclear.c
ls -la $O/tlbclear
