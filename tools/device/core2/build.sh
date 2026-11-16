#!/bin/bash
# Build the second-core tools: job.bin/core2 (single-shot proof of concept), boot.bin, worker.bin, jobs.bin and c2bench.
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
P=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-
SR=$SF/mipsel-buildroot-linux-gnu/sysroot
R=$(cd "$(dirname "$0")/../../.." && pwd)
S=$R/tools/device/core2
O=$R/build/core2; mkdir -p $O
blob() {   # position-independent blob: one global function at offset 0, no relocations, no calls by jal
  local n=$1
  ${P}gcc -EL -march=mips32r2 -mno-abicalls -fno-pic -G0 -O2 -ffreestanding -fno-builtin -fno-asynchronous-unwind-tables \
      -fno-stack-protector -Wall -I$S -c $S/$n.c -o $O/$n.o
  if ${P}readelf -r $O/$n.o | grep -q "rel.text"; then echo "$n.o has relocations:"; ${P}readelf -r $O/$n.o; exit 1; fi
  test "$(${P}nm $O/$n.o | grep -c ' [TtDdBbRr] ')" = 1 || { echo "$n.o must define exactly one symbol"; ${P}nm $O/$n.o; exit 1; }
  if ${P}objdump -d $O/$n.o | grep -qE "\sjal\s"; then echo "$n.o uses jal"; exit 1; fi
  ${P}objcopy -O binary -j .text $O/$n.o $O/$n.bin
  echo "$n.bin $(stat -c %s $O/$n.bin) bytes"
}
blob job
blob boot
blob worker
blob jobs
${P}gcc -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall -o $O/core2 $S/core2.c -lpthread
${P}gcc -EL -mips32r2 -march=mips32r2 -mfp32 -mhard-float --sysroot=$SR -O2 -Wall -o $O/c2bench $S/c2bench.c $R/common/core2.c
ls -la $O
