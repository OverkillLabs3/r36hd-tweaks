#!/usr/bin/env python3
"""Extract a cpio 'newc' (070701) archive without any external tool, preserving file modes on a best-effort basis.
  python -I tools/hc_cpio_extract.py archive.cpio outdir
Symlinks are written as text files '<name>.symlink' (Windows-safe). Device nodes are skipped.
"""
import sys, os

def main():
    d = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    pos = 0
    n = 0
    while d[pos:pos + 6] == b"070701":
        f = [int(d[pos + 6 + 8 * i: pos + 14 + 8 * i], 16) for i in range(13)]
        mode, filesize, namesize = f[1], f[6], f[11]
        name = d[pos + 110: pos + 110 + namesize - 1].decode("latin1")
        data_off = (pos + 110 + namesize + 3) & ~3
        nxt = (data_off + filesize + 3) & ~3
        if name == "TRAILER!!!":
            break
        kind = mode & 0o170000
        path = os.path.join(out, name.lstrip("./"))
        if kind == 0o040000:
            os.makedirs(path, exist_ok=True)
        elif kind == 0o120000:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path + ".symlink", "wb").write(d[data_off:data_off + filesize])
        elif kind == 0o100000:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(d[data_off:data_off + filesize])
        n += 1
        pos = nxt
    print("extracted", n, "entries to", out)

main()
