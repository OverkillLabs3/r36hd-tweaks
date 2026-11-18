OD=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-objdump
R="$(cd "$(dirname "$0")/../.." && pwd)"
V=$R/build/stock_fw/rootfs/lib/modules/4.4.186-release/kernel/drivers/usb/gadget/libcomposite.ko
M=$R/build/kmod/libcomposite.ko
f=$1
$OD -d --no-show-raw-insn $V | awk -v f="<$f>:" '$2==f{p=1;next} p&&/^$/{exit} p' | sed 's/^ *[0-9a-f]*:\t//' > /tmp/v_$f.txt
$OD -d --no-show-raw-insn $M | awk -v f="<$f>:" '$2==f{p=1;next} p&&/^$/{exit} p' | sed 's/^ *[0-9a-f]*:\t//' > /tmp/m_$f.txt
diff /tmp/v_$f.txt /tmp/m_$f.txt | head -${2:-60}
