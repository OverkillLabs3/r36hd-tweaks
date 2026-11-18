# fn.sh <object> <function>: disassemble one function of an object in the kernel tree
OD=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-objdump
cd /root/kbuild/linux-4.4.186
$OD -d $1 | awk -v f="<$2>:" '$2==f{p=1;next} p&&/^$/{exit} p' | cut -f3-
