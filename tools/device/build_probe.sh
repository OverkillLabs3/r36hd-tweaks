#!/bin/bash
# Builds the on-device probe tools with the ship toolchain and smoke-tests them under QEMU.
set -e
cd "$(dirname "$0")/../.."
. tools/env_ship.sh
OUT=build/probe
rm -rf $OUT
mkdir -p $OUT
CC="${SF_PREFIX}gcc"
$CC $SF_CFLAGS -O2 -static-libgcc -o $OUT/bench tools/device/bench.c -lm -lpthread
$CC $SF_CFLAGS -O2 -mtune=74kc -mdspr2 -static-libgcc -o $OUT/bench74 tools/device/bench.c -lm -lpthread
$CC $SF_CFLAGS -O2 -static-libgcc -o $OUT/hwprobe tools/device/hwprobe.c
$CC $SF_CFLAGS -O2 -static-libgcc -o $OUT/joylog tools/device/joylog.c
# luajit command line tool, relinked without libgcc_s
(
  cd third_party/LuaJIT
  make clean >/dev/null 2>&1 || true
  make -j8 amalg BUILDMODE=static TARGET_SYS=Linux HOST_CC="gcc -m32" CROSS="$SF_PREFIX" \
    TARGET_CFLAGS="$SF_CFLAGS" TARGET_LDFLAGS="$SF_LDFLAGS -static-libgcc" \
    STATIC_CC="$CC" DYNAMIC_CC="$CC" TARGET_LD="$CC" >/dev/null 2>&1
  cp src/luajit ../../$OUT/luajit
)
cp tools/device/probe.sh tools/device/buttons.sh tools/device/bench.lua $OUT/
chmod +x $OUT/*
readelf -d $OUT/luajit | grep NEEDED
readelf -A $OUT/bench | grep -i 'fp abi'
echo "--- QEMU smoke test of hwprobe (numbers describe QEMU, not the device)"
export QEMU_LD_PREFIX=$SF_SYSROOT
timeout 300 qemu-mipsel -L $SF_SYSROOT $OUT/hwprobe /tmp 2>&1 | cut -c1-200
