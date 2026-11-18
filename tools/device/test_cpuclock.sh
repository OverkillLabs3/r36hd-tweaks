#!/bin/bash
# Dry run of cpuclock.sh on the PC with fake devmem/probe/usleep. Checks the arithmetic and the guard logic.
T=/tmp/cpuclk_test; rm -rf $T; mkdir -p $T/bin $T/sd/cubegm/logs $T/sd/roms/ports/r36probe
cat > $T/bin/devmem <<'EOF'
#!/bin/bash
# devmem ADDR WIDTH [VALUE] backed by files; 0x70 mirrors the source bits of 0x74 once bit 22 is written
R=/tmp/cpuclk_test/regs; mkdir -p $R
f=$R/$(printf '%x' $(( $1 )))
if [ -n "$3" ]; then
  v=$(( $3 & 0xffffffff ))
  printf '0x%08X\n' $v > $f
  if [ "$(printf '%x' $(( $1 )))" = "18800074" ] && [ $(( v & 0x400000 )) -ne 0 ]; then
     src=$(( (v >> 8) & 7 ))
     printf '0x%08X\n' $(( 0x0007F010 | (src << 8) )) > $R/18800070
  fi
else
  [ -f $f ] && cat $f || echo 0x00000000
fi
EOF
cat > $T/bin/usleep <<'EOF'
#!/bin/bash
exit 0
EOF
cat > $T/sd/roms/ports/r36probe/cpuclk_probe <<'EOF'
#!/bin/bash
src=$(( ($(printf '%d' $(/tmp/cpuclk_test/bin/devmem 0x18800070 32)) >> 8) & 7 ))
if [ "$src" = 7 ]; then echo "dependent add chain: best 0.0150 s for 20480000 adds -> CPU ~ 1119.1 MHz (checksum 1)"; echo "hardware timer clock: 19000 ms for 20480000 adds -> CPU ~ ${FAKE_MHZ:-1250.0} MHz (true, 24 MHz timer)"; else echo "dependent add chain: best 0.0183 s for 20480000 adds -> CPU ~ 1119.1 MHz (checksum 1)"; echo "hardware timer clock: 19000 ms for 20480000 adds -> CPU ~ 1188.0 MHz (true, 24 MHz timer)"; fi
EOF
chmod +x $T/bin/* $T/sd/roms/ports/r36probe/cpuclk_probe
mkdir -p $T/regs
printf '0x00000510\n' > $T/regs/18800074; printf '0x0007F510\n' > $T/regs/18800070
printf '0x81760900\n' > $T/regs/18800380; printf '0x053C3002\n' > $T/regs/1880007c
sed -e "s#/mnt/sdcard#$T/sd#g" "$(dirname "$0")/cpuclock.sh" > $T/cpuclock.sh
export PATH=$T/bin:$PATH
run() { sh $T/cpuclock.sh; echo "--- log:"; cat $T/sd/cubegm/logs/cpuclock.log 2>/dev/null; echo "regs: 74=$(cat $T/regs/18800074) 70=$(cat $T/regs/18800070) 380=$(cat $T/regs/18800380) 7c=$(cat $T/regs/1880007c)"; ls $T/sd/cubegm; }
echo "### 1. no config"; run
echo "### 2. 1250 MHz, probe says faster"; echo 1250 > $T/sd/cubegm/cpu_mhz.txt; run; sleep 21; echo "flag after 21 s: $(ls $T/sd/cubegm | grep flag)"
echo "### 3. hang simulation: flag left behind"; touch $T/sd/cubegm/cpu_try.flag; run
echo "### 4. config with probe NOT faster"; echo 1300 > $T/sd/cubegm/cpu_mhz.txt; printf '0x00000510\n' > $T/regs/18800074; printf '0x0007F510\n' > $T/regs/18800070
FAKE_MHZ=1121.0 run
echo "### 5. bad value"; echo 99999 > $T/sd/cubegm/cpu_mhz.txt; run
expect=$(( (12500 - 24) / 24 )); echo "expected divider for 1250: $expect = $(printf '0x%x' $expect)"
