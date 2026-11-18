#!/usr/bin/env python3
"""Screenshot of the console's screen over the USB link: python -I tools/fbshot.py <out.png> [fb0|fb1]
Reads the visible page of the frame buffer (fb0: 640x480 RGB565 at the pan offset, fb1: 640x480 32-bit overlay),
copies it with tools/r36.py and writes a PNG (standard library only)."""
import os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
R36 = os.path.join(HERE, "r36.py")


def r36(*a):
    p = subprocess.run([sys.executable, "-I", R36] + list(a), capture_output=True, text=True, timeout=120)
    return p.stdout


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    out = sys.argv[1]
    fb = sys.argv[2] if len(sys.argv) > 2 else "fb0"
    pan = int(r36("sh", "cat /sys/class/graphics/%s/pan" % fb).strip().split(",")[1])
    bpp = int(r36("sh", "cat /sys/class/graphics/%s/bits_per_pixel" % fb).strip())
    stride = int(r36("sh", "cat /sys/class/graphics/%s/stride" % fb).strip())
    w, h = 640, 480
    r36("sh", "dd if=/dev/%s of=/tmp/_fb.raw bs=%d skip=%d count=%d 2>/dev/null" % (fb, stride, pan, h))
    tmp = os.path.join(tempfile.gettempdir(), "_fb.raw")
    r36("get", "/tmp/_fb.raw", tmp)
    r36("sh", "rm -f /tmp/_fb.raw")
    data = open(tmp, "rb").read()
    rgb = bytearray(w * h * 3)
    if bpp == 16:
        for y in range(h):
            row = data[y * stride:(y + 1) * stride]
            for x in range(w):
                v = row[x * 2] | (row[x * 2 + 1] << 8)
                i = (y * w + x) * 3
                rgb[i] = ((v >> 11) & 31) * 255 // 31; rgb[i + 1] = ((v >> 5) & 63) * 255 // 63; rgb[i + 2] = (v & 31) * 255 // 31
    else:
        for y in range(h):
            row = data[y * stride:(y + 1) * stride]
            for x in range(w):
                b, g, r_ = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
                i = (y * w + x) * 3
                rgb[i], rgb[i + 1], rgb[i + 2] = r_, g, b
    png(out, w, h, bytes(rgb))
    print("wrote", out)


if __name__ == "__main__":
    main()
