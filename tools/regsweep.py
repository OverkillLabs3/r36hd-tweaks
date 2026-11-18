#!/usr/bin/env python3
"""Sweep SoC register pages over the USB link, hands-free: every page is read on the console with the hardware watchdog
armed (tools/device/wdtd.c), so a page that hangs the bus makes the console reset itself; the sweep waits for the
auto-started link (cubegm/usblink_auto), starts the watchdog again, marks the page HANG and goes on.

  python -I tools/regsweep.py <first hex> <last hex> <out dir>      pages of 4 KB, last one excluded

Results: <out dir>/<addr>.bin for readable pages, <out dir>/sweep.log with one line per page (ok, HANG, fault).
Needs build/kx/regscan and build/kx/wdtd (tools/device/build_*.sh); read-only on the console.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
R36 = os.path.join(HERE, "r36.py")
ROOT = os.path.dirname(HERE)


def r36(*args, timeout=30):
    try:
        p = subprocess.run([sys.executable, "-I", R36] + list(args), capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return 124, ""


def wait_link(limit=150):
    end = time.time() + limit
    while time.time() < end:
        rc, out = r36("ping", timeout=15)
        if rc == 0 and "PONG" in out:
            return True
        time.sleep(4)
    return False


def prepare():
    r36("put", os.path.join(ROOT, "build", "kx", "regscan"), "/tmp/regscan", timeout=60)
    r36("put", os.path.join(ROOT, "build", "kx", "wdtd"), "/tmp/wdtd", timeout=60)
    r36("sh", "mkdir -p /tmp/regs; /tmp/wdtd start 6000", timeout=30)


def main():
    first, last, out = int(sys.argv[1], 16), int(sys.argv[2], 16), sys.argv[3]
    os.makedirs(out, exist_ok=True)
    logp = os.path.join(out, "sweep.log")
    done = set()
    if os.path.exists(logp):
        done = {l.split()[0] for l in open(logp)}
    if not wait_link():
        raise SystemExit("no link")
    prepare()
    for a in range(first, last, 0x1000):
        key = "%08x" % a
        if key in done:
            continue
        rc, o = r36("sh", "/tmp/regscan /tmp/regs %s; tail -1 /tmp/regs/scan.log" % key, timeout=25)
        if rc == 0 and "ok" in o:
            rc2, _ = r36("get", "/tmp/regs/%s.bin" % key, os.path.join(out, key + ".bin"), timeout=40)
            res = "ok" if rc2 == 0 else "ok-getfail"
        elif rc == 0 and "fault" in o:
            res = "fault"
        else:
            res = "HANG"
        with open(logp, "a") as f:
            f.write("%s %s\n" % (key, res))
        print(key, res, flush=True)
        if res == "HANG":
            time.sleep(3)
            if not wait_link():
                raise SystemExit("link did not come back after %s" % key)
            prepare()
    r36("sh", "/tmp/wdtd stop", timeout=20)


if __name__ == "__main__":
    main()
