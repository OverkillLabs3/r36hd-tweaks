#!/bin/sh
# R36 Probe: collects as much evidence as possible about the handheld, read-only, into
#   /mnt/sdcard/probe/*.txt  (one file per topic),  /mnt/sdcard/probe_report.txt (all of it in one file)
# and /mnt/sdcard/probe_bundle.tgz when tar is available. Nothing is written outside the SD card.
SD=${SD:-/mnt/sdcard}
DIR=${DIR:-$SD/roms/ports/r36probe}
RUN=${RUN:-}                       # test hook: a prefix such as "qemu-mipsel -L sysroot"
P=$SD/probe
rm -rf "$P"; mkdir -p "$P"
PROG=$P/PROGRESS.txt
: > "$PROG"

note() { echo "$(date +%H:%M:%S 2>/dev/null) $1" >> "$PROG"; sync; }

# run a command with a time limit (seconds), output to stdout
lim() {
  t=$1; shift
  "$@" &
  p=$!
  i=0
  while kill -0 $p 2>/dev/null && [ $i -lt $t ]; do sleep 1; i=$((i+1)); done
  if kill -0 $p 2>/dev/null; then echo "(timed out after ${t}s, stopped)"; kill -9 $p 2>/dev/null; fi
  wait $p 2>/dev/null
}

show() { echo "--- $*"; "$@" 2>&1; echo; }
cat_file() { echo "--- $1"; if [ -r "$1" ]; then head -c "${2:-6000}" "$1" 2>&1; else echo "(not readable / absent)"; fi; echo; }
have() { command -v "$1" >/dev/null 2>&1; }

# print every readable regular file below $1 (text shown printable, plus a hex preview for short binary values)
dump_tree() {
  find "$1" -type f 2>/dev/null | while read -r f; do
    case "$f" in
      *trigger*|*/reset|*/bind|*/unbind|*/export|*/unexport|*/kmsg|*/kcore|*/sysrq-trigger|*/event[0-9]*|*/uevent_helper|*/remove|*/rescan|*/shutdown*|*/delete) continue;;
    esac
    echo "## $f"
    head -c 400 "$f" 2>/dev/null | tr -c '[:print:]\n' '.'
    echo
  done
}

dt_dump() {
  find /proc/device-tree -type f 2>/dev/null | while read -r f; do
    echo "## $f ($(wc -c < "$f" 2>/dev/null) bytes)"
    head -c 256 "$f" 2>/dev/null | tr -c '[:print:]' '.'
    echo
    if have od; then echo -n "   hex: "; head -c 48 "$f" 2>/dev/null | od -An -tx1 | tr -d '\n'; echo; fi
  done
}

