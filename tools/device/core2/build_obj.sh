#!/bin/bash
# Build relocatable second-core objects: build_obj.sh name src1.c [src2.c ...] --entry sym [--entry sym2]
# Output: build/core2/name.c2o. Sources must be freestanding (no libc, no libgcc calls: no 64-bit division, no float).
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot
P=$SF/opt/ext-toolchain/bin/mips-mti-linux-gnu-
R=$(cd "$(dirname "$0")/../../.." && pwd)
O=$R/build/core2; mkdir -p $O
name=$1; shift
srcs=(); ents=()
while [ $# -gt 0 ]; do case "$1" in --entry) ents+=("$2"); shift 2;; *) srcs+=("$1"); shift;; esac; done
objs=()
for s in "${srcs[@]}"; do
  o=$O/${name}_$(basename $s .c).o
  ${P}gcc -EL -march=mips32r2 -mno-abicalls -fno-pic -G0 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -fno-asynchronous-unwind-tables -mno-check-zero-division -msoft-float -I$R/tools/device/core2 -I$R/common -c $s -o $o
  objs+=($o)
done
cat > $O/$name.ld <<LD
SECTIONS { . = 0; .text : { *(.text*) } .rodata ALIGN(16) : { *(.rodata*) } .data ALIGN(16) : { *(.data*) *(.sdata*) }
 .bss ALIGN(16) : { *(.bss*) *(.sbss*) *(COMMON) } /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.mdebug*) *(.comment) *(.pdr) *(.gnu.attributes) } }
LD
${P}ld -EL -T $O/$name.ld --emit-relocs -o $O/$name.elf "${objs[@]}"
if ${P}nm $O/$name.elf | grep -E " U " ; then echo "undefined symbols (libgcc or libc call?)"; exit 1; fi
python3 $R/tools/device/core2/c2link.py $O/$name.elf $O/$name.c2o "${ents[@]}"
