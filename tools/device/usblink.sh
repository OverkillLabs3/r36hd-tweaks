#!/bin/sh
# USB link to a PC: the OTG port becomes a USB serial port (ACM, a COM port on Windows) served by r36agent
# (tools/r36.py on the PC: commands and file copies). The stock kernel has no IPv4, so the optional network
# adapter ("ncm" or "rndis" in cubegm/usblink_mode) enumerates but carries no traffic. The SD card stays mounted.
#   usblink.sh start | stop
# Log: /mnt/sdcard/cubegm/logs/usblink.log. Nothing persists over a reboot.
D=/mnt/sdcard/roms/ports/r36probe/usb
LOG=/mnt/sdcard/cubegm/logs/usblink.log
CFG=/sys/kernel/config
G=$CFG/usb_gadget/r36link
UDC=musb-hdrc.0.auto
IP=192.168.77.1
MODE=$(cat /mnt/sdcard/cubegm/usblink_mode 2>/dev/null | tr -d '\r\n ')
[ -n "$MODE" ] || MODE=acm

log() { echo "$(date '+%H:%M:%S' 2>/dev/null) $*" >> $LOG; sync; }
klog() { dmesg 2>/dev/null | tail -n ${1:-15} | sed 's/^/    k: /' >> $LOG; sync; }
# run a step that may block in the kernel: log before, give it a few seconds, log the result and the kernel log
step() { t=$1; shift; log "step: $*"; ( "$@" ) >> $LOG 2>&1 & sp=$!; n=0
    while kill -0 $sp 2>/dev/null && [ $n -lt $((t*10)) ]; do sleep 0.1 2>/dev/null || sleep 1; n=$((n+1)); done
    if kill -0 $sp 2>/dev/null; then log "  STILL RUNNING after ${t}s (blocked in the kernel?)"; klog 25; return 1; fi
    wait $sp; r=$?; log "  rc=$r"; return $r; }
wr() { [ -e "$2" ] || [ -d "$(dirname "$2")" ] || return 1; printf '%s\n' "$1" > "$2" 2>>$LOG; }

role_path() { find /sys/devices -path "*$UDC/mode" 2>/dev/null | head -n 1; }

stop() {
    killall r36agent udhcpd 2>/dev/null
    [ -f /tmp/r36link_shell.pid ] && kill "$(cat /tmp/r36link_shell.pid)" 2>/dev/null
    if [ -d $G ]; then
        printf '\n' > $G/UDC 2>/dev/null
        rm -f $G/configs/c.1/ncm.usb0 $G/configs/c.1/rndis.usb0 $G/configs/c.1/acm.gs0 2>/dev/null
        rm -f $G/os_desc/c.1 2>/dev/null
        rmdir $G/configs/c.1/strings/0x409 $G/configs/c.1 $G/functions/ncm.usb0 $G/functions/rndis.usb0 \
              $G/functions/acm.gs0 $G/strings/0x409 $G 2>/dev/null
    fi
    R=$(role_path); [ -n "$R" ] && printf 'host\n' > "$R" 2>/dev/null
    log "stopped"
}

