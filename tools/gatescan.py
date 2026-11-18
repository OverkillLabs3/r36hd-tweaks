#!/usr/bin/env python3
"""Find which clock-gate bit unlocks which register page, hands-free (needs the USB link, the watchdog tool and the
auto-start flag; see tools/regsweep.py).

For every set bit in the gate registers 0x18800060, 0x18800064 and 0x1880008c (set = gated, see KERNEL_ANALYSIS.md):
  1. clear only that bit in a call of its own; if the console resets, the bit is marked CRASH (clearing it is fatal);
  2. otherwise read the test pages one per call with the bit still cleared; a hang marks the page HANG for that bit,
     a readable page is copied to <out>/<reg>_<bit>_<page>.bin.
Results go to <out>/gatescan.log (one line per step); a rerun skips finished steps.

  python -I tools/gatescan.py <out dir>
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
R36 = os.path.join(HERE, "r36.py")
ROOT = os.path.dirname(HERE)
GATES = {0x60: 0x0e000f8c, 0x64: 0x24c90f40, 0x8c: 0xf800ffc0}   # live values at the menu
PAGES = ["18828000", "18832000"]


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
    r36("sh", "mkdir -p /tmp/regs; sync", timeout=30)


def clear_cmd(reg, bit):
    a = "0x188000%02x" % reg
    return ("/tmp/wdtd start 6000; v=$(devmem %s 32); devmem %s 32 $(printf 0x%%08x $(( v & ~(1 << %d) ))); sleep 1; echo ok" %
            (a, a, bit))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    logp = os.path.join(out, "gatescan.log")
    done = {}
    if os.path.exists(logp):
        for l in open(logp):
            k, _, v = l.rstrip("\n").partition(" ")
            done[k] = v
    log = open(logp, "a")

    def note(k, v):
        log.write("%s %s\n" % (k, v)); log.flush(); print(k, v, flush=True)

    if not wait_link():
        raise SystemExit("no link")
    prepare()
    for reg, val in GATES.items():
        for bit in range(32):
            if not (val >> bit) & 1:
                continue
            key = "%02x.%02d" % (reg, bit)
            if key in done:
                continue
            rc, o = r36("sh", clear_cmd(reg, bit), timeout=25)
            if not (rc == 0 and "ok" in o):
                note(key, "CRASH")
                time.sleep(3)
                if not wait_link():
                    raise SystemExit("link lost")
                prepare()
                continue
            note(key, "survives")
            for page in PAGES:
                pk = "%s.%s" % (key, page)
                rc, o = r36("sh", "/tmp/regscan /tmp/regs %s; tail -1 /tmp/regs/scan.log" % page, timeout=25)
                if rc == 0 and "ok" in o:
                    rc2, _ = r36("get", "/tmp/regs/%s.bin" % page, os.path.join(out, "%s_%s.bin" % (key, page)), timeout=40)
                    note(pk, "readable")
                else:
                    note(pk, "HANG")
                    time.sleep(3)
                    if not wait_link():
                        raise SystemExit("link lost")
                    prepare()
                    r36("sh", clear_cmd(reg, bit), timeout=25)   # clear the bit again for the next page
            # restore: reset the console so the next bit starts from stock gates
            r36("sh", "sync; /tmp/wdtd arm 1500", timeout=20)
            time.sleep(6)
            if not wait_link():
                raise SystemExit("link lost after restore")
            prepare()
    r36("sh", "/tmp/wdtd stop", timeout=20)


if __name__ == "__main__":
    main()
