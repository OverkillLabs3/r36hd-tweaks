#!/bin/sh
# inventory.sh: read-only snapshot of the running console for offline study (run over the USB link).
#   inventory.sh <out dir>
# Reads /proc, the device nodes, chosen sysfs and debugfs files, the flash partitions (mtdblock, read only),
# the initramfs root (files not under the SD bind mounts) and every process. Nothing is written outside <out dir>.
O=${1:-/mnt/sdcard/live_dump}
mkdir -p $O/proc $O/flash $O/procs $O/sys
for f in cpuinfo meminfo iomem ioports interrupts modules mounts partitions mtd cmdline version devices misc \
         filesystems buddyinfo vmstat zoneinfo slabinfo pagetypeinfo softirqs stat uptime loadavg locks \
         execdomains swaps kallsyms timer_list sched_debug schedstat consoles crypto diskstats vmallocinfo \
         dma-mappings key-users sysvipc/shm sysvipc/msg sysvipc/sem net/dev net/protocols net/unix \
         config.gz; do
    [ -r /proc/$f ] && cat /proc/$f > $O/proc/$(echo $f | tr / _) 2>/dev/null
done
sysctl -a > $O/proc/sysctl.txt 2>/dev/null
dmesg > $O/dmesg.txt 2>/dev/null
ls -laR /dev > $O/dev.txt 2>/dev/null
ls -la /sys/class/* > $O/sys/class.txt 2>/dev/null
ls -laR /sys/kernel/debug > $O/sys/debugfs_tree.txt 2>/dev/null
ls -la /sys/bus/platform/devices/ /sys/bus/platform/drivers/ > $O/sys/platform.txt 2>/dev/null
ls -laR /sys/firmware > $O/sys/firmware_tree.txt 2>/dev/null
ls -laR /sys/module > $O/sys/module_tree.txt 2>/dev/null
# module parameters (plain values)
for p in /sys/module/*/parameters/*; do [ -f "$p" ] && echo "$p = $(cat "$p" 2>/dev/null)"; done > $O/sys/module_params.txt
# cpufreq, devfreq, thermal, regulators, clocks, gpio (attribute files only, one level)
for d in /sys/devices/system/cpu/cpu0/cpufreq /sys/class/devfreq/* /sys/class/thermal/* /sys/class/regulator/* \
         /sys/class/backlight/* /sys/class/power_supply/* /sys/class/input/input* /sys/class/sound/* \
         /sys/class/graphics/* /sys/class/mmc_host/* /sys/class/udc/* /sys/class/hwmon/* /sys/class/leds/* \
         /sys/class/misc/* /sys/class/watchdog/* /sys/class/rtc/* /sys/class/i2c-adapter/* /sys/class/spi_master/*; do
    [ -d "$d" ] || continue
    for a in "$d"/*; do [ -f "$a" ] && [ -r "$a" ] && echo "$a = $(head -c 300 "$a" 2>/dev/null | tr '\n' ' ')"; done
done > $O/sys/attrs.txt 2>/dev/null
# device tree as the kernel sees it
[ -d /proc/device-tree ] && tar cf $O/sys/device-tree.tar -C /proc device-tree 2>/dev/null
# debugfs files that are plain text reports (no register pokes)
for f in clk/clk_summary gpio pinctrl/pinctrl-handles pinctrl/pinctrl-maps wakeup_sources suspend_stats \
         dma_buf/bufinfo ion/heaps sched_features memblock/memory memblock/reserved; do
    [ -r /sys/kernel/debug/$f ] && cat /sys/kernel/debug/$f > $O/sys/debug_$(echo $f | tr / _) 2>/dev/null
done
# flash partitions
for i in 0 1 2 3 4; do dd if=/dev/mtdblock$i of=$O/flash/mtd$i.bin bs=4096 2>/dev/null; done
# initramfs root: everything not on the SD card
tar cf $O/rootfs.tar -C / --exclude=./proc --exclude=./sys --exclude=./dev --exclude=./mnt --exclude=./media \
    --exclude=./lib --exclude=./usr --exclude=./bin --exclude=./sbin --exclude=./etc --exclude=./tmp . 2>/dev/null
ls -laR /tmp /run /dev/shm > $O/tmp_tree.txt 2>/dev/null
# processes
ps -o pid,ppid,user,vsz,rss,stat,args > $O/ps.txt 2>/dev/null || ps > $O/ps.txt
for p in /proc/[0-9]*; do
    n=${p#/proc/}
    { echo "== $n $(cat $p/cmdline 2>/dev/null | tr '\0' ' ')"; cat $p/status 2>/dev/null; echo "-- stat"; cat $p/stat 2>/dev/null
      echo "-- maps"; cat $p/maps 2>/dev/null; echo "-- fds"; ls -la $p/fd 2>/dev/null
      echo "-- threads"; for t in $p/task/*; do echo "$(cat $t/comm 2>/dev/null) $(cat $t/stat 2>/dev/null)"; done
      echo "-- environ"; cat $p/environ 2>/dev/null | tr '\0' '\n'; } > $O/procs/$n.txt 2>/dev/null
done
# CPU use over 10 s, every thread
for t in /proc/[0-9]*/task/*; do echo "$t $(cut -d' ' -f14,15 $t/stat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > $O/cpu_a.txt
cat /proc/stat | head -1 > $O/cpu_total_a.txt
sleep 10
for t in /proc/[0-9]*/task/*; do echo "$t $(cut -d' ' -f14,15 $t/stat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > $O/cpu_b.txt
cat /proc/stat | head -1 > $O/cpu_total_b.txt
cat /proc/interrupts > $O/interrupts_b.txt
cd $O/.. && tar cf $(basename $O).tar $(basename $O) && echo "done $(ls -la $(basename $O).tar)"
