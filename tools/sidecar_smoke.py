#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Smoke test for a sidecar image (esp32c3-qemu, esp32-qemu): boot the
starter firmware (firmware/fw-probe) in the image, with no network, as the
image's own non-root user, and check what the MACHINE saw:

1. serial: the UART prints ``boot <mcu>`` and at least 4 ``led=`` lines;
2. the LED pin, as the machine's GPIO model drove it (``G`` lines), toggles
   every 250 ms of the machine's own clock (+/- 10 %);
3. a button press applied at 1.0 s and released at 1.5 s of the machine's
   clock prints exactly one ``pressed`` and one ``released``, each 0-60 ms
   after the edge was applied.

Usage:
  tools/sidecar_smoke.py --image IMG [--platform linux/arm64] --dir /opt/heypcb-esp32 \
      --exe heypcb-esp32-sidecar --lib libqemu-xtensa.so --flash firmware/esp32-fw-probe.flash.bin.gz \
      --mcu esp32 --led 2 --button 0

Exit 0 when every check passes, 1 when one fails, 2 when the run itself failed.
Only the Python standard library and the docker CLI are needed.
"""
from __future__ import annotations

import argparse
import gzip
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

PRESS_AT_US, RELEASE_AT_US, SPAN_US = 1_000_000, 1_500_000, 3_000_000
SLACK_US = 60_000


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--platform", default=None)
    ap.add_argument("--dir", required=True, help="the image's sidecar directory, e.g. /opt/heypcb-c3")
    ap.add_argument("--exe", required=True)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--flash", required=True, help="a gzipped flash image")
    ap.add_argument("--mcu", required=True)
    ap.add_argument("--led", type=int, required=True)
    ap.add_argument("--button", type=int, required=True)
    ap.add_argument("--uart", type=int, default=0)
    ap.add_argument("--wall-timeout", type=float, default=900.0)
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix="sim-smoke-")
    try:
        os.chmod(work, 0o777)  # the image runs as uid 10001; QEMU writes the flash
        flash = os.path.join(work, "flash.bin")
        with gzip.open(a.flash, "rb") as src, open(flash, "wb") as dst:
            shutil.copyfileobj(src, dst)
        os.chmod(flash, 0o666)
        cmd = ["docker", "run", "--rm", "-i", "--network", "none", "--read-only", "--tmpfs", "/tmp:rw,size=16m",
               "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
               "-v", f"{work}:/work"]
        if a.platform:
            cmd += ["--platform", a.platform]
        cmd += [a.image, f"{a.dir}/{a.lib}", a.dir, "/work/flash.bin"]
        return run(cmd, a)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run(cmd, a) -> int:
    started = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True, bufsize=1)
    lines: "queue.Queue[str | None]" = queue.Queue()

    def pump():
        for line in p.stdout:
            lines.put(line.rstrip("\n"))
        lines.put(None)

    err: list[str] = []
    threading.Thread(target=pump, daemon=True).start()
    threading.Thread(target=lambda: err.extend(p.stderr), daemon=True).start()

    def send(s: str) -> None:
        p.stdin.write(s + "\n")
        p.stdin.flush()

    serial = bytearray()
    serial_lines: list[tuple[int, str]] = []  # (t_us, text)
    pending = ""
    led_edges: list[tuple[int, int]] = []
    applied: dict[str, int] = {}
    ready_t = None
    last_t = 0
    ok_run = False
    try:
        while True:
            if time.monotonic() - started > a.wall_timeout:
                print(f"FAIL run: no end within {a.wall_timeout:.0f} s of wall time", file=sys.stderr)
                break
            try:
                line = lines.get(timeout=5)
            except queue.Empty:
                continue
            if line is None:
                break
            parts = line.split()
            if not parts:
                continue
            kind = parts[0]
            t = int(parts[-1]) if parts[-1].lstrip("-").isdigit() else last_t
            if kind == "R":
                ready_t = t
                # The button idles released: the machine models no pull-up.
                send(f"P {a.button} 1")
            if ready_t is None:
                continue
            last_t = t
            rel = t - ready_t
            if kind == "G" and int(parts[1]) == a.led:
                led_edges.append((rel, int(parts[2])))
            elif kind == "U" and int(parts[1]) == a.uart:
                chunk = bytes.fromhex(parts[2]) if len(parts) > 3 else b""
                serial += chunk
                pending += chunk.decode("latin-1")
                while "\n" in pending:
                    text, pending = pending.split("\n", 1)
                    serial_lines.append((rel, text.strip("\r")))
            elif kind == "I" and len(parts) > 1 and parts[1] != "0":
                send(f"A {parts[1]} {1 if parts[4] in ('0', '1', '2') else 255}")  # nobody on the bus
            if "press" not in applied and rel >= PRESS_AT_US:
                send(f"P {a.button} 0")
                applied["press"] = rel
            elif "press" in applied and "release" not in applied and rel >= RELEASE_AT_US:
                send(f"P {a.button} 1")
                applied["release"] = rel
            if rel >= SPAN_US:
                ok_run = True
                send("Q")
                break
    finally:
        try:
            p.stdin.close()
        except OSError:
            pass
        try:
            p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            p.kill()
    wall = time.monotonic() - started
    if not ok_run:
        print("FAIL run: the machine did not reach 3 s of its own time", file=sys.stderr)
        print("".join(err[-20:]), file=sys.stderr)
        return 2

    texts = [s for _, s in serial_lines]
    checks = {}
    led_lines = [s for s in texts if s.startswith("led=")]
    checks["serial"] = (f"boot {a.mcu}" in texts and len(led_lines) >= 4,
                        f"boot={'boot ' + a.mcu in texts} led_lines={len(led_lines)}")
    edges = [t for t, _ in led_edges]
    gaps = [b - c for c, b in zip(edges, edges[1:])]
    checks["led"] = (len(gaps) >= 4 and all(225_000 <= g <= 275_000 for g in gaps),
                     f"edges={len(edges)} gaps_ms={[round(g / 1000, 1) for g in gaps[:8]]}")
    pressed = [t for t, s in serial_lines if s == "pressed"]
    released = [t for t, s in serial_lines if s == "released"]
    pa, ra = applied.get("press"), applied.get("release")
    good = (len(pressed) == 1 and len(released) == 1 and pa is not None and ra is not None
            and 0 <= pressed[0] - pa <= SLACK_US and 0 <= released[0] - ra <= SLACK_US)
    checks["button"] = (good, f"applied_ms={[None if x is None else round(x / 1000, 1) for x in (pa, ra)]} "
                              f"pressed_ms={[round(x / 1000, 1) for x in pressed]} released_ms={[round(x / 1000, 1) for x in released]}")
    report = {"image": a.image, "platform": a.platform or "native", "wall_s": round(wall, 1),
              "serial_bytes": len(serial), "checks": {k: {"pass": v[0], "seen": v[1]} for k, v in checks.items()},
              "first_lines": texts[:6]}
    print(json.dumps(report, indent=1))
    return 0 if all(v[0] for v in checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
