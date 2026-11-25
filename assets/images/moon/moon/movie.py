"""Boot movie: one precomputed synodic cycle of device-identical moon frames.

A startup movie that plays (looping) while the device computes the real
phase: ``N`` consecutive hourly bakes spanning ~one synodic month, each a
raw gray ``S x S`` disk baked by the PORTABLE C CORE (``moon_sim``), encoded
as a grayscale JPEG q90 (the same ``esp_new_jpeg`` codec the TF hour cache
runs -- a gray JPEG decodes to RGB888 with R==G==B, so the device's existing
take-R decode path works unchanged), packed into a single
``moon_movie.bin`` flashed into the ``moon_movie`` data partition in the
same ``idf.py flash`` as the app.

Why the C core, not ``moon.animation``: ``animation.py`` is the Python
ground-truth reference, not the device path -- its own header says the
on-device version steps the cheap fixed-point ephemeris (``moon_bake``).
The movie must be pixel-identical to a device bake, so it IS a device bake:
``moon_sim`` (the host_sim binary) with ``--size S --radius R --darkzero
--neutral --pgm`` reproduces the exact on-device call
(``moon_bake(..., S, R, S/2, S/2, ..., dark_zero=1, dk_sup=0.0, bev=NULL)``
-- the clocks' ``MOON_PX`` posture, the knob's ``knob_ui_set_disk_geom(150,
180)``).

Loop + catch-up: frame ``i``'s hour centre is ``ref_center + i*3600`` for a
fixed canonical ``ref_center`` (deterministic bin across builds). The span
``N-1`` hours ~ one synodic month (29.530588853 d = 708.73 h), so the wrap
from the last frame to the first is a sub-frame phase jump (44 min at
``N = 709``) -- no visible seam. The device maps ``now`` to a loop index by
hour arithmetic on ``ref_center`` (no new-moon scan); the movie frame vs the
live bake differ by <= 30 min of phase (the hour-centre staleness), so the
swap is seamless.

Sizing policy ("fill the space, never hinder the code build"): the movie is
the LAST partition row. The generator reads the target's ``moon_movie``
CSV row if present (the committed source of truth); otherwise it computes
the flash leftover (total chip -- fixed table region -- every other row)
and reports the row size to paste. It then bakes the FINEST
``hours_per_frame`` step (1, 2, 4, ...) whose total fits the row and emits
exactly that many frames. Shrink the row later and re-run: the generator
lands on a coarser step automatically. If zero frames fit it still emits a
valid zero-frame bin (the firmware falls back to the card / disc path).

Container (``moon_movie.bin``):
  header            62 B, little-endian (no padding): magic "MOOV",
                    calcver (16 s, NUL-padded), target (8 s), S, radius,
                    jpeg_q, hours_per_frame, frame_count (u64),
                    ref_center, table_off, table_len, frame_off, frame_len
  offset table      frame_count x u32 (offset of each frame RELATIVE to the
                    first frame; the device random-accesses a frame with one
                    flash read at catch-up)
  frames            the q90 grayscale JPEGs, packed back to back

Emits (one dir per board target, ``<out>/<target>/``, gitignored like the
rest of ``out/``):
  moon_movie.bin            the container above
  moon_movie_manifest.json  single-line JSON for the CMake consistency guard
Run:  python -m moon.movie [--target s3 p4 knob] [--out DIR]
      [--sim DIR] [--budget-kib N]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess

import numpy as np
from PIL import Image

from . import constants as C

# One synodic month, hours (29.530588853 d). N frames span N-1 hours; N =
# round(SYNODIC_H) + 1 makes the span within 30 min of a full cycle, so the
# loop wrap is sub-frame.
SYNODIC_H = 29.530588853 * 24.0

# Canonical frame 0 hour-centre: a fixed epoch (2026-01-01T01:00Z) so the
# bin is deterministic across builds; the phase cycle is event-independent
# (neutral bakes), so no real new moon is required.
REF_CENTER_EPOCH = 1767226800  # 2026-01-01T01:00:00Z

MAGIC = b"MOOV"
# Header fields (13, little-endian, no padding, 62 B): magic, calcver,
# target, s, radius, jpeg_q, hours_per_frame, frame_count (u64),
# ref_center, table_off, table_len, frame_off, frame_len.
HDR = struct.Struct("<4s16s8sHHBBQ5I")
assert HDR.size == 62

# Per board: (bake px S, disk radius R -- the exact device bake geometry:
# clocks bake MOON_PX with r = MOON_PX/2 centred (moon_ui.c), the knob bakes
# 360 px with r = 150 centred (knob_ui_set_disk_geom(150, 360/2)). All
# integer px.)
GEOM = {
    "s3": (400, 200),
    "p4": (650, 325),
    "knob": (360, 150),
}

# Partition tables + chip size per target (total KiB; the fixed
# bootloader+table region before the first row: 32K bootloader + 4K
# table -> rows start at 0x9000 on every board, the knob's explicit
# offsets being the witness).
TABLES = {
    "s3": {"csv": "partitions.csv", "total_kib": 16384, "fixed_kib": 0x9000 // 1024},
    "p4": {"csv": "partitions_p4.csv", "total_kib": 32768, "fixed_kib": 0x9000 // 1024},
    "knob": {"csv": "knob/partitions.csv", "total_kib": 16384, "fixed_kib": 0x9000 // 1024},
}

# Per-target moon_sim build dir (mirrors host_sim/check_gate.py).
SIM_BUILD_DIRS = {"s3": "build", "p4": "build-p4", "knob": "build-knob"}


def _csv_kib(text: str) -> int:
    """Sum the size column (KiB) of all data/app rows of a partition CSV."""
    total = 0
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.lower().startswith("name,"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 5:
            continue
        size = parts[4]
        m = re.fullmatch(r"0x[0-9A-Fa-f]+", size)
        if m:
            total += int(size, 16) // 1024
        elif size.endswith("K"):
            total += int(size[:-1])
        else:
            raise SystemExit("unparseable partition size %r in %s" % (size, line))
    return total


def movie_budget_kib(t: str, repo: str) -> int:
    """The movie row size: the committed ``moon_movie`` row if present, else
    the computed flash leftover (the row to paste)."""
    spec = TABLES[t]
    path = os.path.join(repo, spec["csv"])
    text = open(path).read()
    m = re.search(r"^moon_movie\s*,[^\n]*?,\s*([0-9]+)K", text, re.M)
    if m:
        return int(m.group(1))
    used = _csv_kib(text)
    budget = spec["total_kib"] - spec["fixed_kib"] - used
    if budget < 128:
        raise SystemExit(
            "[%s] no flash leftover for a moon_movie row (%d KiB used of %d KiB); "
            "shrink another row first." % (t, used, spec["total_kib"] - spec["fixed_kib"]))
    return budget


def _iso(epoch: int) -> str:
    """Epoch seconds -> ISO-8601 UTC (matches moon_sim's parser)."""
    import datetime as _dt
    return _dt.datetime.fromtimestamp(epoch, _dt.timezone.utc).strftime("%Y-%m-%dT%H:%M")


def frame_epochs(n: int, hpf: int) -> list[int]:
    """``n`` hour centres, ``hpf*3600`` s apart, starting at REF_CENTER."""
    step = hpf * 3600
    return [REF_CENTER_EPOCH + i * step for i in range(n)]


def bake_frames(t: str, sim: str, epochs: list[int], s: int, r: int,
                out_dir: str) -> list[str]:
    """Bake ``len(epochs)`` raw gray frames with the target's moon_sim
    (device-identical geometry + dark_zero + neutral) and return the PGM
    paths in order."""
    os.makedirs(out_dir, exist_ok=True)
    # One process, the even-spacing path: start = first centre, end = last
    # centre (exact multiples of 3600 s, so the spacing is exact).
    cmd = [sim, "--frames", str(len(epochs)), "--start", _iso(epochs[0]),
           "--end", _iso(epochs[-1]), "--size", str(s), "--radius", str(r),
           "--darkzero", "--neutral", "--pgm", "--out", out_dir]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise SystemExit("[%s] moon_sim bake failed:\n%s\n%s"
                         % (t, res.stdout[-800:], res.stderr[-2000:]))
    paths = [os.path.join(out_dir, "sim.%04d.pgm" % i) for i in range(len(epochs))]
    for p in paths:
        if not os.path.exists(p):
            raise SystemExit("[%s] missing bake output %s" % (t, p))
    return paths


def _read_pgm(path: str, s: int) -> np.ndarray:
    with open(path, "rb") as f:
        head = f.readline()
        if head.strip() != b"P5":
            raise SystemExit("bad PGM header in %s: %r" % (path, head))
        dims = f.readline().split()
        if int(dims[0]) != s or int(dims[1]) != s:
            raise SystemExit("PGM dims %s != %dx%d in %s"
                             % (dims, s, s, path))
        maxv = int(f.readline())
        if maxv != 255:
            raise SystemExit("PGM maxval %d != 255 in %s" % (maxv, path))
        data = f.read()
    if len(data) != s * s:
        raise SystemExit("PGM payload %d bytes != %d in %s" % (len(data), s * s, path))
    return np.frombuffer(data, dtype=np.uint8).reshape(s, s)


def encode_jpegs(t: str, paths: list[str], s: int, q: int,
                 out_dir: str) -> tuple[list[bytes], list[int]]:
    """PIL q-``q`` grayscale JPEGs of the raw bakes. Gray (single-component)
    JPEGs are what esp_new_jpeg probes as JPEG_PIXEL_FORMAT_GRAY; they decode
    to RGB888 with R==G==B on the device (the take-R path)."""
    blobs: list[bytes] = []
    sizes: list[int] = []
    for i, p in enumerate(paths):
        disk = _read_pgm(p, s)
        import io
        buf = io.BytesIO()
        Image.fromarray(disk, "L").save(buf, format="JPEG", quality=q)
        blobs.append(buf.getvalue())
        sizes.append(len(blobs[-1]))
        if (i + 1) % 50 == 0 or i + 1 == len(paths):
            print("[%s]   encoded %d/%d gray JPEGs (this one %d B)"
                  % (t, i + 1, len(paths), sizes[-1]))
    return blobs, sizes


def frame_count_for(hpf: int) -> int:
    """Frames spanning ~one synodic month at ``hpf`` h/frame: round the
    cycle to whole steps, +1 to close it (frame 0 ~ frame N-1)."""
    steps = int(round(SYNODIC_H / hpf))
    return steps + 1


def pack(t: str, calcver: str, s: int, r: int, q: int, hpf: int,
         blobs: list[bytes], ref_center: int, out_dir: str,
         budget_kib: int) -> dict:
    """Write moon_movie.bin (header + frames) + the manifest; return the
    manifest dict."""
    n = len(blobs)
    offsets: list[int] = []
    off = 0
    for b in blobs:
        offsets.append(off)
        off += len(b)
    table = struct.pack("<%dI" % n, *offsets) if n else b""
    table_off = HDR.size
    frame_off = HDR.size + len(table)
    payload = b"".join(blobs)
    hdr = HDR.pack(MAGIC, calcver.encode()[:16].ljust(16, b"\0"),
                   t.encode()[:8].ljust(8, b"\0"), s, r, q, hpf, n,
                   ref_center, table_off, len(table), frame_off, len(payload))
    bin_path = os.path.join(out_dir, "moon_movie.bin")
    with open(bin_path, "wb") as f:
        f.write(hdr)
        f.write(table)
        f.write(payload)
    total = frame_off + len(payload)
    if total > budget_kib * 1024:
        raise SystemExit(
            "[%s] moon_movie.bin %d B exceeds the %d KiB moon_movie row (%d B); "
            "bake at a coarser hours_per_frame (got %d) or widen the row."
            % (t, total, budget_kib, budget_kib * 1024, hpf))
    manifest = {
        "target": t,
        "calcver": calcver,
        "s": s,
        "radius": r,
        "jpeg_q": q,
        "hours_per_frame": hpf,
        "frame_count": n,
        "ref_center": ref_center,
        "payload_bytes": len(payload),
        "total_bytes": total,
        "budget_kib": budget_kib,
        "min_frame_bytes": min(len(b) for b in blobs) if blobs else 0,
        "max_frame_bytes": max(len(b) for b in blobs) if blobs else 0,
    }
    with open(os.path.join(out_dir, "moon_movie_manifest.json"), "w") as f:
        f.write(json.dumps(manifest, sort_keys=True,
                           separators=(",", ":")) + "\n")
    return manifest


def _calcver(repo: str) -> str:
    """The CMake calcver key: .cz.toml version line, else git describe, else
    'unknown' (mirrors the root CMakeLists)."""
    cz = os.path.join(repo, ".cz.toml")
    if os.path.exists(cz):
        m = re.search(r"version\s*=\s*\"([^\"]+)\"", open(cz).read())
        if m:
            return m.group(1)
    r = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"],
                       cwd=repo, capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    return "unknown"


def _target(t: str, sim_root: str, out_root: str, repo: str,
            budget_override: int | None, jpeg_q: int) -> None:
    s, r = GEOM[t]
    spec = TABLES[t]
    budget_kib = budget_override or movie_budget_kib(t, repo)
    sim = os.path.join(sim_root, SIM_BUILD_DIRS[t], "moon_sim")
    if not os.path.exists(sim):
        raise SystemExit(
            "missing moon_sim for %s (%s); build it first: cmake "
            "-DMOON_ASSETS_TARGET=%s -B <dir> && cmake --build <dir>"
            % (t, sim, t))
    calcver = _calcver(repo)
    out_dir = os.path.join(out_root, t)
    os.makedirs(out_dir, exist_ok=True)

    row = "the moon_movie row"
    m = re.search(r"^moon_movie\s*,[^\n]*?,\s*([0-9]+)K",
                  open(os.path.join(repo, spec["csv"])).read(), re.M)
    if not m:
        row = "the planned %d KiB moon_movie row (not in %s yet -- add it)" % (
            budget_kib, spec["csv"])
    print("[%s] movie: S=%d r=%d q=%d, %s = %d KiB"
          % (t, s, r, jpeg_q, row, budget_kib))

    # Size-adaptive: the finest hours_per_frame whose cycle fits. A full
    # bake at step 1 is host-cheap, so measure the real sizes per step.
    for hpf in (1, 2, 4, 8):
        n = frame_count_for(hpf)
        epochs = frame_epochs(n, hpf)
        print("[%s]   step %d h/frame: %d frames spanning %.2f d"
              % (t, hpf, n, (n - 1) * hpf / 24.0))
        paths = bake_frames(t, sim, epochs, s, r,
                            os.path.join(out_dir, "pgm-%dh" % hpf))
        blobs, _sizes = encode_jpegs(t, paths, s, jpeg_q, out_dir)
        if not blobs or HDR.size + sum(len(b) for b in blobs) <= budget_kib * 1024:
            manifest = pack(t, calcver, s, r, jpeg_q, hpf, blobs,
                            REF_CENTER_EPOCH, out_dir, budget_kib)
            print("[%s]   OK: %d frames @ q%d, %d B total "
                  "(min %d / max %d B per frame) -> %s"
                  % (t, manifest["frame_count"], jpeg_q, manifest["total_bytes"],
                     manifest["min_frame_bytes"], manifest["max_frame_bytes"],
                     os.path.join(out_dir, "moon_movie.bin")))
            return
        print("[%s]   does not fit the %d KiB row -> coarser step"
              % (t, budget_kib))
    raise SystemExit("[%s] not even %d frames @ 8 h/frame fit the %d KiB row; "
                     "widen the moon_movie row." % (t, frame_count_for(8), budget_kib))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None,
                    help="output ROOT dir (default: <pkg>/out; each target "
                         "writes to <root>/<target>/)")
    ap.add_argument("--target", nargs="+", choices=sorted(GEOM),
                    default=list(GEOM),
                    help="board targets to bake (default: all three)")
    ap.add_argument("--sim", default=None,
                    help="host_sim build ROOT (default: <repo>/host_sim)")
    ap.add_argument("--budget-kib", type=int, default=None,
                    help="override the moon_movie row size (KiB) for all "
                         "selected targets")
    ap.add_argument("--jpeg-q", type=int, default=90,
                    help="gray JPEG quality (default 90, the TF cache's)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    out_root = args.out or os.path.join(here, "out")
    sim_root = args.sim or os.path.join(repo, "host_sim")
    for t in args.target:
        _target(t, sim_root, out_root, repo, args.budget_kib, args.jpeg_q)


if __name__ == "__main__":
    raise SystemExit(main())
