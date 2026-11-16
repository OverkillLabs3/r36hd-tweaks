#!/bin/bash
# builds the on-device memory test (MIPS32r2 hard float, runs on the console)
cd "$(dirname "$0")/../.."
. tools/env_ship.sh
mkdir -p build/probe
"${SF_PREFIX}gcc" $SF_CFLAGS -O2 -static-libgcc -o build/probe/memtest tools/device/memtest.c && echo built && ls -la build/probe/memtest
