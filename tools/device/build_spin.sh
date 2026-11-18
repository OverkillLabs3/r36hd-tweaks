#!/bin/bash
cd "$(dirname "$0")/../.."
. tools/env_ship.sh
"${SF_PREFIX}gcc" $SF_CFLAGS -O2 -static-libgcc -o build/probe/spin tools/device/spin.c && echo built
QEMU_LD_PREFIX=$SF_SYSROOT qemu-mipsel -L $SF_SYSROOT build/probe/spin
