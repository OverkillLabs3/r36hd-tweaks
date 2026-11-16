# Kernel-mode payload (main CPU): copy CP0 identification and configuration registers to entry + 0x8000.
# Called as the virtuart driver's poll(file, wait) for one call; returns 0 (no events). Position independent.
    .set noreorder
    .set noat
    .text
    .globl kx_cp0read
kx_cp0read:
    move    $t8, $ra
    bal     1f
    nop
1:  move    $t0, $ra              # t0 = entry + 12
    move    $ra, $t8
    addiu   $t0, $t0, -12
    ori     $t1, $zero, 0x8000
    addu    $t0, $t0, $t1         # result area
    mfc0    $t2, $15, 0
    sw      $t2, 0($t0)           # PRId
    mfc0    $t2, $15, 1
    sw      $t2, 4($t0)           # EBase
    mfc0    $t2, $16, 0
    sw      $t2, 8($t0)           # Config
    mfc0    $t2, $16, 1
    sw      $t2, 12($t0)          # Config1
    mfc0    $t2, $16, 2
    sw      $t2, 16($t0)          # Config2
    mfc0    $t2, $16, 3
    sw      $t2, 20($t0)          # Config3
    mfc0    $t2, $16, 6
    sw      $t2, 24($t0)          # Config6
    mfc0    $t2, $16, 7
    sw      $t2, 28($t0)          # Config7
    mfc0    $t2, $12, 0
    sw      $t2, 32($t0)          # Status
    mfc0    $t2, $6, 0
    sw      $t2, 36($t0)          # Wired
    mfc0    $t2, $5, 0
    sw      $t2, 40($t0)          # PageMask
    mfc0    $t2, $16, 4
    sw      $t2, 44($t0)          # Config4
    mfc0    $t2, $16, 5
    sw      $t2, 48($t0)          # Config5
    mfc0    $t2, $26, 0
    sw      $t2, 52($t0)          # ErrCtl
    mfc0    $t2, $12, 1
    sw      $t2, 56($t0)          # IntCtl
    lui     $t2, 0x4B58
    ori     $t2, $t2, 0x4F4B
    sw      $t2, 60($t0)          # magic "KOXK"
    cache   0x15, 0($t0)          # write the two result lines back to RAM
    cache   0x15, 32($t0)
    sync
    jr      $ra
    move    $v0, $zero
