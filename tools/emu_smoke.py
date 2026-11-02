#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Smoke test for an emulator image that is not a sidecar (esp8266-emu): boot
the starter firmware (firmware/fw-probe) in the image, with no network, as the
image's own non-root user, a read-only root and every capability dropped,
with the press scripted on the machine's own clock, and check what the MACHINE
saw (the same three checks as tools/sidecar_smoke.py):

1. serial: the UART prints ``boot <mcu>`` and at least 4 ``led=`` lines;
2. the LED pin, as the machine's GPIO model drove it (``edges`` events),
   toggles every 250 ms of the machine's own clock (+/- 10 %);
3. a button press applied at 1.0 s and released at 1.5 s of the machine's
   clock prints exactly one ``pressed`` and one ``released``, each 0-60 ms
   after the edge was applied, and ``btn=1`` only inside that window;

and 4. the same run twice gives the same edges and the same serial lines at
the same times (the emulator counts one instruction a cycle).

Usage:
  tools/emu_smoke.py --image IMG [--platform linux/arm64] --dir /opt/heypcb-esp8266 \
      --exe esp8266emu --flash firmware/esp8266-fw-probe.flash.bin.gz --mcu esp8266 --led 2 --button 0

Exit 0 when every check passes, 1 when one fails, 2 when the run itself failed.
Only the Python standard library and the docker CLI are needed.
"""
from __future__ import annotations

import argparse
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

PRESS_AT_MS, RELEASE_AT_MS, SPAN_S = 1000, 1500, 3
SLACK_US = 60_000


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--platform", default=None)
    ap.add_argument("--dir", required=True, help="the image's directory, e.g. /opt/heypcb-esp8266")
    ap.add_argument("--exe", required=True)
    ap.add_argument("--flash", required=True, help="a gzipped flash image")
    ap.add_argument("--mcu", required=True)
    ap.add_argument("--led", type=int, required=True)
    ap.add_argument("--button", type=int, required=True)
    ap.add_argument("--wall-timeout", type=float, default=900.0)
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix="emu-smoke-")
    try:
        os.chmod(work, 0o755)
        flash = os.path.join(work, "flash.bin")
        with gzip.open(a.flash, "rb") as src, open(flash, "wb") as dst:
            shutil.copyfileobj(src, dst)
        os.chmod(flash, 0o644)
        with open(os.path.join(work, "press.txt"), "w") as f:
            f.write(f"{PRESS_AT_MS} in {a.button} 0\n{RELEASE_AT_MS} in {a.button} -1\n")
        os.chmod(os.path.join(work, "press.txt"), 0o644)
        cmd = ["docker", "run", "--rm", "--network", "none", "--read-only", "--tmpfs", "/tmp:rw,size=16m",
               "--cap-drop", "ALL", "--security-opt", "no-new-privileges", "-v", f"{work}:/work:ro"]
        if a.platform:
            cmd += ["--platform", a.platform]
        cmd += ["--entrypoint", f"{a.dir}/{a.exe}", a.image, "--flash", "/work/flash.bin", "--speed", "0",
                "--max-seconds", str(SPAN_S), "--no-stdin", "--script", "/work/press.txt"]
        runs = []
        for _ in range(2):
            t0 = time.monotonic()
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=a.wall_timeout)
            if p.returncode != 0:
                print(f"run failed (exit {p.returncode}): {p.stderr.strip()[-2000:]}")
                return 2
            runs.append((view(p.stdout), time.monotonic() - t0))
        return check(runs, a)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def view(stdout: str):
    """Every pin edge, and every UART0 line with the emulated time of its first byte."""
    edges, lines, buf, t0, halt = [], [], "", None, None
    for raw in stdout.splitlines():
        try:
            e = json.loads(raw)
        except ValueError:
            continue
        if e.get("t") == "edges":
            edges += [tuple(x) for x in e["e"]]
        elif e.get("t") == "uart" and e.get("u") == 0:
            if not buf:
                t0 = e.get("vt_us")
            buf += bytes.fromhex(e["d"]).decode("latin-1")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                lines.append((t0, line.rstrip("\r")))
                t0 = e.get("vt_us")
        elif e.get("t") == "halt":
            halt = e.get("m")
    return edges, lines, halt


def check(runs, a) -> int:
    (edges, lines, halt), wall = runs[0]
    ok = True
    if halt:
        print(f"FAIL: the machine halted: {halt}")
        return 1
    if any(t is None for t, _ in lines):
        print("FAIL: a serial line carries no machine time (uart events need vt_us: uart-times.patch)")
        return 1
    boot = [t for t, l in lines if l == f"boot {a.mcu}"]
    leds = [t for t, l in lines if l.startswith("led=")]
    c1 = bool(boot) and len(leds) >= 4
    ok &= c1
    print(f"{'pass' if c1 else 'FAIL'} 1 serial: boot at {boot[0] / 1000 if boot else None} ms, {len(leds)} led= lines")

    led = [t for t, pin, lv, md in edges if pin == a.led and md == "o"]
    gaps = [(b - c) / 1000 for c, b in zip(led[1:], led[2:])]  # the first output edge is the pin's setup
    c2 = len(gaps) >= 4 and all(225 <= g <= 275 for g in gaps)
    ok &= c2
    print(f"{'pass' if c2 else 'FAIL'} 2 LED GPIO{a.led}: {len(gaps)} toggles, {min(gaps, default=0):.3f}-{max(gaps, default=0):.3f} ms apart")

    btn = [(t, lv) for t, pin, lv, md in edges if pin == a.button and t > 0]
    press = next((t for t, lv in btn if lv == 0), None)
    release = next((t for t, lv in btn if lv == 1 and press is not None and t > press), None)
    pressed = [t for t, l in lines if l == "pressed"]
    released = [t for t, l in lines if l == "released"]
    ones = [t for t, l in lines if " btn=1 " in f" {l} "]
    c3 = (press is not None and release is not None and len(pressed) == 1 and len(released) == 1
          and 0 <= pressed[0] - press <= SLACK_US and 0 <= released[0] - release <= SLACK_US
          and ones and all(press <= t <= release + SLACK_US for t in ones))
    ok &= bool(c3)
    lag = (lambda xs, at: f"+{(xs[0] - at) / 1000:.3f} ms" if len(xs) == 1 and at is not None else f"{len(xs)} lines")
    print(f"{'pass' if c3 else 'FAIL'} 3 button GPIO{a.button}: applied {press and press / 1000}-{release and release / 1000} ms, "
          f"pressed {lag(pressed, press)}, released {lag(released, release)}, btn=1 lines {len(ones)}")

    same = runs[0][0] == runs[1][0]
    ok &= same
    print(f"{'pass' if same else 'FAIL'} 4 twice: {'the same edges and serial lines' if same else 'the runs differ'}"
          f" ({len(edges)} edges, {len(lines)} lines); wall {runs[0][1]:.1f} s and {runs[1][1]:.1f} s for {SPAN_S} s")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
