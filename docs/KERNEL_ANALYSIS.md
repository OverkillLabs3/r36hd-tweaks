# R36HD boot chain, kernel and firmware

What is known about the HiChip HC1600 based R36HD, from decrypted and decompiled code and from measurements on a running console. Anything not listed here is unknown.

## 1. Boot chain

| Stage | Where | What |
|---|---|---|
| 1 | SPI flash `mtd1` 0x800 | Copies and XOR-decodes stage 2 (key 0x7e3f9c2d). Plain code. |
| 2 | SPI flash `mtd1`, runs at 0x9fe00000 | Reads flash 0x4000.. (0x4a620 bytes) to RAM 0xa9e70000 and decrypts it with Speck128/128-CTR (constants at 0x31e0 and 0x31d0). |
| 3 | RAM 0x89e70000 | 12 KB start code. LZMA-inflates the rest to 0x89da0000 and jumps there. |
| 4 | RAM 0x89da0000 | `hcboot`: FreeRTOS plus a U-Boot style `bootm`. Loads the device tree(s), the video firmware and the kernel, boots the second core, then boots Linux. |
| 5 | Linux 4.4.186 PREEMPT, entry 0x803337c0 | Built-in initramfs (ramfs root), then the init script `S99app`. |

RAM base is physical 0x08000000. The RAM offsets in the device tree start at 0.

## 2. Image format on the SD card

Kernel, video firmware and device tree on the card are `16-byte header + ciphertext`. `header[0:12]` is a ChaCha20 nonce and `header[12:16]` the payload length (little endian). The 32-byte key is a constant in `hcboot` (file offset 0x9cc98), hidden by a 32-step byte loop:

```
5af3abd80cb4fd74b12d83d76370dc35b5d69f67653b93fb985075b3faf60064
```

Counter starts at 0, 20 rounds. The key is not tied to the chip. `tools/hc_decrypt.py` decrypts and encrypts, `tools/hc_unpack.py` unwraps the uImage (gzip). `hcboot` checks the uImage header CRC and data CRC32.

File names are chosen through a table: `vmlinux.uImage` maps to `advapi32.dll`, `dtb.bin` to `Bubbles.scr`, `avp.uImage` to `ApplicationFrame.dll`. This console uses the second names.

A modified boot file can leave the console unable to start. Keep the original files and a way to restore them from a PC.

## 3. Device tree

`hcboot` loads two trees: the base tree from the SPI flash partition `dtb` (`mtd2`, 36 KB) and the SD tree `Bubbles.scr`. It copies only these properties from the SD tree into the base tree:

* `/hcrtos/memory-mapping/{bootmem,sysmem,mmz0,mmz1}` `reg`
* `/hcrtos/boot-fb0` and `/soc/fb0@18808000`: `buffer-phy-static`, `buffer-header-size`
* `/memory` `reg`
* `/hcrtos/de-engine` `tvtype`, `/hcrtos/i2so` `volume`, the two `power_button`/`power_pin` values

Everything else comes from the flash tree and cannot be changed from the SD card: CPU clock (`clock`, `mcpu-dig-pll-clk`), all `mm-*` and `avc-*` buffer sizes, drivers and pins. Confirmed on a console: a card tree with `cpu@0 clock=7` left `/proc/device-tree/cpus/cpu@0/clock` at 5.

## 4. CPU clock

