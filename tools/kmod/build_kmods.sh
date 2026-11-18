#!/bin/bash
# Build kernel modules that match the console's kernel (Linux 4.4.186-release, PREEMPT, MIPS32r2, no SMP, no module
# versions, no module unload). Recipe from TreeFrogUI's usb_f_mass_storage.ko (reference/TreeFrogUI/apps/usb_mode).
# Output: build/kmod/*.ko
set -e
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin
export PATH=$SF:$PATH
K=/root/kbuild/linux-4.4.186
R="$(cd "$(dirname "$0")/../.." && pwd)"
O=$R/build/kmod
mkdir -p $O
cd $K
M="ARCH=mips CROSS_COMPILE=mips-mti-linux-gnu- HOSTCFLAGS=-fcommon"
make $M rt305x_defconfig >/dev/null
C=scripts/config
$C --set-str LOCALVERSION "-release" -d LOCALVERSION_AUTO
$C -e PREEMPT -d PREEMPT_NONE -d PREEMPT_VOLUNTARY -d DEBUG_PREEMPT -d SMP -d MODVERSIONS -d MODULE_UNLOAD -e MODULES
$C -e CONFIGFS_FS -e HZ_1000 -d HZ_100 -d HZ_250 --set-val HZ 1000 -e NET -e USB_SUPPORT -e USB_GADGET -m USB_LIBCOMPOSITE -m USB_CONFIGFS
$C -e USB_CONFIGFS_NCM -e USB_CONFIGFS_ECM -e USB_CONFIGFS_RNDIS -e USB_CONFIGFS_ACM -e USB_CONFIGFS_SERIAL
$C -m ZSMALLOC -m ZRAM
# struct net_device and struct sk_buff must match the vendor kernel (checked with tools/kmod/abi_fn.sh and offs.sh):
# its kernel has no netfilter ingress hook, no VLAN and no packet scheduler
$C -d NETFILTER_INGRESS -d NET_INGRESS -d VLAN_8021Q -d NET_SCHED
# block layer: vendor kernel has 32-bit sector numbers and the bsg char device (gendisk and request_queue layouts)
$C -d LBDAF -e BLK_DEV_BSG -e BLK_DEV_BSGLIB
# memory layout (struct zone, struct page, slab): vendor has MAX_ORDER 14 (buddyinfo shows 14 orders) and SLAB (slabinfo has the tunables columns)
$C -d SLUB -e SLAB --set-val FORCE_MAX_ZONEORDER 14
make $M olddefconfig >/dev/null
grep -E "^CONFIG_(PREEMPT|SMP|MODVERSIONS|MODULE_UNLOAD|LOCALVERSION|USB_F_|USB_U_|USB_CONFIGFS|USB_LIBCOMPOSITE|USB_GADGET|CONFIGFS_FS|NET|ZRAM|ZSMALLOC|CPU_MIPS32_R2|CPU_LITTLE|HZ)=" .config | sort
make $M -j8 modules_prepare >/dev/null
make $M -j8 M=drivers/usb/gadget modules 2>&1 | grep -E "error|warning: \"" | head || true
make $M -j8 M=drivers/block/zram modules 2>&1 | grep -E "error" | head || true
make $M -j8 mm/zsmalloc.ko 2>&1 | grep -E "error" | head || true
find drivers/usb/gadget drivers/block/zram mm -name "*.ko" -exec cp {} $O/ \;
for f in $O/*.ko; do printf "%-28s %s\n" $(basename $f) "$(modinfo -F vermagic $f 2>/dev/null || strings $f | grep vermagic=)"; done
