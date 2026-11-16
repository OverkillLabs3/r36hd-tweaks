#!/usr/bin/env python3
"""Decrypt (and re-encrypt) the HC16xx SD-card boot images: cubegm/advapi32.dll (Linux kernel uImage),
cubegm/ApplicationFrame.dll (avp.uImage). Derived by reverse engineering the bootloader (stage 4, FUN_89dab188):

  file  = 16-byte header + ciphertext
  header[0:12]  = ChaCha20 nonce
  header[12:16] = payload length (little endian)
  key           = 32-byte constant (obfuscated in the loader by a byte loop; deobfuscated value below)
  cipher        = ChaCha20, 20 rounds, block counter starts at 0 and counts 64-byte blocks

ChaCha20 is symmetric, so the same call encrypts. Usage:
  python -I tools/hc_decrypt.py dec  in.dll out.uImage
  python -I tools/hc_decrypt.py enc  in.uImage out.dll [--nonce HEX24]   (default: keep a given nonce or use 12 zero bytes)
"""
import struct, sys

KEY = bytes.fromhex("5af3abd80cb4fd74b12d83d76370dc35b5d69f67653b93fb985075b3faf60064")
M = 0xFFFFFFFF

def rotl(v, n):
    return ((v << n) & M) | (v >> (32 - n))

def qr(x, a, b, c, d):
    x[a] = (x[a] + x[b]) & M; x[d] ^= x[a]; x[d] = rotl(x[d], 16)
    x[c] = (x[c] + x[d]) & M; x[b] ^= x[c]; x[b] = rotl(x[b], 12)
    x[a] = (x[a] + x[b]) & M; x[d] ^= x[a]; x[d] = rotl(x[d], 8)
    x[c] = (x[c] + x[d]) & M; x[b] ^= x[c]; x[b] = rotl(x[b], 7)

def block(key, counter, nonce):
    st = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574]
    st += list(struct.unpack("<8I", key))
    st += [counter & M] + list(struct.unpack("<3I", nonce))
    x = st[:]
    for _ in range(10):
        qr(x, 0, 4, 8, 12); qr(x, 1, 5, 9, 13); qr(x, 2, 6, 10, 14); qr(x, 3, 7, 11, 15)
        qr(x, 0, 5, 10, 15); qr(x, 1, 6, 11, 12); qr(x, 2, 7, 8, 13); qr(x, 3, 4, 9, 14)
    return struct.pack("<16I", *[(x[i] + st[i]) & M for i in range(16)])

def crypt(key, nonce, data):
    out = bytearray(len(data))
    for i in range(0, len(data), 64):
        ks = block(key, i // 64, nonce)
        chunk = data[i:i + 64]
        for j, c in enumerate(chunk):
            out[i + j] = c ^ ks[j]
    return bytes(out)

def main():
    if len(sys.argv) < 4:
        print(__doc__); return
    mode, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(src, "rb").read()
    if mode == "dec":
        nonce = data[:12]
        (length,) = struct.unpack("<I", data[12:16])
        assert length == len(data) - 16, "header length %d != %d" % (length, len(data) - 16)
        open(dst, "wb").write(crypt(KEY, nonce, data[16:16 + length]))
        print("decrypted %d bytes, nonce %s" % (length, nonce.hex()))
    else:
        nonce = bytes(12)
        if "--nonce" in sys.argv:
            nonce = bytes.fromhex(sys.argv[sys.argv.index("--nonce") + 1])
        hdr = nonce + struct.pack("<I", len(data))
        open(dst, "wb").write(hdr + crypt(KEY, nonce, data))
        print("encrypted %d bytes, nonce %s" % (len(data), nonce.hex()))

if __name__ == "__main__":
    main()
