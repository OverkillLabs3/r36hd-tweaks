#!/usr/bin/env python3
"""Talk to the console over the USB cable (R36 USB Link: r36agent on the console's USB serial function).

  python -I tools/r36.py ping
  python -I tools/r36.py sh "<command>"            run a shell command on the console, print its output, exit with its status
  python -I tools/r36.py put <local> <remote>     copy a file to the console (mode kept for executables)
  python -I tools/r36.py get <remote> <local>     copy a file from the console
  python -I tools/r36.py push <local dir> <remote dir>   copy every file of a folder (recursive)
  python -I tools/r36.py run "/mnt/sdcard/roms/ports/<launcher>.sh"   start a launcher as if picked in the menu
  python -I tools/r36.py stop                       end the running game (back to the menu)
From Git Bash set MSYS_NO_PATHCONV=1 so console paths are not turned into Windows paths.

Port: R36_COM (default: the first COM port of the "R36HD dev link" USB device, else COM3). The console kernel has no
IP networking, so the link is the USB serial function, not the network adapter.
"""
import os
import stat
import sys
import time

if sys.platform == "win32":
    import ctypes
    import msvcrt
    from ctypes import wintypes


def find_com():
    if os.environ.get("R36_COM"):
        return os.environ["R36_COM"]
    # the serial function is interface 0 in the serial-only link and interface 2 with the network adapter
    import winreg
    present = set()
    try:
        m = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
        i = 0
        while True:
            present.add(winreg.EnumValue(m, i)[1])
            i += 1
    except OSError:
        pass
    for mi in ("MI_00", "MI_02"):
        try:
            k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Enum\USB\VID_1D6B&PID_0104&" + mi)
        except OSError:
            continue
        i = 0
        while True:
            try:
                inst = winreg.EnumKey(k, i)
            except OSError:
                break
            i += 1
            try:
                port = winreg.QueryValueEx(winreg.OpenKey(k, inst + r"\Device Parameters"), "PortName")[0]
            except OSError:
                continue
            if port in present:
                return port
    return "COM3"


class Serial:
    def __init__(self, name):
        self.f = open(r"\\.\%s" % name, "r+b", buffering=0)
        h = msvcrt.get_osfhandle(self.f.fileno())

        class COMMTIMEOUTS(ctypes.Structure):
            _fields_ = [("ReadIntervalTimeout", wintypes.DWORD), ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
                        ("ReadTotalTimeoutConstant", wintypes.DWORD), ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
                        ("WriteTotalTimeoutConstant", wintypes.DWORD)]
        # return what is there, or wait up to 200 ms for the first byte
        t = COMMTIMEOUTS(0xFFFFFFFF, 0xFFFFFFFF, 200, 0, 20000)
        if not ctypes.windll.kernel32.SetCommTimeouts(wintypes.HANDLE(h), ctypes.byref(t)):
            raise SystemExit("SetCommTimeouts failed on %s" % name)
        self.buf = bytearray()

    def sendall(self, b):
        mv = memoryview(b)
        while len(mv):
            n = self.f.write(mv[:16384])
            mv = mv[n:]

    def recv(self, n, timeout=30.0):
        if self.buf:
            out = bytes(self.buf[:n]); del self.buf[:n]; return out
        end = time.time() + timeout
        while time.time() < end:
            b = self.f.read(min(n, 65536))
            if b:
                return b
        return b""

    def drain(self):
        while self.f.read(65536):
            pass


_conn = None


def conn():
    global _conn
    if _conn is None:
        _conn = Serial(find_com())
        _conn.sendall(b"\n\n")
        time.sleep(0.2)
        _conn.drain()
    return _conn


def read_line(s):
    b = bytearray()
    while True:
        c = s.recv(1)
        if not c:
            raise SystemExit("no answer from the console")
        if c == b"\n":
            break
        b += c
    return b.decode("utf-8", "replace").rstrip("\r")


def ping():
    s = conn()
    s.sendall(b"PING\n")
    print(read_line(s))


def sh(cmd, echo=True):
    s = conn()
    s.sendall(b"EXEC\n" + cmd.encode() + b"\n")
    data = bytearray()
    marker = b"\n#R36EXIT "
    while True:
        b = s.recv(65536, timeout=600)
        if not b:
            raise SystemExit("no answer from the console")
        data += b
        i = data.rfind(marker)
        if i >= 0 and data.endswith(b"\n") and data.find(b"\n", i + 1) == len(data) - 1:
            break
    i = data.rfind(marker)
    status = int(data[i + len(marker):].split()[0])
    text = data[:i].decode("utf-8", "replace")
    if echo:
        sys.stdout.write(text)
        if text and not text.endswith("\n"):
            sys.stdout.write("\n")
    return status


