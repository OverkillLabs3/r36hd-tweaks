#!/bin/bash
# abi_fn.sh: compare the memory offsets used by kernel functions in the vendor kernel and in our build of the same
# source (a quick check that struct layouts match). Lines: function, vendor offsets, ours, verdict.
R="$(cd "$(dirname "$0")/../.." && pwd)"
K=/root/kbuild/linux-4.4.186
OD=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-objdump
export PATH=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin:$PATH
KS=$R/build/kernel/kallsyms.txt
check() {   # function object
    f=$1; obj=$2
    a=$(grep -E " [Tt] $f\$" $KS | head -1 | cut -c1-8)
    [ -n "$a" ] || { echo "$f: not in kallsyms"; return; }
    off=$(( 0x$a - 0x80000000 ))
    (cd $K && make ARCH=mips CROSS_COMPILE=mips-mti-linux-gnu- HOSTCFLAGS=-fcommon $obj >/dev/null 2>&1)
    mine=$($OD -d $K/$obj | awk -v f="<$f>:" '$2==f{p=1;next} p&&/^$/{exit} p' | cut -f3-)
    n=$(echo "$mine" | wc -l)
    vend=$($OD -D -b binary -m mips:isa32r2 -EL --start-address=$off --stop-address=$((off + n * 4)) $R/build/kernel/vmlinux.raw | tail -n +8 | cut -f3-)
    vo=$(echo "$vend" | grep -oE '[-0-9]+\((s[0-8]|a[0-3]|v[01]|t[0-9])\)' | sed 's/(.*//' | sort -n | uniq | tr '\n' ' ')
    mo=$(echo "$mine" | grep -oE '[-0-9]+\((s[0-8]|a[0-3]|v[01]|t[0-9])\)' | sed 's/(.*//' | sort -n | uniq | tr '\n' ' ')
    if [ "$vo" = "$mo" ]; then echo "$f: same offsets ($vo)"; else echo "$f: DIFFERENT"; echo "   vendor: $vo"; echo "   ours:   $mo"; fi
}
check __alloc_skb net/core/skbuff.o
check eth_type_trans net/ethernet/eth.o
check alloc_netdev_mqs net/core/dev.o
check register_netdevice net/core/dev.o
check netif_rx_internal net/core/dev.o
check netdev_register_kobject net/core/net-sysfs.o
check tty_port_init drivers/tty/tty_port.o
check tty_register_driver drivers/tty/tty_io.o
check usb_gadget_probe_driver drivers/usb/gadget/udc/udc-core.o