start() {
    mkdir -p /mnt/sdcard/cubegm/logs
    log "start mode=$MODE"
    stop >/dev/null 2>&1
    mountpoint -q $CFG 2>/dev/null || mount -t configfs none $CFG 2>>$LOG
    for m in libcomposite u_ether usb_f_ncm usb_f_rndis u_serial usb_f_acm; do
        grep -q "^$m " /proc/modules || insmod $D/$m.ko >>$LOG 2>&1 || log "insmod $m failed"
    done
    log "modules: $(cut -d' ' -f1 /proc/modules | tr '\n' ' ')"
    R=$(role_path)
    [ -n "$R" ] || { log "no musb role file"; return 1; }
    log "role was $(cat "$R" 2>/dev/null)"
    klog 10
    step 5 sh -c "printf 'peripheral\n' > '$R'" || step 5 sh -c "printf 'b_peripheral\n' > '$R'"
    log "role now $(cat "$R" 2>/dev/null)"; klog 10
    n=0; while [ $n -lt 30 ] && [ ! -e /sys/class/udc/$UDC ]; do sleep 0.1; n=$((n+1)); done
    [ -e /sys/class/udc/$UDC ] || { log "UDC $UDC did not appear"; return 1; }
    mkdir -p $G || { log "configfs gadget dir failed"; return 1; }
    wr 0x1d6b $G/idVendor; wr 0x0104 $G/idProduct; wr 0x0100 $G/bcdDevice; wr 0x0200 $G/bcdUSB
    wr 0xEF $G/bDeviceClass; wr 0x02 $G/bDeviceSubClass; wr 0x01 $G/bDeviceProtocol
    mkdir -p $G/strings/0x409
    wr "R36HD" $G/strings/0x409/manufacturer; wr "R36HD dev link" $G/strings/0x409/product; wr "r36hd0001" $G/strings/0x409/serialnumber
    mkdir -p $G/configs/c.1/strings/0x409
    wr "link" $G/configs/c.1/strings/0x409/configuration; wr 250 $G/configs/c.1/MaxPower
    if [ "$MODE" = rndis ]; then
        mkdir -p $G/functions/rndis.usb0
        wr 1 $G/os_desc/use; wr 0xcd $G/os_desc/b_vendor_code; wr MSFT100 $G/os_desc/qw_sign
        wr RNDIS $G/functions/rndis.usb0/os_desc/interface.rndis/compatible_id
        wr 5162001 $G/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id
        ln -s $G/functions/rndis.usb0 $G/configs/c.1/
        ln -s $G/configs/c.1 $G/os_desc/ 2>>$LOG
        NET=$(cat $G/functions/rndis.usb0/ifname 2>/dev/null)
    elif [ "$MODE" = ncm ]; then
        mkdir -p $G/functions/ncm.usb0
        ln -s $G/functions/ncm.usb0 $G/configs/c.1/
        NET=$(cat $G/functions/ncm.usb0/ifname 2>/dev/null)
    fi
    mkdir -p $G/functions/acm.gs0 && ln -s $G/functions/acm.gs0 $G/configs/c.1/
    step 8 sh -c "printf '%s\n' $UDC > $G/UDC" || { log "UDC bind failed"; klog 30; return 1; }
    klog 20
    if [ "$MODE" != acm ]; then
    [ -n "$NET" ] && [ "$NET" != "(unnamed net_device)" ] || NET=usb0
    ifconfig $NET $IP netmask 255.255.255.0 up 2>>$LOG
    log "network $NET $(ifconfig $NET 2>&1 | grep -o 'inet addr:[0-9.]*')"
    cat > /tmp/r36link_udhcpd.conf <<EOF2
start 192.168.77.2
end 192.168.77.2
interface $NET
max_leases 1
lease_file /tmp/r36link.leases
option subnet 255.255.255.0
option lease 86400
EOF2
    : > /tmp/r36link.leases
    start-stop-daemon -S -b -x /usr/sbin/udhcpd -- -f /tmp/r36link_udhcpd.conf 2>>$LOG || log "udhcpd failed"
    fi
    # the agent serves the serial function (it reopens /dev/ttyGS0 when the cable comes back)
    start-stop-daemon -S -b -x $D/r36agent -- tty /dev/ttyGS0 2>>$LOG || log "r36agent failed"
    log "ready: agent on /dev/ttyGS0, udc state $(cat /sys/class/udc/$UDC/state 2>/dev/null)"
    ( sleep 15; log "after 15 s: udc state $(cat /sys/class/udc/$UDC/state 2>/dev/null)"; klog 30 ) &
}

chmod 755 $D/r36agent 2>/dev/null
case "$1" in
    stop) stop ;;
    *) start ;;
esac
exit 0
