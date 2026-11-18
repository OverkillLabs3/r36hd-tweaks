#!/bin/bash
# try_cfg.sh [scripts/config args]: apply config changes, rebuild net/core/dev.o, print the net_device size and
# the offsets alloc_netdev_mqs stores to (vendor kernel: size 960, stores at 584 and 856..862)
K=/root/kbuild/linux-4.4.186
export PATH=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin:$PATH
cd $K
[ $# -gt 0 ] && scripts/config "$@"
M="ARCH=mips CROSS_COMPILE=mips-mti-linux-gnu- HOSTCFLAGS=-fcommon"
make $M olddefconfig >/dev/null 2>&1
make $M net/core/dev.o >/dev/null 2>&1 || echo BUILD FAIL
mips-mti-linux-gnu-objdump -d net/core/dev.o \
  | awk '$2=="<alloc_netdev_mqs>:"{p=1;next} p&&/^$/{exit} p' | cut -f3- \
  | grep -E 'li.v0,9[0-9][0-9]|addiu.v1,s0,9|s[wh].(v0|zero),(5|8)[0-9][0-9][(]s0[)]' | head -8 | tr '\n' ';'
echo
