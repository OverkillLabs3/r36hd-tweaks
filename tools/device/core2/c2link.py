#!/usr/bin/env python3
"""Turn a statically linked MIPS ELF (linked at address 0 with --emit-relocs, see build_obj.sh) into a relocatable
second-core object (.c2o) that common/c2obj.h can place at any 64 KB aligned address.

  python -I c2link.py in.elf out.c2o [entry_symbol ...]

Why 64 KB alignment: the only relocations of non-PIC, -G0, no-GOT code are R_MIPS_32 (data pointers, jump tables),
R_MIPS_26 (j/jal), R_MIPS_HI16 and R_MIPS_LO16. With the image base a multiple of 0x10000 the low halves never change,
so a loader needs no HI/LO pairing: HI16 += base >> 16, 32 += base, 26 += base >> 2, LO16 untouched.

File format (little endian words): magic 'C2O1', image_size (bytes of code+data), total_size (with bss), n_relocs,
n_entries, then n_entries x (u32 offset), then n_relocs x u32 (offset << 3 | kind; kind 0 = word32, 1 = 26, 2 = hi16),
then the image bytes (image_size, padded to 4).
"""
import struct, sys

SHT_REL, SHT_PROGBITS, SHT_NOBITS, SHT_SYMTAB = 9, 1, 8, 2
SHF_ALLOC = 2
R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_PC16, R_MIPS_NONE = 2, 4, 5, 6, 10, 0


def main():
    elf = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    entries = sys.argv[3:]
    assert elf[:4] == b"\x7fELF" and elf[4] == 1 and elf[5] == 1, "ELF32 little endian expected"
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2e)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info, entsize=entsize))
    strsec = secs[shstrndx]
    def sname(s):
        o = strsec["off"] + s["name"]
        return elf[o:elf.index(b"\0", o)].decode()
    alloc = [s for s in secs if s["flags"] & SHF_ALLOC and s["size"]]
    lo = min(s["addr"] for s in alloc)
    assert lo == 0, "link at address 0"
    prog = [s for s in alloc if s["type"] == SHT_PROGBITS]
    image_end = max(s["addr"] + s["size"] for s in prog)
    total_end = max(s["addr"] + s["size"] for s in alloc)
    image = bytearray(image_end)
    for s in prog:
        image[s["addr"]:s["addr"] + s["size"]] = elf[s["off"]:s["off"] + s["size"]]
    relocs = []
    skipped = {}
    for s in secs:
        if s["type"] != SHT_REL:
            continue
        tgt = secs[s["info"]]
        if not (tgt["flags"] & SHF_ALLOC):
            continue
        for j in range(s["size"] // 8):
            r_off, r_info = struct.unpack_from("<II", elf, s["off"] + j * 8)
            t = r_info & 0xff
            if t == R_MIPS_32: relocs.append((r_off, 0))
            elif t == R_MIPS_26: relocs.append((r_off, 1))
            elif t == R_MIPS_HI16: relocs.append((r_off, 2))
            elif t in (R_MIPS_LO16, R_MIPS_PC16, R_MIPS_NONE): pass
            else: skipped[t] = skipped.get(t, 0) + 1
    if skipped:
        sys.exit("unsupported relocation types %s: build with -mno-abicalls -fno-pic -G0" % skipped)
    # symbol table for entry points
    symtab = next(s for s in secs if s["type"] == SHT_SYMTAB)
    strtab = secs[symtab["link"]]
    syms = {}
    for j in range(symtab["size"] // 16):
        n, val, size, info, other, shndx = struct.unpack_from("<IIIBBH", elf, symtab["off"] + j * 16)
        o = strtab["off"] + n
        syms[elf[o:elf.index(b"\0", o)].decode()] = val
    ents = []
    for e in entries:
        assert e in syms, "entry symbol %s not found" % e
        ents.append(syms[e])
    relocs.sort()
    blob = struct.pack("<4sIIII", b"C2O1", image_end, (total_end + 15) & ~15, len(relocs), len(ents))
    blob += b"".join(struct.pack("<I", e) for e in ents)
    blob += b"".join(struct.pack("<I", (o << 3) | k) for o, k in relocs)
    blob += bytes(image)
    open(out, "wb").write(blob)
    print("%s: image %d bytes, total %d, %d relocations, entries %s" % (out, image_end, total_end, len(relocs), dict(zip(entries, ents))))


main()
