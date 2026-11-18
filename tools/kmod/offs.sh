# offs.sh: print offsets of sk_buff and net_device fields with the current kernel config (compiles a test file with
# the same command line the kernel uses for net/core/skbuff.o)
K=/root/kbuild/linux-4.4.186
export PATH=/opt/toolchains/sf3000/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin:$PATH
cd $K
make ARCH=mips CROSS_COMPILE=mips-mti-linux-gnu- HOSTCFLAGS=-fcommon net/core/skbuff.o >/dev/null 2>&1
cat > /tmp/offs.c <<'C'
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#define O(s,f) asm volatile("\n.ascii \"@OFF " #s "." #f " %0\"" :: "i"(offsetof(struct s, f)));
#define S(s) asm volatile("\n.ascii \"@SIZE " #s " %0\"" :: "i"(sizeof(struct s)));
void offs(void) {
 S(sk_buff) O(sk_buff,cb) O(sk_buff,_skb_refdst) O(sk_buff,destructor) O(sk_buff,len) O(sk_buff,queue_mapping)
 O(sk_buff,protocol) O(sk_buff,inner_transport_header) O(sk_buff,transport_header) O(sk_buff,mac_header) O(sk_buff,mark) O(sk_buff,tail) O(sk_buff,end) O(sk_buff,head) O(sk_buff,data)
 O(sk_buff,hash) O(sk_buff,vlan_proto) O(sk_buff,priority) O(sk_buff,skb_iif)
 S(net_device) O(net_device,dev) O(net_device,netdev_ops) O(net_device,flags) O(net_device,mtu) O(net_device,dev_addr) O(net_device,_tx) O(net_device,priv_flags) O(net_device,ethtool_ops)
}
C
CMD=$(sed -n 's/^cmd_net\/core\/skbuff.o := //p' net/core/.skbuff.o.cmd | sed 's/-Wp,-MD,[^ ]* //; s/ -c -o net\/core\/skbuff.o net\/core\/skbuff.c//')
eval "$CMD -S -o /tmp/offs.s /tmp/offs.c" 2>&1 | head -5
grep -o '@[A-Z]* [a-z_.]* [0-9]*' /tmp/offs.s
