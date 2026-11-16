#!/bin/bash
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
CC=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-gcc
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R=$(cd "$(dirname "$0")/../.." && pwd); mkdir -p "$R/build"
$CC -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall -o $R/build/sdclk $R/tools/device/sdclk.c
readelf -A $R/build/sdclk | grep -E 'FP ABI|ISA:'
readelf -d $R/build/sdclk | grep NEEDED
ls -la $R/build/sdclk