# ---------------------------------------------------------------- 01 system / identity
note "01 system"
{
  echo "######## 01 SYSTEM IDENTITY   $(date 2>/dev/null)"
  show uname -a
  show id
  cat_file /proc/version
  cat_file /proc/cmdline
  cat_file /proc/device-tree/model
  cat_file /proc/device-tree/compatible
  cat_file /proc/uptime
  cat_file /proc/loadavg
  for f in /etc/os-release /etc/issue /etc/version /etc/release /etc/hostname /etc/buildroot-release /etc/*release*; do [ -f "$f" ] && cat_file "$f" 2000; done
  show env
  echo "--- commands available"
  for c in busybox sh ash bash awk sed grep find od hexdump xxd strings gzip zcat tar dd perl python python3 lua luajit gdb strace ldd readelf objdump file fbset aplay amixer i2cdetect i2cdump lsusb lspci lsmod modinfo top free vmstat iostat perf md5sum sha256sum timeout nproc getconf sysctl ifconfig ip route wget curl nc telnet ssh dropbear sync mount df du ls date hwclock fdisk blkid hdparm mmc; do
    p=$(command -v $c 2>/dev/null); echo "$c: ${p:-no}"
  done
  echo
  show busybox
} > "$P/01_system.txt" 2>&1

# ---------------------------------------------------------------- 02 CPU
note "02 cpu"
{
  echo "######## 02 CPU"
  cat_file /proc/cpuinfo 12000
  cat_file /sys/devices/system/cpu/online
  cat_file /sys/devices/system/cpu/possible
  cat_file /sys/devices/system/cpu/present
  cat_file /sys/devices/system/cpu/offline
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    echo "--- $c"
    dump_tree "$c"
  done
  echo "--- cpufreq policies"
  dump_tree /sys/devices/system/cpu/cpufreq
  echo "--- clocksource / timers"
  dump_tree /sys/devices/system/clocksource
  dump_tree /sys/devices/system/clockevents
  cat_file /proc/timer_list 3000
  cat_file /proc/interrupts
  cat_file /proc/stat
  if have getconf; then show getconf -a; fi
} > "$P/02_cpu.txt" 2>&1

# ---------------------------------------------------------------- 02b quick hardware evidence (done early in case the run is interrupted)
note "02b hwprobe"
{
  echo "######## 02b NATIVE PROBE (ISA tests, FPU speed, limits, framebuffer ioctls, memory limit, storage speed)"
  if [ -x "$DIR/hwprobe" ]; then lim 240 $RUN "$DIR/hwprobe" "$SD"; else echo "hwprobe missing"; fi
  echo
  echo "--- kernel FPU emulator counters (debugfs), before and after a burst of FP instructions"
  DBG=/sys/kernel/debug
  MOUNTED_HERE=0
  if [ -d $DBG ] && [ ! -d $DBG/mips ]; then mount -t debugfs none $DBG 2>/dev/null && MOUNTED_HERE=1; fi
  if [ -d $DBG/mips/fpuemustats ]; then
    for f in $DBG/mips/fpuemustats/*; do echo "before $(basename $f) = $(cat $f 2>/dev/null)"; done
    [ -x "$DIR/hwprobe" ] && $RUN "$DIR/hwprobe" "$SD" fp >/dev/null 2>&1
    for f in $DBG/mips/fpuemustats/*; do echo "after  $(basename $f) = $(cat $f 2>/dev/null)"; done
  else
    echo "fpuemustats not available ($(ls $DBG 2>&1 | head -c 300))"
  fi
  ls $DBG/mips 2>&1 | head -n 40
  [ $MOUNTED_HERE = 1 ] && umount $DBG 2>/dev/null
} > "$P/02b_hwprobe.txt" 2>&1

# ---------------------------------------------------------------- 03 memory
note "03 memory"
{
  echo "######## 03 MEMORY"
  cat_file /proc/meminfo 6000
  cat_file /proc/vmstat 6000
  cat_file /proc/buddyinfo
  cat_file /proc/zoneinfo 5000
  cat_file /proc/swaps
  cat_file /proc/iomem 6000
  cat_file /proc/ioports 3000
  cat_file /proc/sys/vm/overcommit_memory
  cat_file /proc/sys/vm/swappiness
  cat_file /proc/sys/vm/min_free_kbytes
  cat_file /proc/sys/vm/vfs_cache_pressure
  cat_file /proc/sys/vm/dirty_ratio
  cat_file /proc/sys/vm/dirty_background_ratio
  have free && show free
  cat_file /proc/slabinfo 3000
} > "$P/03_memory.txt" 2>&1

# ---------------------------------------------------------------- 04 device tree
note "04 device tree"
{
  echo "######## 04 DEVICE TREE (every property)"
  lim 90 dt_dump
} > "$P/04_devicetree.txt" 2>&1

# ---------------------------------------------------------------- 05 sysfs
note "05 sysfs"
{
  echo "######## 05 SYSFS (selected subtrees)"
  for t in /sys/class/thermal /sys/class/power_supply /sys/class/hwmon /sys/class/leds /sys/class/backlight /sys/class/graphics \
           /sys/class/mmc_host /sys/class/sound /sys/class/input /sys/class/rtc /sys/class/net /sys/class/misc /sys/class/devfreq \
           /sys/class/regulator /sys/class/pwm /sys/class/gpio /sys/class/udc /sys/class/watchdog /sys/class/i2c-dev /sys/class/spi_master \
           /sys/class/drm /sys/class/display /sys/class/video4linux /sys/class/tty /sys/block /sys/bus/mmc /sys/bus/platform/drivers \
           /sys/bus/usb/devices /sys/bus/i2c /sys/bus/spi /sys/kernel/config/usb_gadget /sys/devices/platform /sys/module/printk /sys/firmware; do
    [ -e "$t" ] || { echo "== $t absent"; continue; }
    echo "=================== $t"
    lim 40 dump_tree "$t"
  done
  echo "=================== symlinks of /sys/class/* (which hardware exists)"
  for d in /sys/class/*; do echo "$d: $(ls $d 2>/dev/null | tr '\n' ' ')"; done
} > "$P/05_sysfs.txt" 2>&1

# ---------------------------------------------------------------- 06 kernel
note "06 kernel"
{
  echo "######## 06 KERNEL"
  echo "--- dmesg (complete)"; dmesg 2>&1
  echo
  cat_file /proc/modules 6000
  cat_file /proc/filesystems
  cat_file /proc/devices 5000
  cat_file /proc/misc
  cat_file /proc/bus/input/devices 6000
  cat_file /proc/tty/drivers
  cat_file /proc/asound/cards
  cat_file /proc/asound/pcm
  cat_file /proc/asound/devices
  cat_file /proc/asound/version
  for f in /proc/asound/card*/*; do [ -f "$f" ] && cat_file "$f" 2000; done
  echo "--- kernel config (/proc/config.gz)"
  if [ -r /proc/config.gz ]; then (zcat /proc/config.gz 2>/dev/null || gzip -dc /proc/config.gz 2>/dev/null) | head -n 4000; else echo "absent"; fi
  echo "--- module files"
  find / -xdev -name '*.ko' 2>/dev/null | head -n 400
  find /lib/modules -name '*.ko' 2>/dev/null | head -n 400
  echo "--- sysctl"
  have sysctl && sysctl -a 2>/dev/null | head -n 600
} > "$P/06_kernel.txt" 2>&1

