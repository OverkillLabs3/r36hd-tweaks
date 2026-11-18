#!/bin/bash
# Builds cpusoak for the ship toolchain. First pass gets the PC reference values under QEMU, second pass embeds them.
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
CC=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-gcc
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R="$(cd "$(dirname "$0")/../.." && pwd)"
F="-EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall"
$CC $F -o $R/build/cpusoak0 $R/tools/device/cpusoak.c -lm
L=$(qemu-mipsel -L $SR $R/build/cpusoak0 0.001 /tmp/soak0.txt >/dev/null; grep 'first round:' /tmp/soak0.txt | head -1)
echo "$L"
set -- $L
D="-DEXPECT_A=0x${4}u -DEXPECT_B=0x${6}u -DEXPECT_C=0x${8}u -DEXPECT_D=0x${10}u -DEXPECT_E=0x${12}u"
echo "$D"
$CC $F $D -o $R/build/cpusoak $R/tools/device/cpusoak.c -lm
readelf -A $R/build/cpusoak | grep -E 'FP ABI|ISA:'
qemu-mipsel -L $SR $R/build/cpusoak 0.001 /tmp/soak1.txt; cat /tmp/soak1.txt