def put(local, remote):
    st = os.stat(local)
    with open(local, "rb") as f:
        elf = f.read(4) == b"\x7fELF"
    mode = 0o755 if (st.st_mode & stat.S_IXUSR or elf or local.endswith((".so", ".sh"))) else 0o644
    s = conn()
    s.sendall(("PUT %d %o\n%s\n" % (st.st_size, mode, remote)).encode())
    t0 = time.time()
    with open(local, "rb") as f:
        while True:
            b = f.read(1 << 16)
            if not b:
                break
            s.sendall(b)
    r = read_line(s)
    if r != "OK":
        raise SystemExit("put %s: %s" % (remote, r))
    dt = max(time.time() - t0, 1e-3)
    print("%s -> %s (%d bytes, %.0f KB/s)" % (local, remote, st.st_size, st.st_size / 1024 / dt))


def _get1(remote, local):
    s = conn()
    s.sendall(("GET\n%s\n" % remote).encode())
    h = read_line(s)
    if not h.startswith("SIZE "):
        raise SystemExit("get %s: %s" % (remote, h))
    n = int(h[5:])
    got = 0
    with open(local, "wb") as f:
        while got < n:
            b = s.recv(min(1 << 16, n - got))
            if not b:
                break
            f.write(b)
            got += len(b)
    if got != n:
        raise SystemExit("get %s: short read %d of %d" % (remote, got, n))
    s.recv(1, timeout=3)   # the agent's trailing newline (see r36agent.c)
    return n


def get(remote, local):
    """Files over 1 MB come in 256 KB slices (one large transfer over the USB serial link loses its last bytes)."""
    import hashlib
    import tempfile
    out = io_sh("wc -c < '%s'" % remote).strip()
    size = int(out) if out.isdigit() else 0
    if size <= 1 << 20:
        n = _get1(remote, local)
        print("%s -> %s (%d bytes)" % (remote, local, n))
        return
    sl = 262144
    h = hashlib.md5()
    with open(local, "wb") as f:
        for i in range((size + sl - 1) // sl):
            sh("dd if='%s' of=/tmp/_r36slice bs=%d skip=%d count=1 2>/dev/null" % (remote, sl, i), echo=False)
            tmp = os.path.join(tempfile.gettempdir(), "_r36slice")
            _get1("/tmp/_r36slice", tmp)
            with open(tmp, "rb") as t:
                b = t.read()
            f.write(b)
            h.update(b)
    sh("rm -f /tmp/_r36slice", echo=False)
    remote_md5 = io_sh("md5sum '%s'" % remote).split()[0]
    if remote_md5 != h.hexdigest():
        raise SystemExit("get %s: checksum mismatch (%s on the console, %s here)" % (remote, remote_md5, h.hexdigest()))
    print("%s -> %s (%d bytes, md5 ok)" % (remote, local, size))


def io_sh(cmd):
    """Run a console command and return its output as text (sh() prints; this returns)."""
    s = conn()
    s.sendall(b"EXEC\n" + cmd.encode() + b"\n")
    data = bytearray()
    marker = b"\n#R36EXIT "
    while True:
        b = s.recv(65536, timeout=600)
        if not b:
            raise SystemExit("no answer from the console")
        data += b
        i = data.rfind(marker)
        if i >= 0 and data.endswith(b"\n") and data.find(b"\n", i + 1) == len(data) - 1:
            return data[:i].decode("utf-8", "replace")


def push(local_dir, remote_dir):
    for root, _dirs, files in os.walk(local_dir):
        rel = os.path.relpath(root, local_dir).replace("\\", "/")
        rdir = remote_dir if rel == "." else remote_dir + "/" + rel
        sh("mkdir -p '%s'" % rdir, echo=False)
        for f in files:
            put(os.path.join(root, f), rdir + "/" + f)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    c, a = sys.argv[1], sys.argv[2:]
    if c == "ping":
        ping()
    elif c == "sh":
        raise SystemExit(sh(" ".join(a)))
    elif c == "put":
        put(a[0], a[1])
    elif c == "get":
        get(a[0], a[1])
    elif c == "push":
        push(a[0], a[1])
    elif c == "run":
        # start a launcher the way the menu does: the console's launcher loop stops the menu (or the running game)
        raise SystemExit(sh("printf 'standalone\\n%%s\\n' '%s' > /mnt/sdcard/cubegm/frogui_launch.txt" % a[0]))
    elif c == "stop":
        # end the running game; the launcher loop brings the menu back
        raise SystemExit(sh("killall picoarch; sleep 1; killall -9 picoarch 2>/dev/null; true"))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