* Source table from `hcboot`: 0 = 594, 1 = 396, 2 = 297, 3 = 198, 4 = 900, 5 = 1188, 6 = 24 MHz, 7 = the PLL at the `mcpu-dig-pll-clk` value.
* PLL divider: `0x18800380 = ((MHz*10-0x18)/0x18)<<16 | 0x80000000`. Source: `0x18800074` bits 10:8, then bit 22 applies the change. `0x1880007c` bit 7 is needed for source 7. `0x18800070` reads back the source in use.
* The stock clock is 1188 MHz (source 5). Source 7 with a PLL value works at run time (1248, 1320 and 1400 MHz were applied and checked).
* `CLOCK_MONOTONIC` runs from the cp0 counter, which follows the CPU clock, so a loop timed with it gives the same number at any clock. The independent reference is the 24 MHz timer block at 0x18818a00 (milliseconds = `u32@0xa0c * 1000 + u16@0xa0a`).
* There is no voltage control and no cpufreq. At 1400 MHz the video path shows glitches. At 1320 MHz games crashed in random places (SIGBUS, SIGSEGV) and one did not start. 1188 MHz is the stable setting. Saves written during an unstable session can be damaged.

## 5. Memory

* 128 MB DDR3. The controller's size field reads 256 MB, but data is mirrored at +128 MB at every address checked (72 of 72 pairs), so address bit 27 is not decoded.
* DDR speed is 1066 MT/s, although the chip is rated 1600. It is programmed by the register script in stage 2 of the SPI flash (section 13) before any code on the card runs, so it cannot change at run time and would need the flash rewritten and the memory retrained. The script equals the vendor SDK 256M 1066 MHz configuration, which explains the 256 MB size field.
* RAM layout (offsets): Linux 0..0x2f91e50 (47.6 MB), the 14.5 MB panel frame buffer (720x1280x32, four pages), `sysmem` 11.8 MB, `mmz1` 4.7 MB, `mmz0` 52.4 MB up to 128 MB. `hcboot` sits at 0x1da0000. Growing Linux's `/memory` node overlaps the live frame buffer, so extra RAM for Linux is not possible this way.
* Linux reports about 43 MB total. The kernel has no shared memory support, so `tmpfs` is plain ramfs and nothing in `/tmp` can be swapped or evicted.

### The mmz pool

`mmz0` is idle unless the video firmware decodes H.264. Linux exposes it as `/dev/mmz`:

* `open("/dev/mmz", O_RDWR)`, `ioctl(fd, 0xC0101901, {0, 0x20, size, 0})`, `mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`. Free with `munmap` plus `ioctl(fd, 0xC0101902, ...)`.
* Blocks of 1 to 48 MB are granted, 52 MB fails. The mapping is cached and as fast as `malloc`. A block is freed when the process exits.
* The ioctl returns the block's KSEG0 address in word 3. `/proc/self/pagemap` shows 0 for these pages.
* The pool is not zeroed. `common/mmz_heap.c` zeroes every region it hands to glibc, because `calloc` assumes fresh top memory is zero.
* `common/mmz_heap.c` maps one block and points glibc's `__morecore` into it, with `M_MMAP_MAX=0` and `M_ARENA_MAX=1`. glibc 2.20 caps the mmap threshold at 512 KB on 32-bit, so this is the only way to keep big allocations in the pool.

## 6. Kernel facts

* 4.4.186 for MIPS32 R2. No zswap, cpufreq, cpuidle governors or hugetlbfs. zram is not built in but works as a module (section 14). `cpu_wait` is set, so idle uses the MIPS `wait` instruction. Block scheduler is `noop`, default read-ahead 128 KB.
* Kernel modules are supported (`modules_disabled = 0`, `/sbin/insmod`), vermagic `4.4.186-release preempt MIPS32_R2 32BIT`, but there is no module unload and the `struct module` layout must match, so a module could never be removed.
* `/dev/mem` reaches kernel RAM (no STRICT_DEVMEM).
* `S99app` waits for the card, bind-mounts `/lib` and `/usr/lib` (and `/usr`, `/bin`, `/sbin`, `/etc` if a `rootfs` folder exists on the card), decrypts `driver.so` and `rkgame` into `/tmp/cubegm`, creates a 128 MB `pagefile.sys` and swaps on it when MemTotal is 128000 kB or less, then starts `icube.sh`.
* `crc_validator` checks 18 files (list in `cubegm/AcXtrnal`, XOR 0x5a). On a mismatch it shows a "game corrupted" picture and boot continues. The kernel, `ApplicationFrame.dll` and `Bubbles.scr` are not in the list.
* Kernel-mode code from user space: the `virtuart` driver's `poll` pointer in its `fops` (physical 0x0039b1b8, stock 0x802bc8f4) can be pointed at a payload for one call. See `tools/device/kx/`.

