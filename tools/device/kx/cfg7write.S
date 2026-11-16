# Kernel-mode payload (main CPU): Config7 = (Config7 & ~mask) | (value & mask), then read back.
# Inputs at entry + 0x8100: [0] value, [4] mask. Outputs at entry + 0x8100: [8] old Config7, [12] new Config7,
# [16] magic 0x37474643 ("CFG7"). Called as the virtuart driver's poll() for one call; returns 0. Position independent.
    .set noreorder
    .set noat
    .text
    .globl kx_cfg7write
kx_cfg7write:
    move    $t8, $ra
    bal     1f
    nop
1:  move    $t0, $ra
    move    $ra, $t8
    addiu   $t0, $t0, -12
    ori     $t1, $zero, 0x8100
    addu    $t0, $t0, $t1          # parameter block
    cache   0x11, 0($t0)           # drop any stale copy of the parameters (written by Linux, flushed to RAM)
    sync
    lw      $t2, 0($t0)            # value
    lw      $t3, 4($t0)            # mask
    mfc0    $t4, $16, 7
    sw      $t4, 8($t0)
    nor     $t5, $t3, $zero        # ~mask
    and     $t5, $t4, $t5
    and     $t6, $t2, $t3
    or      $t5, $t5, $t6
    mtc0    $t5, $16, 7
    ehb
    mfc0    $t4, $16, 7
    sw      $t4, 12($t0)
    lui     $t2, 0x3747
    ori     $t2, $t2, 0x4643
    sw      $t2, 16($t0)
    cache   0x15, 0($t0)
    sync
    jr.hb   $ra
    move    $v0, $zero
