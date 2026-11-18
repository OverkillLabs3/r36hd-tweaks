# Kernel-mode payload (main CPU): program the four 74K performance counters.
# Parameter block at entry + 0x8000: PerfCtl0..3 values (words 0..3); the counters are cleared.
# Called as the virtuart driver's poll(file, wait) for one call; returns 0. Position independent.
    .set noreorder
    .set noat
    .text
    .globl kx_pmuset
kx_pmuset:
    move    $t8, $ra
    bal     1f
    nop
1:  move    $t0, $ra              # t0 = entry + 12
    move    $ra, $t8
    addiu   $t0, $t0, -12
    ori     $t1, $zero, 0x8000
    addu    $t0, $t0, $t1         # parameter block
    mtc0    $zero, $25, 0         # stop all counters while they are set up
    mtc0    $zero, $25, 2
    mtc0    $zero, $25, 4
    mtc0    $zero, $25, 6
    ehb
    mtc0    $zero, $25, 1
    mtc0    $zero, $25, 3
    mtc0    $zero, $25, 5
    mtc0    $zero, $25, 7
    lw      $t2, 0($t0)
    mtc0    $t2, $25, 0
    lw      $t2, 4($t0)
    mtc0    $t2, $25, 2
    lw      $t2, 8($t0)
    mtc0    $t2, $25, 4
    lw      $t2, 12($t0)
    mtc0    $t2, $25, 6
    ehb
    mfc0    $t2, $25, 0           # read back the controls (shows which bits the core kept)
    sw      $t2, 16($t0)
    mfc0    $t2, $25, 2
    sw      $t2, 20($t0)
    mfc0    $t2, $25, 4
    sw      $t2, 24($t0)
    mfc0    $t2, $25, 6
    sw      $t2, 28($t0)
    lui     $t2, 0x504D
    ori     $t2, $t2, 0x5553      # magic "PMUS"
    sw      $t2, 60($t0)
    cache   0x15, 0($t0)
    cache   0x15, 32($t0)
    sync
    jr      $ra
    move    $v0, $zero