## 7. SD card clock

The device tree caps the controller at 20 MHz: the mmc node has `clock-freq-min-max = <100000 20000000>`. The card is a high-speed card (50 MHz). A run-time fix needs no new kernel: `tools/device/sdfast.c` finds `struct mmc_host` in kernel RAM by its three consecutive words (f_min 100000, f_max 20000000, f_init 400000, exactly one match required), sets f_max to 50000000, and writes 50000000 to `/sys/kernel/debug/mmc0/clock` so the mmc core changes the clock under the host claim. It then writes 2 MB at the new clock, reads it back cold and compares, and restores 20 MHz on any difference.

Measured: 12 MB cold read 7.96 MB/s at 20 MHz, 16.69 MB/s at 50 MHz. The setting resets on reboot.

## 8. Second CPU core

The video and audio firmware (`ApplicationFrame.dll`, gzip uImage, load and entry 0x83da4000, FreeRTOS with a NuttX shell) runs on a second MIPS core clocked at 800 MHz.

* PRId 0x00019655 (24KE family). Config1 bit 0 is 0: **no FPU**, integer and fixed point only. Cache lines are 16 bytes.
* About 60 percent of the main core on simple integer code (151.7 M against 252 M dependent adds per second). Both cores together reach 1.76x on integer work.
* Both cores see the same physical RAM, at 0x80000000 | P (cached) or 0xA0000000 | P (uncached). Caches are not coherent. Linux uses `cacheflush()`, the second core uses `cache 0x11` and `cache 0x15`.
* Changing its clock at run time freezes the display. A job that faults stops display and sound until a power cycle; nothing persists.
* Linux reaches the firmware shell through `/dev/virtuart` (the device tree sets `/hcrtos/stdio serial0 = /hcrtos/virtuart`). The prompt is `hc1600a@dbE3100v20#`. `mw ADDR` prints a word and `mw ADDR=VALUE` writes one, inside the `nsh` sub-shell. Always read `/dev/virtuart` into a buffer of at least 512 bytes: the driver copies 512 bytes whatever length is asked.
* The shell copies its command table at start-up, so editing the table does nothing. Entry method: write a four instruction jump (`lui t9`, `ori t9`, `jr t9`, `nop`) over the first words of the `mv` handler code at 0x83e503a0 (physical 0x03e503a0, outside Linux RAM) through `/dev/mem`, run `mv <block> 0` in `nsh`, then restore the original words (`3c028403 00a03025 00802825 3c048402`, checked before any write). See `tools/device/core2/tramp.h`.
* Firmware helpers: `0x83de80d0(entry, name, stack, arg, prio, &handle)` creates a FreeRTOS task, `vTaskDelay` is at 0x83dea58c. `tools/device/core2/boot.c` uses them to start a worker task that polls a mailbox and runs jobs.
* A persistent worker answers an empty job in about 1.03 ms (one FreeRTOS tick) and fills 4 MB in 17.5 ms (228 MB/s). Keep jobs short, under about 100 ms.
* Relocatable objects: `tools/device/core2/build_obj.sh` and `c2link.py` turn freestanding C into a `.c2o` that `core2_load_obj()` places at any 64 KB aligned address and relocates (R_MIPS_32, R_MIPS_26, HI16 and LO16, where LO16 never changes for a 64 KB aligned base).

## 9. Main core (MIPS 74Kc)

