#!/bin/bash
# cmp_kfn.sh <function> <vendor address hex> <object in the kernel tree>: print the load/store offsets and immediates
# of a function in the vendor kernel and in our build of the same source, side by side
OD=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-objdump
R="$(cd "$(dirname "$0")/../.." && pwd)"
K=/root/kbuild/linux-4.4.186
f=$1; a=$((16#$2)); obj=$3; n=${4:-120}
off=$((a - 0x80000000))
$OD -D -b binary -m mips:isa32r2 -EL --start-address=$off --stop-address=$((off + n*4)) $R/build/kernel/vmlinux.raw | tail -n +8 | cut -f3- | sed 's/0x[0-9a-f]*$//' > /tmp/v.txt
cd $K
export PATH=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin:$PATH
make ARCH=mips CROSS_COMPILE=mips-mti-linux-gnu- HOSTCFLAGS=-fcommon $obj >/dev/null 2>&1
$OD -d $K/$obj | awk -v f="<$f>:" '$2==f{p=1;next} p&&/^$/{exit} p' | cut -f3- | head -$n > /tmp/m.txt
paste -d'|' <(cut -c1-45 /tmp/v.txt) <(cut -c1-45 /tmp/m.txt) | head -$n
