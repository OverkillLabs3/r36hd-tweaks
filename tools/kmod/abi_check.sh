#!/bin/bash
# Compare struct module size with the vendor's modules and check every undefined symbol is exported by the kernel
# (build/kernel/exports.txt) or by another module in the set.
SF=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin
R="$(cd "$(dirname "$0")/../.." && pwd)"
O=$R/build/kmod
ST=$R/build/stock_fw/rootfs/lib/modules/4.4.186-release
sz() { $SF/mips-mti-linux-gnu-readelf -S -W $1 | awk '/this_module/ {print strtonum("0x"$6)}'; }
echo "struct module size: stock libcomposite $(sz $ST/kernel/drivers/usb/gadget/libcomposite.ko) stock mtp $(sz $ST/kernel/drivers/usb/gadget/function/usb_f_mtp.ko) tf mass_storage $(sz $R/reference/TreeFrogUI/apps/usb_mode/modules/4.4.186-release/usb_f_mass_storage.ko)"
for f in $O/*.ko; do echo "  $(basename $f): $(sz $f)"; done
tr -d "" < $R/build/kernel/exports.txt > /tmp/known.txt
for f in $O/*.ko $ST/kernel/drivers/usb/gadget/libcomposite.ko; do $SF/mips-mti-linux-gnu-nm --defined-only $f | awk '$2 ~ /[TDRB]/ {print $3}' >> /tmp/known.txt; done
sort -u /tmp/known.txt -o /tmp/known.txt
for f in $O/*.ko; do
  miss=$($SF/mips-mti-linux-gnu-nm -u $f | awk '{print $2}' | sort -u | comm -23 - /tmp/known.txt | tr '\n' ' ')
  echo "$(basename $f) unresolved: ${miss:-none}"
done
