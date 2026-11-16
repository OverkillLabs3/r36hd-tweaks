# Kernel-mode payload (main CPU): install or remove wired large-page TLB entries.
# Parameter block at entry + 0x8200 (written by Linux):
#   [0] op (1 = install, 2 = remove)   [4] n entries   [8] unused   [12] wired base (for remove)
#   [16 + 16*i] EntryHi, EntryLo0, EntryLo1, PageMask of entry i (i < 8)
# Results at entry + 0x8200 + 0xC0: [0] Wired before, [4] Wired after, [8] magic 0x424C5457 ("WTLB").
# Install: interrupts off; every non-wired TLB entry is invalidated first (so no small-page entry can overlap a large
# one), the entries are written at Index = old Wired .. old Wired + n - 1, Wired = old + n. EntryHi (it holds the
# current ASID) and PageMask are saved and put back. Remove: the n entries from the given base are invalidated and
# Wired is set back to the base. Invalidated entries get a unique KSEG0 EntryHi, as Linux does (never matched).
    .set noreorder
    .set noat
    .text
    .globl kx_tlbwire
kx_tlbwire:
    move    $t8, $ra
    bal     1f
    nop
1:  move    $t0, $ra
    move    $ra, $t8
    addiu   $t0, $t0, -12
    ori     $t1, $zero, 0x8200
    addu    $t0, $t0, $t1           # parameter block
    cache   0x11, 0($t0)
    cache   0x11, 32($t0)
    cache   0x11, 64($t0)
    cache   0x11, 96($t0)
    cache   0x11, 128($t0)
    sync
    di      $t9                     # t9 = old Status
    ehb
    mfc0    $a0, $10, 0             # saved EntryHi
    mfc0    $a1, $5, 0              # saved PageMask
    mfc0    $a2, $6, 0              # Wired before
    sw      $a2, 0xC0($t0)
    lw      $t1, 0($t0)             # op
    lw      $t2, 4($t0)             # n
    ori     $t3, $zero, 1
    bne     $t1, $t3, 20f
    nop
    # ---- install: invalidate entries a2 .. 31
    move    $t4, $a2
    ori     $t5, $zero, 32
2:  beq     $t4, $t5, 3f
    nop
    mtc0    $t4, $0, 0              # Index
    sll     $t6, $t4, 13
    lui     $t7, 0x8000
    addu    $t6, $t6, $t7           # unique KSEG0 EntryHi
    mtc0    $t6, $10, 0
    mtc0    $zero, $2, 0
    mtc0    $zero, $3, 0
    mtc0    $zero, $5, 0
    ehb
    tlbwi
    ehb
    addiu   $t4, $t4, 1
    b       2b
    nop
3:  addiu   $t3, $t0, 16            # entry list
    move    $t4, $a2                # index
    move    $t1, $zero              # i
4:  beq     $t1, $t2, 5f
    nop
    mtc0    $t4, $0, 0
    lw      $t6, 12($t3)
    mtc0    $t6, $5, 0              # PageMask of this entry
    lw      $t6, 0($t3)
    mtc0    $t6, $10, 0
    lw      $t6, 4($t3)
    mtc0    $t6, $2, 0
    lw      $t6, 8($t3)
    mtc0    $t6, $3, 0
    ehb
    tlbwi
    ehb
    addiu   $t3, $t3, 16
    addiu   $t4, $t4, 1
    addiu   $t1, $t1, 1
    b       4b
    nop
5:  mtc0    $t4, $6, 0              # Wired = old + n
    b       30f
    nop
    # ---- remove: invalidate base .. base + n - 1, Wired = base
20: ori     $t3, $zero, 2
    bne     $t1, $t3, 30f
    nop
    lw      $t4, 12($t0)            # base
    addu    $t5, $t4, $t2
21: beq     $t4, $t5, 22f
    nop
    mtc0    $t4, $0, 0
    sll     $t6, $t4, 13
    lui     $t7, 0x8000
    addu    $t6, $t6, $t7
    mtc0    $t6, $10, 0
    mtc0    $zero, $2, 0
    mtc0    $zero, $3, 0
    mtc0    $zero, $5, 0
    ehb
    tlbwi
    ehb
    addiu   $t4, $t4, 1
    b       21b
    nop
22: lw      $t4, 12($t0)
    mtc0    $t4, $6, 0              # Wired = base
30: mtc0    $a1, $5, 0              # restore PageMask
    mtc0    $a0, $10, 0             # restore EntryHi (ASID)
    ehb
    mfc0    $t6, $6, 0
    sw      $t6, 0xC4($t0)          # Wired after
    lui     $t6, 0x424C
    ori     $t6, $t6, 0x5457
    sw      $t6, 0xC8($t0)
    cache   0x15, 0xC0($t0)
    sync
    mtc0    $t9, $12, 0             # Status back (interrupts as before)
    ehb
    jr.hb   $ra
    move    $v0, $zero
