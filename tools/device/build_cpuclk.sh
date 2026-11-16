#!/bin/bash
# build the cpu clock probe with the ship toolchain and smoke-test under QEMU
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
CC=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-gcc
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R=$(cd "$(dirname "$0")/../.." && pwd); mkdir -p "$R/build"
$CC -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -o $R/build/cpuclk_probe $R/tools/device/cpuclk_probe.c
readelf -A $R/build/cpuclk_probe | grep -E 'FP ABI|ISA:'
readelf -d $R/build/cpuclk_probe | grep NEEDED
echo "--- qemu smoke test (MHz value is QEMU's, not the device's) ---"
timeout 120 qemu-mipsel -cpu 74Kf -L $SR/mipsel-r2-hard $R/build/cpuclk_probe 2>&1 | head -6 || true
