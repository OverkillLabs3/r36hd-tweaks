NM=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-nm
R="$(cd "$(dirname "$0")/../.." && pwd)"
V=$R/build/stock_fw/rootfs/lib/modules/4.4.186-release/kernel/drivers/usb/gadget/libcomposite.ko
M=$R/build/kmod/libcomposite.ko
$NM -S --defined-only $V | awk '$3 ~ /[TDRB]/ {print $4, $2}' | sort > /tmp/v.txt
$NM -S --defined-only $M | awk '$3 ~ /[TDRB]/ {print $4, $2}' | sort > /tmp/m.txt
echo "vendor $(wc -l < /tmp/v.txt) mine $(wc -l < /tmp/m.txt)"
join -a1 -a2 -e MISSING -o 0,1.2,2.2 /tmp/v.txt /tmp/m.txt | awk '$2 != $3' | head -40
