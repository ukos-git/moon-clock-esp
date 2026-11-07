#!/usr/bin/env python3
"""perf_measure.py — run EVERY performance measurement and write the data
file the plots are drawn from: ``docs/tools/perf_data.json``.

This is the measurement half of the performance page (the plotting half is
``gen_perf_figs.py``). Nothing here touches the device: the host engines are
measured live, the Python and JS comparators are measured live, and the
device points are the *accepted-capture* numbers (the same quoted values
:doc:`on-device verification <../verification/device-verify>` certifies —
the page quotes them, it does not re-measure them).

Engines measured live, per disk size (median of ``--iters`` runs, one fixed
epoch, so every number is comparable across engines):

* **C host** — ``host_sim/build/moon_bench`` (the S3-asset build) and
  ``host_sim/build-p4/moon_bench`` (the P4-asset build), each at the three
  Kconfig-sized settings the A/B question asks: default (relief on, gate
  path), ``--no-relief``, ``--darkzero --neutral`` (the display path).
* **Python (numpy)** — the validated prototype's ``moon.animation.render_frame``
  (the path ``check_gate.py`` validates against).
* **JS (V8)** — the interactive sim's ``moonlab.js`` bake under Node
  (``perf_bench_moonlab.js``), the device display path.

Run:  docs/.venv/bin/python docs/tools/perf_measure.py [--iters 5]
      (builds moon_bench for both targets if missing; cmake configures itself)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DOCS = HERE.parent
ROOT = DOCS.parent

UTC = "2026-04-15T12:00"
SIZES = [200, 240, 320, 400, 466, 618, 650]
MOON_VENV = ROOT / "assets" / "images" / "moon" / ".venv" / "bin" / "python"
DOCS_VENV_PY = DOCS / ".venv" / "bin" / "python"

# The accepted-capture device points (quoted, never re-measured here — the
# performance page's own standard). Values in MILLISECONDS, wall per frame,
# from the captures device-verify lists (2026-11-01 S3 sweep, M3/M4 P4).
DEVICE_POINTS = {
    "s3": [
        {"size": 240, "ms": 280.0, "note": "sweep intermediate, accepted 336-frame capture"},
        {"size": 400, "ms": 720.0, "note": "sweep final, accepted capture"},
        {"size": 466, "ms": 1300.0, "note": "full-res panel bake, accepted capture"},
    ],
    "p4": [
        {"size": 618, "ms": 600.0, "note": "sweep final, M3/M4 acceptance capture"},
        {"size": 650, "ms": 960.0, "note": "sweep final @650, M3/M4 acceptance capture"},
    ],
}
# One-time device steps from the same accepted captures (ms).
DEVICE_ONESHOT = {
    "s3": {"relief_build": 1300.0, "shademap_build": 1600.0},
    "p4": {"shademap_build": 282.0},
}
# The per-pipeline device costs (accepted captures, ms) — the rows of the
# old device-pipelines table, as plotted data. cat: bake / display / build /
# fade.
DEVICE_PIPELINES = [
    {"board": "S3", "name": "sweep bake @240 px (intermediate)", "ms": 280.0, "cat": "bake"},
    {"board": "S3", "name": "sweep final bake @400 px", "ms": 720.0, "cat": "bake"},
    {"board": "S3", "name": "full-res bake @466 px", "ms": 1300.0, "cat": "bake"},
    {"board": "P4", "name": "sweep final bake @618 px", "ms": 600.0, "cat": "bake"},
    {"board": "P4", "name": "sweep final bake @650 px", "ms": 960.0, "cat": "bake"},
    {"board": "S3", "name": "spin frame (gather+composite+write)", "ms": 45.0, "cat": "display"},
    {"board": "S3", "name": "dusk frame (re-project)", "ms": 50.0, "cat": "display"},
    {"board": "S3", "name": "sweep display step (per frame)", "ms": 230.0, "cat": "display"},
    {"board": "P4", "name": "dusk total (14–95 s over |phi*|)", "ms": 43000.0, "cat": "display"},
    {"board": "S3", "name": "shade-map build (once, at dusk entry)", "ms": 1600.0, "cat": "build"},
    {"board": "P4", "name": "shade-map build (once, at dusk entry)", "ms": 282.0, "cat": "build"},
    {"board": "S3", "name": "relief cache build (once, at boot)", "ms": 1300.0, "cat": "build"},
    {"board": "both", "name": "screen-switch fade (no pixels written)", "ms": 240.0, "cat": "fade"},
]
# On-device one-core CPU shares from the CONFIG_FREERTOS_HZ probe captures.
DEVICE_CPU = [
    {"name": "settings hub, idle (post loop-rate fix)", "pct": 8.0, "lo": 7.0, "hi": 9.0},
    {"name": "spin render, core 1 (gather + composite)", "pct": 48.0, "lo": 48.0, "hi": 48.0},
]


def run(cmd, cwd=None, env=None):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"FAILED ({r.returncode}): {' '.join(map(str, cmd))}\n{r.stderr[-2000:]}")
    return r.stdout


def ensure_bench(build_dir: Path, target: str) -> Path:
    exe = ROOT / "host_sim" / build_dir / "moon_bench"
    if not exe.exists():
        print(f"building moon_bench for {target} ({build_dir}) ...")
        cfg = ["cmake", "-B", build_dir, "-S", "."]
        if target == "p4":
            cfg += ["-DMOON_ASSETS_TARGET=p4"]
        run(cfg, cwd=ROOT / "host_sim")
        run(["cmake", "--build", build_dir, "--target", "moon_bench", "-j"],
            cwd=ROOT / "host_sim")
    return exe


LINE_RE = re.compile(
    r"size=\s*(\d+)\s+bake=\s*([\d.]+) ms\s+rotate=\s*([\d.]+) ms"
    r"\s+mapbuild=\s*([\d.]+) ms\s+reproj=\s*([\d.]+) ms")


def bench(bench_dir: str, sizes, iters, extra=()) -> dict:
    """One moon_bench invocation -> {size: {step: ms}, '_relief': ms}."""
    out = run([str(ROOT / "host_sim" / bench_dir / "moon_bench"),
               "--utc", UTC, "--iters", str(iters), "--sizes", *map(str, sizes),
               *extra])
    data = {"_relief": None, "_geo": None}
    for line in out.splitlines():
        m = LINE_RE.search(line)
        if m:
            size, bake, rot, mb, rp = m.groups()
            data[int(size)] = {"bake": float(bake), "rotate": float(rot),
                               "mapbuild": float(mb), "reproj": float(rp)}
        elif "relief build" in line:
            data["_relief"] = float(re.search(r"([\d.]+) ms", line).group(1))
        elif "geo=" in line:
            data["_geo"] = float(re.search(r"geo=([\d.]+)", line).group(1))
    missing = [s for s in sizes if s not in data]
    if missing:
        sys.exit(f"bench {bench_dir} {extra}: no lines for sizes {missing}\n{out}")
    return data


def measure_python(iters: int) -> dict:
    """The validated prototype, per size — the path check_gate.py validates."""
    script = f'''
import sys; sys.path.insert(0, {str(ROOT / "assets" / "images" / "moon")!r})
import time, json, numpy as np
from moon.astro import geometry
from moon.animation import render_frame
from moon.textures import load_textures
from moon import constants as C
alb, hgt = load_textures()
lut = np.asarray(C.TONE_LUT, dtype=np.uint8)
g = geometry({UTC!r})
out = {{}}
for size in {SIZES!r}:
    r = size / 2.0
    ts = []
    for i in range({iters}):
        t0 = time.perf_counter()
        render_frame(g, alb, hgt, lut, size=size, radius=r, cx=r, cy=r)
        ts.append((time.perf_counter() - t0) * 1e3)
    out[size] = sorted(ts)[len(ts) // 2]
print(json.dumps(out))
'''
    out = run([str(MOON_VENV), "-I", "-c", script],
              cwd=ROOT / "assets" / "images" / "moon").strip().splitlines()
    return {int(k): float(v) for k, v in json.loads(out[-1]).items()}


def measure_js(iters: int) -> tuple[dict, float]:
    out = run(["node", HERE / "perf_bench_moonlab.js", "--utc", UTC,
               "--iters", str(iters), "--sizes", *map(str, SIZES)])
    sizes, relief = {}, None
    for line in out.splitlines():
        p = line.split()
        if p[0] != "js":
            continue
        if p[1] == "relief":
            relief = float(p[2])
        else:
            sizes[int(p[1])] = float(p[2])
    if len(sizes) != len(SIZES) or relief is None:
        sys.exit(f"js bench incomplete:\n{out}")
    return sizes, relief


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=5)
    ap.add_argument("--out", default=str(HERE / "perf_data.json"))
    args = ap.parse_args()

    print(f"perf_measure: epoch {UTC}, iters {args.iters}, sizes {SIZES}")
    t0 = time.time()

    ensure_bench("build", "s3")
    ensure_bench("build-p4", "p4")

    print("  C host (S3 assets): default / --no-relief / --darkzero --neutral")
    host_s3 = {
        "default": bench("build", SIZES, args.iters),
        "no_relief": bench("build", SIZES, args.iters, ("--no-relief",)),
        "display": bench("build", SIZES, args.iters, ("--darkzero", "--neutral")),
    }
    print("  C host (P4 assets): default / --no-relief / --darkzero --neutral")
    host_p4 = {
        "default": bench("build-p4", SIZES, args.iters),
        "no_relief": bench("build-p4", SIZES, args.iters, ("--no-relief",)),
        "display": bench("build-p4", SIZES, args.iters, ("--darkzero", "--neutral")),
    }

    print("  Python (numpy float64, validated prototype) ...")
    python = measure_python(args.iters)
    print("  JS (V8 scalar, the interactive sim) ...")
    js, js_relief = measure_js(args.iters)

    data = {
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "utc": UTC,
        "iters": args.iters,
        "sizes": SIZES,
        "host": {"s3": host_s3, "p4": host_p4},
        "python": python,
        "js": {"bake": js, "relief_build": js_relief},
        "device": {"points": DEVICE_POINTS, "oneshot": DEVICE_ONESHOT,
                   "pipelines": DEVICE_PIPELINES, "cpu": DEVICE_CPU},
    }
    Path(args.out).write_text(json.dumps(data, indent=1) + "\n")
    print(f"wrote {args.out}  ({time.time() - t0:.0f} s)")


if __name__ == "__main__":
    main()