* PRId 0x0001974c, Config 0x80240483, Config7 0x80030800. 32 KB I and 32 KB D cache, 4-way, 32 TLB pairs, FPU, DSP r2.
* Config7: ES = 0, PREF = 1 (a value of 2 lines gave no clear gain), branch prediction on, **FPR = 01: the FPU runs at half the core clock** and the field is read-only.
* Latency: 13 ns inside 16 KB, 118 ns at 64 KB, 320 ns at 8 MB. memcpy about 150 to 240 MB/s.
* With 4 KB pages 32 TLB entries cover 256 KB. Wiring 4 MB pages cut a 24 MB pointer chase from 399 ns to 219 ns per load. `common/bigtlb.c` wires up to six global entries (8 MB, 2 MB or 512 KB page pairs) over the pool, mapped at 0x40000000 + (PA mod 8 MB), using a kernel-mode payload (`tools/device/kx/tlbwire.S`). `tools/device/kx/tlbclear.c` removes every wired entry, and should run before each program so a crashed one cannot leave a stale mapping.
* `pref 30` on whole cache lines: memset 808 to 831 MB/s against 197 to 269 MB/s for glibc, memcpy with prefetch 237 to 347 MB/s against 204 to 262.

## 10. 2D engine

Registers at 0x18806000 (`/soc/ge`, IRQ 9), device `/dev/ge`, user library `libge.so`. DirectFB style API (`hcge_open`, `hcge_blit`, `hcge_stretch_blit`, `hcge_fill_rect`, `hcge_engine_sync` and others) with a command queue at `hcge_cmdq_paddr`, so buffers must be physically contiguous (the mmz pool qualifies). Formats ARGB, RGB16, RGB32, RGB24, LUT8, ARGB4444, ARGB1555, RGB555, RGB444. Blits support alpha blending, colorize, premultiply, colour keys, rotate 90/180/270, flips, colour matrix and convolution. It only does 2D work, so it cannot rasterize triangles.

## 11. USB serial link

The kernel has no IPv4, so there is no network. The USB port can still carry a CDC ACM gadget: `tools/device/usblink.sh` brings it up, `tools/device/r36agent.c` serves commands on `/dev/ttyGS0`, and `tools/r36.py` is the PC side (`ping`, `sh`, `put`, `get`, `push`, `run`, `stop`). Transfers above 1 MB go in 256 KB slices, each verified. `tools/fbshot.py` reads the frame buffer over the link and writes a PNG. The card's start-up script can launch the link at boot when the flag file `cubegm/usblink_auto` exists; that hook lives in `zhijack.sh` on the card, not in this repository.

## 12. Register sweeps and the watchdog

* Readable register pages: SPI flash controller 0x1882e000, USB 0x18844000 and 0x18850000, DSI 0x1884a000, MMC 0x1884c000, display control 0x1883a000, hwspinlock 0x18867000.
* Reading these pages hangs the bus: 0x1880b000 to 0x1880f000, 0x1881a000, 0x18828000 (Ethernet) and 0x18832000 (NAND controller). They are the blocks the device tree marks disabled, with their clocks gated.
* `tools/device/wdtd.c` arms the hardware watchdog around a risky access so a hang resets the console. The timeout register counts in units of the watchdog clock, check the value against the clock before trusting a short timeout.
* A watchdog reset is a power loss for the SD card: the FAT can come back dirty. Run `sync` before an experiment that may reset and check the card afterwards.
* PLL words in the system block: frequency is 24 MHz x (N + 1) / 10 with N the low 12 bits of the word at 0x380 (900 MHz at stock, the PLL behind CPU source 7), 0x390 (about 400 MHz, display engine), 0x3a0 (about 500 MHz, video engine), 0x3b0 (800 MHz, second core).

## 13. SPI flash, boot ROM and DDR

