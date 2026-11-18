#!/bin/bash
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
R="$(cd "$(dirname "$0")/../.." && pwd)"
$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-gcc -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float \
  --sysroot=$SF/mipsel-buildroot-linux-gnu/sysroot -O2 -Wall -o $R/build/kx/fwsh $R/tools/device/fwsh.c && echo built