# ---------------------------------------------------------------- 07 root filesystem and software
note "07 filesystem"
{
  echo "######## 07 ROOT FILESYSTEM AND SOFTWARE"
  show ls -la /
  for d in /bin /sbin /usr/bin /usr/sbin /lib /usr/lib /etc /opt /root /home /var /run /tmp /dev /mnt /mnt/sdcard /mnt/sdcard/cubegm /mnt/sdcard/frogui; do
    echo "--- ls -la $d"; ls -la "$d" 2>&1 | head -n 500; echo
  done
  echo "--- glibc banner"; /lib/libc.so.6 2>&1 | head -n 6; ls /lib/libc-* /lib/ld-* /lib/libm-* /lib/libpthread* 2>&1
  echo "--- shared libs on the device"; ls -l /lib/*.so* /usr/lib/*.so* 2>/dev/null | head -n 300
  echo "--- init and startup scripts"
  for f in /init /linuxrc /etc/inittab /etc/fstab /etc/profile /etc/rcS /etc/init.d/* /etc/rc.d/* /etc/rc.local /etc/modules /etc/mdev.conf /etc/network/interfaces /etc/sysctl.conf /usr/bin/*.sh /bin/*.sh /sbin/*.sh /mnt/sdcard/cubegm/*.sh /mnt/sdcard/*.sh; do
    case "$f" in *wpa*|*passwd*|*shadow*|*key*|*secret*|*psk*|*ssh*) continue;; esac
    [ -f "$f" ] && [ ! -L "$f" ] && { echo "######## $f"; head -c 12000 "$f" | tr -c '[:print:]\n\t' '.'; echo; }
  done
  echo "--- processes"
  ps 2>&1 | head -n 80
  have top && { echo "--- top"; top -b -n 1 2>&1 | head -n 40; }
  echo "--- md5 of system binaries"
  have md5sum && md5sum /mnt/sdcard/cubegm/picoarch /mnt/sdcard/cubegm/*.so /bin/busybox /sbin/init /usr/bin/* 2>/dev/null | head -n 200
  echo "--- /tmp small files"
  for f in /tmp/*; do [ -f "$f" ] && [ ! -L "$f" ] && { echo "## $f ($(wc -c < "$f") bytes)"; head -c 600 "$f" | tr -c '[:print:]\n\t' '.'; echo; }; done
} > "$P/07_filesystem_software.txt" 2>&1

# ---------------------------------------------------------------- 08 storage
note "08 storage"
{
  echo "######## 08 STORAGE"
  cat_file /proc/partitions
  cat_file /proc/mounts
  cat_file /proc/diskstats
  show df
  have df && show df -h
  for b in /sys/block/mmcblk*; do
    echo "--- $b"; dump_tree "$b"
    dump_tree "$b/device" 2>/dev/null
  done
  show ls -l /dev/mmcblk* /dev/block /dev/disk
  echo "--- SD card FAT info (first sector, hex)"
  have od && head -c 64 /dev/mmcblk0p1 2>/dev/null | od -An -tx1
} > "$P/08_storage.txt" 2>&1

# ---------------------------------------------------------------- 09 display, audio, input, power
note "09 peripherals"
{
  echo "######## 09 DISPLAY, AUDIO, INPUT, POWER"
  show ls -l /dev/fb* /dev/dis* /dev/hcge* /dev/snd /dev/input /dev/mtp_usb /dev/ttyS* /dev/tty[0-9]* /dev/video* /dev/i2c* /dev/spidev* /dev/rtc* /dev/watchdog* /dev/mem /dev/gpiochip*
  have fbset && show fbset -i
  cat_file /sys/class/graphics/fb0/modes
  cat_file /sys/class/graphics/fb0/virtual_size
  cat_file /sys/class/graphics/fb0/bits_per_pixel
  cat_file /sys/class/graphics/fb0/stride
  cat_file /sys/class/graphics/fb0/name
  have aplay && show aplay -l
  have amixer && show amixer
  have lsusb && show lsusb
  echo "--- battery and power supplies"
  for p in /sys/class/power_supply/*; do echo "== $p"; dump_tree "$p"; done
  echo "--- speaker output pin (device tree)"
  cat_file /proc/device-tree/panel/speaker-output
} > "$P/09_peripherals.txt" 2>&1


# ---------------------------------------------------------------- 11 benchmarks with power / clock sampling
sample() {   # one line: time, cpu freqs, temps, battery
  line="$(date +%H:%M:%S 2>/dev/null)"
  for c in /sys/devices/system/cpu/cpu[0-9]*; do [ -r $c/cpufreq/scaling_cur_freq ] && line="$line cpu$(basename $c | tr -d cpu)=$(cat $c/cpufreq/scaling_cur_freq)"; done
  for z in /sys/class/thermal/thermal_zone*; do [ -r $z/temp ] && line="$line temp=$(cat $z/temp)"; done
  for b in /sys/class/power_supply/*; do
    for f in capacity voltage_now current_now status; do [ -r $b/$f ] && line="$line $(basename $b)/$f=$(cat $b/$f 2>/dev/null)"; done
  done
  echo "$line"
}
sampler() { while :; do sample; sleep 2; done; }
phase() { echo "=== $1" >> "$P/11b_power_samples.txt"; }

note "11 benchmarks"
{
  echo "######## 11 BENCHMARKS AND POWER (samples every 2 s: cpu clocks, temperature, battery)"
  echo "(clock/temperature/battery samples are in 11b_power_samples.txt)"
  : > "$P/11b_power_samples.txt"
  phase "idle 10 s"
  i=0; while [ $i -lt 5 ]; do sample >> "$P/11b_power_samples.txt"; sleep 2; i=$((i+1)); done
  sampler >> "$P/11b_power_samples.txt" & SP=$!
  phase "native benchmark, port flags"
  echo "--- native benchmark, port flags"
  if [ -x "$DIR/bench" ]; then $RUN "$DIR/bench"; echo "exit code $?"; else echo "bench missing"; fi
  phase "native benchmark, -mtune=74kc -mdspr2 build (only meaningful if the DSP instructions above EXECUTE)"
  echo "--- native benchmark, -mtune=74kc -mdspr2 build"
  if [ -x "$DIR/bench74" ]; then $RUN "$DIR/bench74"; echo "exit code $?"; else echo "bench74 missing"; fi
  phase "LuaJIT"
  echo "--- LuaJIT"
  if [ -x "$DIR/luajit" ]; then $RUN "$DIR/luajit" "$DIR/bench.lua"; echo "exit code $?"; else echo "luajit missing"; fi
  kill $SP 2>/dev/null; wait $SP 2>/dev/null
  phase "cool-down 10 s"
  i=0; while [ $i -lt 5 ]; do sample >> "$P/11b_power_samples.txt"; sleep 2; i=$((i+1)); done
} > "$P/11_benchmarks_power.txt" 2>&1

# ---------------------------------------------------------------- bundle
note "bundle"
{
  for f in "$P"/*.txt; do echo; echo "################################################################"; echo "# FILE $(basename "$f")"; echo "################################################################"; cat "$f"; done
} > "$SD/probe_report.txt" 2>&1
if have tar && have gzip; then tar czf "$SD/probe_bundle.tgz" -C "$SD" probe probe_report.txt 2>/dev/null; fi
note "DONE"
sync
# visible end signal: the handheld switches itself off when the probe has finished
if [ -z "$RUN" ] && [ -z "${NO_POWEROFF:-}" ] && [ -x /mnt/sdcard/cubegm/powergpio ]; then sleep 2; sync; /mnt/sdcard/cubegm/powergpio; fi
exit 0
