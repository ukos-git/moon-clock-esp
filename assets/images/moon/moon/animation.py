"""Startup animation: the last synodic month, sub-sampled into phase frames.

Renders a sequence of static moon-phase frames covering the 29.53-day cycle
ending at a given UTC instant, using the same validated path as
``moon.validate`` (``render`` + frozen tone LUT + phase-independent
earthshine). No registration (nothing to align to) -- this is the
on-device startup animation reference. Emits one PNG per frame and an
optional mp4 (via ffmpeg).

The on-device version steps the *cheap* fixed-point ephemeris across the same
frame set (Phase 3, ``moon_bake`` + ``moon_render``); this Python render is
the ground-truth look to beat.

Run:  python -m moon.animation [--frames 120] [--end 2026-10-24T12:00]
             [--out /tmp/moon-anim] [--fps 24] [--mp4]
      (default end = current UTC, 120 frames, no mp4)
"""
from __future__ import annotations

import argparse
import os
import subprocess
from datetime import datetime, timedelta, timezone

import numpy as np
from PIL import Image

from . import constants as C
from .astro import geometry
from .renderer import tonemap, to_uint8, render
from .textures import load_textures

# One synodic month (new moon -> new moon), days.
SYNODIC_DAYS = 29.530589


def frame_times(end_utc: str, n: int) -> list[str]:
    """``n`` evenly spaced ISO-8601 UTC instants, ending at ``end_utc``,
    spanning one synodic month. New moon (or whatever phase the end is) is the
    last frame; the cycle rolls back from there."""
    end = datetime.fromisoformat(end_utc).replace(tzinfo=timezone.utc)
    span = timedelta(days=SYNODIC_DAYS)
    start = end - span
    # n frames, inclusive of both ends -> n-1 intervals.
    step = span / (n - 1) if n > 1 else timedelta(0)
    return [(start + i * step).strftime("%Y-%m-%dT%H:%M") for i in range(n)]


def render_frame(g, alb, hgt, lut: np.ndarray, size: int = C.IMAGE_SIZE,
                 radius: float = 199.3, cx: float = 199.3, cy: float = 199.5):
    """One frame: geometry -> raw -> tone LUT -> earthshine -> uint8 disk."""
    raw, dterm = render(g, alb, hgt, size=size, radius=radius, cx=cx, cy=cy,
                        gain=1.0)
    out = to_uint8(tonemap(raw, dterm, lut))
    return out


def make_frames(end_utc: str, n: int, out_dir: str) -> list[str]:
    """Render ``n`` phase frames to ``out_dir``. Returns the PNG paths."""
    os.makedirs(out_dir, exist_ok=True)
    alb, hgt = load_textures()
    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)
    paths = []
    for k, iso in enumerate(frame_times(end_utc, n)):
        g = geometry(iso)
        out = render_frame(g, alb, hgt, lut)
        p = os.path.join(out_dir, "anim.%03d.png" % k)
        Image.fromarray(out.astype(np.uint8)).save(p)
        paths.append(p)
        if k % 10 == 0:
            print("  frame %3d/%d  %s  illum %.3f" % (k + 1, n, iso, g.illum))
    print("  wrote %d frames to %s" % (len(paths), out_dir))
    return paths


def make_mp4(paths: list[str], out_dir: str, fps: int = 24) -> str | None:
    """Stitch the PNGs into an mp4 (ffmpeg). Returns the path, or None."""
    mp4 = os.path.join(out_dir, "moon_anim.mp4")
    cmd = ["ffmpeg", "-y", "-framerate", str(fps), "-i",
           os.path.join(out_dir, "anim.%03d.png"),
           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", mp4]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("ffmpeg failed:\n" + r.stderr[-800:])
        return None
    print("  wrote %s" % mp4)
    return mp4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--end", default=None,
                    help="end UTC (ISO); default = now")
    ap.add_argument("--out", default="/tmp/moon-anim")
    ap.add_argument("--fps", type=int, default=24)
    ap.add_argument("--mp4", action="store_true")
    args = ap.parse_args()

    end = args.end or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M")
    print("animation: %d frames ending %s (last synodic month)" %
          (args.frames, end))
    paths = make_frames(end, args.frames, args.out)
    if args.mp4:
        make_mp4(paths, args.out, args.fps)


if __name__ == "__main__":
    main()
