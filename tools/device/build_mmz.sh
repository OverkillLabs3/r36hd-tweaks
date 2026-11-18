#!/bin/bash
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
CC=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-gcc
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R="$(cd "$(dirname "$0")/../.." && pwd)"
$CC -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -o $R/build/mmz_probe $R/tools/device/mmz_probe.c
readelf -A $R/build/mmz_probe | grep -E 'FP ABI|ISA:'
readelf -d $R/build/mmz_probe | grep NEEDED
