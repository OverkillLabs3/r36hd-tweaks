# R36HD device tweaks

Tools, source and research notes for the R36HD handheld (HiChip HC1600 SoC, one MIPS 74Kc core at 1188 MHz with FPU and DSP r2, 128 MB RAM, a second MIPS 24KE core that runs the video and audio firmware). The device tools run from the SD card or over the USB link, and nothing here writes the SPI flash.

The tweaks were built to make native libretro ports run faster on this device, but they are independent of any game.

## What is in here

| Path | What it does | Status |
|---|---|---|
| `common/mmz_heap.c` | Uses the idle video/audio pool (`/dev/mmz`, up to 48 MB per program) as the program's heap | works |
| `common/bigtlb.c`, `tools/device/kx/` | Wires 8 MB, 2 MB and 512 KB TLB pages over the pool through a one-shot kernel-mode payload. A 24 MB pointer chase drops from 399 ns to 219 ns per step | works |
| `common/fastmem.c` | `LD_PRELOAD` memset and memcpy using `pref 30` on whole cache lines | works |
| `tools/device/sdfast.c` | Raises the SD clock to 50 MHz at run time with a write check and a fall back to 20 MHz | works, can corrupt FAT after a power loss, keep backups |
| `tools/device/lowram*.sh` | Start-up script: read-ahead, swappiness, kernel print level, pool and TLB set-up | works |
| `common/direct_present.h` | Hands a frame to the display driver without the frontend's software scaler | works |
| `common/core2.c`, `tools/device/core2/` | Starts a worker task on the second core and runs relocatable integer jobs on it | works for jobs, no game gain proven yet |
| `common/r36prof.c` | Sampling profiler (SIGPROF) for on-device profiles | works |
| `tools/r36.py`, `tools/fbshot.py`, `tools/device/r36agent.c`, `tools/device/usblink.sh` | PC side and device side of a USB serial link: run commands, push and pull files, take framebuffer screenshots without a network (the kernel has no IPv4) | works |
| `tools/device/wdtd.c` | Watchdog helper for risky experiments: arm it, a hang resets the console | works |
| `tools/regsweep.py`, `tools/gatescan.py`, `tools/device/regscan.c` | Register and clock gate scans over the link. Some pages hang the bus, the watchdog resets it | works, deliberate resets can dirty the SD card |
| `tools/device/ddrpll.c`, `ddrsel.c`, `spiid.c`, `efuse_read.c`, `deepdump.c`, `memdump.c` | Read only probes: DDR PLL and select, SPI flash id, efuses, boot ROM, memory | works |
| `tools/device/audbl.c`, `joyinject.c`, `joyread.c` | Audio and backlight, and button reading and injection through the input shared memory | works |
| `tools/kmod/` | Build and ABI checks for out of tree kernel modules against the vendor 4.4.186 kernel (zram works with the settings in `build_kmods.sh`) | works |
| `tools/ddr_patch.py` | Rewrites DDR words in a dump of the SPI flash. Output not flashed or tested on hardware, see the DDR notes in `docs/KERNEL_ANALYSIS.md` | experimental |
| `docs/DEVICE_AUDIT.md` | Every subsystem checked, with status | |
| `docs/KERNEL_ANALYSIS.md` | Boot chain, kernel and firmware layout, how the boot loader decrypts them | |
| `tools/hc_decrypt.py`, `tools/hc_unpack.py`, `tools/hc_initramfs.py`, `tools/hc_cpio_extract.py`, `tools/hc_repack_kernel.py` | Decrypt and unwrap the kernel, firmware and device tree files from the card, locate and extract the built-in initramfs, repack a kernel | |
| `tools/device/probe.sh`, `inventory.sh`, `hwprobe.c`, `bench.c`, `spin.c`, `mmzmax.c`, `clocktest*.sh`, `buttons.sh`, `joylog.c` | Read-only inventory, CPU and memory benchmarks, input logging | works |

## Measured facts

* RAM is 128 MB DDR3-1066. The DDR controller and clock are set by a register script in the SPI boot loader, so changing the speed or size means rewriting the flash (see section 13 of the kernel notes). The script already holds the 256 MB configuration, but the upper 128 MB mirrors the lower half.
* Linux keeps about 43 MB, so a program has about 40 MB before the pool.
* L1 data cache is 32 KB, there is no L2. Load latency is 13 ns inside 16 KB, 118 ns at 64 KB and 320 ns at 8 MB. Memory is the bottleneck.
* The FPU runs at half rate. Config7 reads `0x80030800` and the FPR field is read-only.
* The second core has no FPU, runs at about 800 MHz and does about 60 percent of the main core's integer work. Both cores together reach 1.76x on a pure integer loop. Changing its clock at run time freezes the display.
* CPU clocks of 1320 and 1400 MHz were not stable in our tests (crashes, display artifacts). Stock 1188 MHz is the safe choice.
* The video and audio firmware lives on the second core. Entering it needs the virtuart shell: the `mv` handler is patched with a four instruction jump through `/dev/mem`, run once, and restored.

## Warnings

* Use at your own risk. A wrong write to the clock registers or to the SD controller can freeze the console until a power cycle. A power loss during an SD write can damage the FAT file system. Keep a copy of your card.
* Nothing in this repository writes the boot loader, the kernel or the SPI flash to a device. `tools/ddr_patch.py` only edits a dump on the PC. Do not add that unless you own a hardware programmer for recovery.
* Always read `/dev/virtuart` into a buffer of at least 512 bytes. The driver copies 512 bytes whatever length you ask for.
* No firmware, kernel image or decrypted dump is included. You need your own device and your own card.

## Building

The kernel module scripts in `tools/kmod/` read an unpacked kernel from `build/kernel/` (made with `tools/hc_unpack.py`, plus `exports.txt` and `kallsyms.txt` from your own console).

The device tools are C for MIPS32r2 hard float with the SF3000 buildroot toolchain (GCC 6.3, glibc 2.20). `tools/device/build_*.sh` and `tools/device/core2/build.sh` show the exact commands. Relocatable second-core objects use `tools/device/core2/build_obj.sh` and `c2link.py`.

## Credits and license

MIT, see `LICENSE`. Findings on the boot chain build on the work in the community notes for this device family. If you reuse these tools or notes, a link back is appreciated.
