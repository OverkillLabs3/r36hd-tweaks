# R36HD device audit

Every subsystem that was checked for speed, with its status. **DONE**: implemented in this repository. **CLOSED**: checked, nothing to gain or not safely reachable. **OPEN**: analysed, not finished. Details are in `KERNEL_ANALYSIS.md`.

## Main CPU (MIPS 74Kc)
| Item | Status | Finding |
|---|---|---|
| Clock | CLOSED | A run-time PLL change works, but 1320 and 1400 MHz were unstable (crashes, display artifacts) with no voltage control. Stock 1188 MHz is the safe setting. |
| Core voltage | CLOSED | No regulator, PMIC or DVFS in the device tree or kernel. |
| FPU clock ratio | CLOSED | Config7.FPR is read-only and reads 01, half the core clock. |
| Config7 controls | CLOSED | Prefetch of 2 lines gave no clear gain; the other bits are already in the best state. |
| TLB | DONE | Wired large pages over the memory pool (`common/bigtlb.c`, `tools/device/kx/`). A 24 MB pointer chase 399 ns to 219 ns. |
| Kernel-mode code | DONE | `tools/device/kx/`: a payload runs once through the `virtuart` driver's poll pointer and is restored at once. |
| Idle | CLOSED | `cpu_wait` is set, the idle loop uses `wait`. |
| Interrupt load | CLOSED | Timer about 2540/s, about 1 percent CPU. |

## Second CPU core (24KE family, 800 MHz, no FPU)
| Item | Status | Finding |
|---|---|---|
| Running code | DONE | `common/core2.{h,c}` and the worker in `tools/device/core2/`: 1.03 ms job round trip, 228 MB/s fill, both cores together 1.76x on integer work. |
| Relocatable objects | DONE | `build_obj.sh` and `c2link.py` produce `.c2o` files that the loader relocates. |
| Clock | CLOSED | A mux change at run time stops the display. |

## Memory (128 MB DDR3)
| Item | Status | Finding |
|---|---|---|
| Size | CLOSED | The controller says 256 MB, but data mirrors at +128 MB. 128 MB. |
| DDR speed | OPEN | 1066 MT/s. Set by a register script in stage 2 of the SPI flash; the chip is rated 1600. Needs a flash rewrite, see section 13 of the kernel notes. |
| Device-tree layout | CLOSED | Linux ends where the panel frame buffer starts. Moving it only shifts memory between Linux and the pool. |
| Video pool (mmz0) | DONE | Up to 48 MB per program through `/dev/mmz` as the heap (`common/mmz_heap.c`), zero-filled. |
| memset / memcpy | DONE | `common/fastmem.c`: `pref 30` on whole lines, 3 to 4x faster memset. |
| zram | DONE | Builds and loads as a module with the settings in `tools/kmod/build_kmods.sh`. No gain measured in a game. |
| zswap, huge pages | CLOSED | Not in the kernel. |
| Swap | DONE | With the pool swap is rarely touched; set swappiness to 0 during games. |

## Storage
| Item | Status | Finding |
|---|---|---|
| SD clock | DONE | `tools/device/sdfast.c` sets 50 MHz at run time with a write check, 7.96 to 16.69 MB/s. |
| UHS modes | CLOSED | 3.3 V signalling only. High speed (50 MHz) is the ceiling. |
| Read-ahead | DONE | 128 KB to 512 KB (`tools/device/lowram_body.sh`). |
| Scheduler | CLOSED | `noop` already. |

## Display and sound
| Item | Status | Finding |
|---|---|---|
| Frame path | DONE | `common/direct_present.h` hands frames to the display driver and skips the frontend's software scaler. |
| 2D engine (GE) | CLOSED | `/dev/ge` does 2D blits only. |
| Sound | CLOSED | Mixed on the second core's firmware through RPC. No main-core cost found. |

## Software and tools
| Item | Status | Finding |
|---|---|---|
| Background processes | CLOSED | `hcdaemon`, `cubevol` and the stopped UI use about 0 percent CPU. |
| Compiler flags | DONE | `-fvisibility=hidden -fno-semantic-interposition` removes about 5 percent of instructions in plugin-style cores. |
| Profiler | DONE | `common/r36prof.c`: SIGPROF sampling at 250 Hz, records the PC. |

## Debug link and flash
| Item | Status | Finding |
|---|---|---|
| USB serial link | DONE | `tools/r36.py` and `tools/device/r36agent.c`: commands, file transfer and screenshots without a network. |
| Register sweeps | DONE | Some disabled blocks hang the bus; `tools/device/wdtd.c` resets the console. See section 12 of the kernel notes. |
| SPI flash | OPEN | Dumped and analysed, never written. DDR speed (1066, chip rated 1600) is set by a register script in the flash. A 1333 candidate image is built offline and checked against the SDK, see section 13. |