* The chip is a 25HD40 (`m25p80`), 512 KB. Partitions used here: `mtd1` boot stages, `mtd2` device tree (36 KB), `mtd4` `persistentmem` (64 KB, a table of tagged records with 4-byte checksums that changes between boots). `tools/device/flashdump.sh` dumps them.
* Header words at 0x40 of `mtd1`: SPI clock 50 MHz, the second core setting 0x03200007 (800 MHz, source 7) and the main core setting 0x04b00005 (1200 MHz, source 5). Offset 0x2b0 holds a USB device descriptor (vendor class), which suggests a USB download mode in the boot stages.
* Stage 2 (decoded, runs at 0x9fe00000 in on-chip RAM) carries a register script of 16-byte records: a zero word, a width (0x32 = 32 bit, 0x16 = 16 bit, 0x08 = byte), block and offset, value. It programs the DDR controller at 0x18801000 and PLL registers in block 0x18800000. The script starts at 0x940 and runs to 0xe10, after a table of the four block bases at 0x910 (0x18800000, 0x18801000, 0x1883e000, 0x18801800); the XOR-encoded region starts at 0xe14.
* The script equals the vendor SDK 256M 1066 MHz configuration on every address except `0x18801000` (ours 0x0d894084, SDK 0x05894084, only bit 27 differs). SDK values for 256M, 1066 / 1333 / 1600: `0x18801000` 05894084 / 6c904044 / 70994004; `0x18801004` 939100 / 93a100 / 93b100; `0x18801030` 5000 / 5800 / 6000; `0x18801033` 60 / e0 / 60; `0x1880108c` 1 / 0x12 / 0x13; `0x1883e034` 1f / 2a / 2a; `0x1883e048` 71947744 / 85589955 / 9d9dbb66; `0x1883e04c` 1562b360 / 19b35bc0 / 1a040400; `0x1883e054` 1930 / 1b50 / 1d70; `0x1883e05c` 8 / 10 / 18; the 1600 setting also changes `0x1883e004` to 0xf781 and `0x1883e068` to 0x910073c7. The chip is rated 1600 MT/s and runs at 1066.
* The first records of the script are byte writes: `0x18800074 = 0x10` (DDR clock select) and `0x18800076 = 0x20`, then a delay. The SDK files do the same (a read-modify-write that clears bits 5:4 and sets bit 21) with `0x10` for 1066 and `0x20` for 1333. The only code in stage 2 that touches `0x18800074` is the CPU clock function at 0x9fe02cf4 (bits 10:8 and bit 22), which is a different field. The live value is 0x510.
* `tools/ddr_patch.py` rewrites the 1333 words listed above plus the select byte at 0x940 (0x10 to 0x20) in a dump after checking every old value; the result matches the SDK 1333 file on every common register. No image has been written to a chip. Changing the DDR speed means rewriting the flash, and a hardware programmer is the only recovery if it does not boot.
* Whether the boot ROM checks a CRC over the first 4 KB is not established; the SDK documents only a size field.

## 14. Kernel modules

`tools/kmod/build_kmods.sh` builds modules against the vendor 4.4.186 tree. `zram` loads and works when the configuration matches the vendor block layer: `LBDAF` off (32-bit sector numbers), `BLK_DEV_BSG` and `BLK_DEV_BSGLIB` on, `SLAB` instead of `SLUB`, `FORCE_MAX_ZONEORDER` 14. With the stock configuration the vendor `add_disk` reads `disk->fops` at offset 400 where the module stored it at 416 and the first load oopses. The `abi_*.sh`, `cmp_*.sh` and `fn.sh` scripts compare function layouts against the vendor kernel. The kernel has no module unload, so a half-loaded module needs a reboot.

## 15. Backlight and audio

* Backlight: `/dev/backlight` takes a raw byte. The curve used by the vendor daemon `cubevol` is `level + 23` below level 43 and `level * level / 43 + 23` above, up to 255. The daemon also stores the raw value in a `persistentmem` slot (30) and applies it at start; a value below 23 is readable on the panel down to about 8.
* Audio volume is the `volume` property of `/hcrtos/i2so` in the device tree and `ioctl 0x8001080b` on the I2S output; `tools/device/audbl.c` exercises both.

