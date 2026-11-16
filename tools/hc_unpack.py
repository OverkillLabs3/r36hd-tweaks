#!/usr/bin/env python3
"""Unwrap a decrypted uImage (from tools/hc_decrypt.py): print the header, strip it, decompress the payload.
  python -I tools/hc_unpack.py in_uimage out_raw
Compression type byte (header offset 31): 0 none, 1 gzip, 2 bzip2, 3 lzma, 4 lzo, 5 lz4 ...
"""
import struct, sys, zlib, lzma, bz2

def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, "rb").read()
    magic, hcrc, tm, size, load, ep, dcrc, os_, arch, typ, comp = struct.unpack(">IIIIIIIBBBB", d[:32])
    name = d[32:64].split(b"\0")[0].decode("latin1")
    print("magic %08x size %d load %08x entry %08x os %d arch %d type %d comp %d name %r" %
          (magic, size, load, ep, os_, arch, typ, comp, name))
    payload = d[64:64 + size]
    if comp == 0:
        raw = payload
    elif comp == 1:
        raw = zlib.decompressobj(31).decompress(payload)
    elif comp == 2:
        raw = bz2.decompress(payload)
    elif comp == 3:
        # U-Boot lzma = LZMA alone: 5 prop bytes + 8 size bytes (size may be -1) + stream
        try:
            raw = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(payload)
        except Exception as e:
            print("lzma alone failed:", e)
            raise
    else:
        raise SystemExit("unsupported compression %d" % comp)
    open(dst, "wb").write(raw)
    print("wrote %d bytes -> %s" % (len(raw), dst))

main()
