#!/usr/bin/env python3
"""Rebuild the SD-card kernel image (cubegm/advapi32.dll) with a slimmer built-in initramfs.

  python -I tools/hc_repack_kernel.py <orig advapi32.dll> <out dir> [--drop PREFIX ...] [--keep-size]

Steps (every step is verified; nothing is written to the card):
  1. decrypt (ChaCha20, tools/hc_decrypt.py), strip the uImage header, gunzip -> raw kernel
  2. locate the cpio 'newc' initramfs inside the raw kernel
  3. rebuild the archive without the dropped entries (default: the two game_corrupted BMP error screens), then NUL-pad
     to the ORIGINAL archive length so every offset and linker-baked size in the kernel stays valid
  4. gzip (level 9) the new raw kernel, rebuild the 64-byte uImage header with fresh header and data CRC32
  5. ChaCha20 re-encrypt with the original nonce and write advapi32.dll
  6. verify: decrypt the result again, check header CRC, data CRC, gunzip, byte-compare to the original outside
     the initramfs region, parse the new archive and list what was removed.
"""
import sys, os, struct, zlib, gzip, io, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hc_decrypt as hc

DEFAULT_DROP = ["hcdemo_files/game_corrupted_480_854.bmp", "hcdemo_files/game_corrupted_640_480.bmp"]

def parse_cpio(d):
    pos, ents = 0, []
    while d[pos:pos + 6] == b"070701":
        f = [int(d[pos + 6 + 8 * i: pos + 14 + 8 * i], 16) for i in range(13)]
        fsz, nsz = f[6], f[11]
        name = d[pos + 110: pos + 110 + nsz - 1].decode("latin1")
        doff = (pos + 110 + nsz + 3) & ~3
        nxt = (doff + fsz + 3) & ~3
        ents.append({"name": name, "start": pos, "end": nxt, "raw": d[pos:nxt], "size": fsz})
        if name == "TRAILER!!!":
            return ents, nxt
        pos = nxt
    raise ValueError("no TRAILER!!!")

def find_archive(raw):
    best = None
    for m in re.finditer(b"070701", raw):
        try:
            ents, end = parse_cpio(raw[m.start():])
        except Exception:
            continue
        if len(ents) > 20 and (best is None or len(ents) > len(best[1])):
            best = (m.start(), ents, end)
    if not best:
        raise SystemExit("initramfs not found")
    return best

def uimage_unwrap(img):
    hdr = bytearray(img[:64])
    magic, hcrc, tm, size, load, ep, dcrc, os_, arch, typ, comp = struct.unpack(">IIIIIIIBBBB", bytes(hdr[:32]))
    assert magic == 0x27051956 and comp == 1, "expected gzip uImage"
    return hdr, img[64:64 + size], dcrc, hcrc

def uimage_wrap(hdr, gz):
    h = bytearray(hdr)
    struct.pack_into(">I", h, 12, len(gz))               # size
    struct.pack_into(">I", h, 24, zlib.crc32(gz) & 0xffffffff)   # data crc
    struct.pack_into(">I", h, 4, 0)
    struct.pack_into(">I", h, 4, zlib.crc32(bytes(h)) & 0xffffffff)  # header crc over the header with crc field zero
    return bytes(h) + gz

def main():
    args = sys.argv[1:]
    src, outdir = args[0], args[1]
    drop = []
    if "--drop" in args:
        i = args.index("--drop") + 1
        while i < len(args) and not args[i].startswith("--"):
            drop.append(args[i]); i += 1
    if not drop:
        drop = DEFAULT_DROP
    os.makedirs(outdir, exist_ok=True)
    enc = open(src, "rb").read()
    nonce = enc[:12]
    (plen,) = struct.unpack("<I", enc[12:16])
    assert plen == len(enc) - 16
    dec = hc.crypt(hc.KEY, nonce, enc[16:])
    hdr, gz, dcrc, hcrc = uimage_unwrap(dec)
    # verify the original's own CRCs first: proves our understanding of the header
    h0 = bytearray(hdr); struct.pack_into(">I", h0, 4, 0)
    print("orig data crc ok:", zlib.crc32(gz) & 0xffffffff == dcrc, " orig header crc ok:", zlib.crc32(bytes(h0)) & 0xffffffff == hcrc)
    raw = gzip.decompress(gz)
    start, ents, aend = find_archive(raw)
    print("initramfs at 0x%x length %d, %d entries" % (start, aend, len(ents)))
    keep = [e for e in ents if e["name"] != "TRAILER!!!" and not any(e["name"] == p or e["name"].startswith(p + "/") for p in drop)]
    removed = [e for e in ents if e["name"] != "TRAILER!!!" and e not in keep]
    trailer = [e for e in ents if e["name"] == "TRAILER!!!"][0]
    body = b"".join(e["raw"] for e in keep) + trailer["raw"]
    assert len(body) <= aend, "new archive is not smaller"
    body = body + b"\0" * (aend - len(body))
    newraw = raw[:start] + body + raw[start + aend:]
    assert len(newraw) == len(raw)
    freed = sum(e["size"] for e in removed)
    print("removed %d entries, %d bytes (%.2f MB) of file data:" % (len(removed), freed, freed / 1048576))
    for e in removed:
        print("   -", e["name"], e["size"])
    ngz = gzip.compress(newraw, 9, mtime=0)
    # keep the original gzip header flags if possible: the loader does not care, but report the sizes
    print("gzip: orig %d -> new %d bytes" % (len(gz), len(ngz)))
    new_dec = uimage_wrap(hdr, ngz)
    new_enc = nonce + struct.pack("<I", len(new_dec)) + hc.crypt(hc.KEY, nonce, new_dec)
    out = os.path.join(outdir, "advapi32.dll")
    open(out, "wb").write(new_enc)
    print("wrote", out, len(new_enc), "bytes")

    # ---- verification pass, from the written file only ----
    chk = open(out, "rb").read()
    n2 = chk[:12]; (l2,) = struct.unpack("<I", chk[12:16])
    assert l2 == len(chk) - 16
    d2 = hc.crypt(hc.KEY, n2, chk[16:])
    h2, g2, dc2, hc2 = uimage_unwrap(d2)
    z = bytearray(h2); struct.pack_into(">I", z, 4, 0)
    assert zlib.crc32(g2) & 0xffffffff == dc2, "data crc"
    assert zlib.crc32(bytes(z)) & 0xffffffff == hc2, "header crc"
    r2 = gzip.decompress(g2)
    assert len(r2) == len(raw), "raw size changed"
    assert r2[:start] == raw[:start] and r2[start + aend:] == raw[start + aend:], "bytes outside initramfs changed"
    s2, e2, a2 = find_archive(r2)
    assert s2 == start and len(e2) == len(keep) + 1
    kept = {e["name"]: e["raw"] for e in keep}
    for e in e2:
        if e["name"] != "TRAILER!!!":
            assert kept[e["name"]] == e["raw"], e["name"]
    print("VERIFIED: header crc, data crc, size, identical outside initramfs, %d entries intact (initramfs now %d bytes of live data)" %
          (len(e2) - 1, e2[-1]["end"]))
    print("original header: load %08x entry %08x" % struct.unpack(">II", bytes(hdr[16:24])))

if __name__ == "__main__":
    main()
