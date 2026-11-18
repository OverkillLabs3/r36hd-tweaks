#!/usr/bin/env python3
"""Build a DDR speed candidate image from a dump of the console's SPI flash (offline; flashes nothing).

The DDR init of this console is a plain register script in the first 4 KB of the flash (16-byte records at 0x940 to
0xe10: zero word, width, block + offset, value; blocks 0 = 0x18800000, 1 = 0x18801000, 2 = 0x1883e000,
3 = 0x18801800). The faster speeds come from the hcRTOS SDK's DDR3 256M init files: the values below are the writes
that differ between its 1066 and 1333 MHz files (decoded with build/re/abswrites.py).

  python -I tools/ddr_patch.py <flash dump 512 KB> <out file> [1333]

Every old value is checked before it is replaced; any mismatch aborts. Output: the patched image and a listing of
every changed word.
"""
import struct
import sys

OLD_NEW = {   # (block, offset): (old value, new value for 1333 MHz)
    (0, 0x0074): (0x00000010, 0x00000020),   # DDR clock select byte (SDK 1066: 0x10, 1333: 0x20)
    (1, 0x0000): (0x0d894084, 0x6c904044),
    (1, 0x0004): (0x00939100, 0x0093a100),
    (1, 0x0030): (0x00005000, 0x00005800),
    (1, 0x0033): (0x00000060, 0x000000e0),
    (1, 0x008c): (0x00000001, 0x00000012),
    (2, 0x0034): (0x0000001f, 0x0000002a),
    (2, 0x0048): (0x71947744, 0x85589955),
    (2, 0x004c): (0x1562b360, 0x19b35bc0),
    (2, 0x0054): (0x00001930, 0x00001b50),
    (2, 0x005c): (0x00000008, 0x00000010),
}
START, END = 0x940, 0xe10


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = bytearray(open(src, "rb").read())
    if len(d) != 0x80000:
        raise SystemExit("expected a 512 KB flash dump, got %d bytes" % len(d))
    done = set()
    for a in range(START, END, 16):
        z, w, ro, v = struct.unpack_from("<4I", d, a)
        key = (ro >> 16, ro & 0xFFFF)
        if key in OLD_NEW:
            old, new = OLD_NEW[key]
            if v != old:
                raise SystemExit("record at 0x%x (block %d +0x%x) holds 0x%08x, expected 0x%08x: wrong image" % (a, key[0], key[1], v, old))
            struct.pack_into("<I", d, a + 12, new)
            done.add(key)
            print("0x%04x  block %d +0x%04x  0x%08x -> 0x%08x" % (a, key[0], key[1], old, new))
    missing = set(OLD_NEW) - done
    if missing:
        raise SystemExit("records not found: %s" % sorted(missing))
    open(dst, "wb").write(d)
    print("wrote %s (%d bytes), %d words changed, everything else identical" % (dst, len(d), len(done)))


if __name__ == "__main__":
    main()
