# Kernel-mode payload (main CPU): read the four 74K performance counters and the cp0 Count register.
# Results at entry + 0x8000: PerfCnt0..3 (words 0..3), Count (word 4), PerfCtl0..3 (words 5..8), magic "PMUR" (word 15).
# The counters keep running. Called as the virtuart driver's poll(file, wait) for one call; returns 0.
    .set noreorder
    .set noat
    .text
    .globl kx_pmuread
kx_pmuread:
    move    $t8, $ra
    bal     1f
    nop
1:  move    $t0, $ra
    move    $ra, $t8
    addiu   $t0, $t0, -12
    ori     $t1, $zero, 0x8000
    addu    $t0, $t0, $t1
    mfc0    $t2, $25, 1
    sw      $t2, 0($t0)
    mfc0    $t2, $25, 3
    sw      $t2, 4($t0)
    mfc0    $t2, $25, 5
    sw      $t2, 8($t0)
    mfc0    $t2, $25, 7
    sw      $t2, 12($t0)
    mfc0    $t2, $9, 0
    sw      $t2, 16($t0)
    mfc0    $t2, $25, 0
    sw      $t2, 20($t0)
    mfc0    $t2, $25, 2
    sw      $t2, 24($t0)
    mfc0    $t2, $25, 4
    sw      $t2, 28($t0)
    mfc0    $t2, $25, 6
    sw      $t2, 32($t0)
    lui     $t2, 0x504D
    ori     $t2, $t2, 0x5552      # magic "PMUR"
    sw      $t2, 60($t0)
    cache   0x15, 0($t0)
    cache   0x15, 32($t0)
    sync
    jr      $ra
    move    $v0, $zero
