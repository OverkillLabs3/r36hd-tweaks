#!/usr/bin/env python3
"""Locate and size the built-in cpio initramfs inside an unpacked kernel (build/kernel/vmlinux.raw).
  python -I tools/hc_initramfs.py build/kernel/vmlinux.raw [outdir]
Reports: offset, extent up to TRAILER!!!, file count, total file bytes, biggest files. If outdir is given,
writes the raw cpio to outdir/initramfs.cpio and extracts it with `cpio` in WSL afterwards (done by the caller).
"""
import sys, re, os

def parse(cpio, base):
    pos = 0
    files = []
    while True:
        if cpio[pos:pos + 6] != b"070701":
            break
        try:
            f = [int(cpio[pos + 6 + 8 * i: pos + 14 + 8 * i], 16) for i in range(13)]
        except ValueError:
            break
        mode, filesize, namesize = f[1], f[6], f[11]
        name = cpio[pos + 110: pos + 110 + namesize - 1].decode("latin1")
        data_off = (pos + 110 + namesize + 3) & ~3
        nxt = (data_off + filesize + 3) & ~3
        if name == "TRAILER!!!":
            return files, nxt
        files.append((name, filesize, mode, base + data_off))
        pos = nxt
    return files, pos

def main():
    d = open(sys.argv[1], "rb").read()
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    starts = [m.start() for m in re.finditer(b"070701", d)]
    print("070701 candidates:", len(starts))
    # the archive start is the first candidate whose parse yields a sane run
    best = None
    for s in starts:
        files, end = parse(d[s:], 0)
        if len(files) > 20 and (best is None or len(files) > len(best[1])):
            best = (s, files, end)
    if not best:
        print("no cpio archive found"); return
    s, files, end = best
    total = sum(f[1] for f in files)
    print("initramfs at 0x%x, archive %d bytes (%.2f MB), %d entries, file data %d bytes (%.2f MB)" %
          (s, end, end / 1048576, len(files), total, total / 1048576))
    for name, size, mode, off in sorted(files, key=lambda x: -x[1])[:30]:
        print("  %9d  %s" % (size, name))
    if outdir:
        os.makedirs(outdir, exist_ok=True)
        open(os.path.join(outdir, "initramfs.cpio"), "wb").write(d[s:s + end])
        print("wrote", os.path.join(outdir, "initramfs.cpio"))

main()
