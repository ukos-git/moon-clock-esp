#!/usr/bin/env python3
"""Render the README animation demos: three montages of exact panel frames
rendered by moon_sim (the device's own core, at --size 466).

Deps: the moon_sim binary (cmake --build host_sim/build) + PIL.

Montages (docs/img/, committed):
  boot_sweep.png   one synodic month, new moon -> now (12 frames)
  free_spin.png    the settled free spin: 8 rotations at 45 deg steps
  dusk.png         the pole-axis spin to the dark side (0 -> phi*)

Each cell is downscaled to ~200 px; the full 466 px frames are the exact
RGB565 panel pixels (see the PNG decode in moon_sim main.c).
"""

import argparse
import datetime
import math
import pathlib
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = pathlib.Path(__file__).resolve().parents[1]
SIM = REPO / "host_sim" / "build" / "moon_sim"
OUT = REPO / "docs" / "img"
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
]

CELL = 200  # downscale target per frame
PAD = 12
COLS = 4
TITLE_H = 40
LABEL_H = 20

SYNODIC = 29.530588853 * 86400.0  # mean synodic month, seconds
REF_NEW_MOON = datetime.datetime(2026, 1, 18, 11, 57, tzinfo=datetime.timezone.utc)  # JPL


def iso(dt):
    return dt.strftime("%Y-%m-%dT%H:%M")


def label(dt):
    return dt.strftime("%b %d %H:%M")


def last_new_moon(now):
    k = math.floor((now - REF_NEW_MOON).total_seconds() / SYNODIC)
    return REF_NEW_MOON + datetime.timedelta(seconds=k * SYNODIC)


def render_frame(out_dir, utc, rot=0, pole=0, darkzero=False):
    args = [str(SIM), "--utc", utc, "--rot", str(rot), "--pole", str(pole),
            "--size", "466", "--out", str(out_dir)]
    if darkzero:
        args.append("--darkzero")
    subprocess.run(args, check=True, stderr=subprocess.DEVNULL)
    return out_dir / "sim.png"


def montage(path, title, cells, labels):
    """cells: [PIL.Image] (square), labels: one string per cell."""
    rows = math.ceil(len(cells) / COLS)
    w = PAD + COLS * (CELL + PAD)
    h = TITLE_H + rows * (CELL + LABEL_H + PAD)
    img = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(img)
    try:
        font_title = ImageFont.truetype(next(f for f in FONT_CANDIDATES if pathlib.Path(f).exists()), 22)
        font_cell = ImageFont.truetype(next(f for f in FONT_CANDIDATES if pathlib.Path(f).exists()), 14)
    except (StopIteration, OSError):
        font_title = ImageFont.load_default()
        font_cell = ImageFont.load_default()
    d.text((PAD, 8), title, font=font_title, fill=(255, 255, 255))
    for i, (cell, lab) in enumerate(zip(cells, labels)):
        r, c = divmod(i, COLS)
        x = PAD + c * (CELL + PAD)
        y = TITLE_H + r * (CELL + LABEL_H + PAD)
        small = cell.resize((CELL, CELL), Image.LANCZOS)
        img.paste(small, (x, y))
        d.text((x, y + CELL + 2), lab, font=font_cell, fill=(190, 190, 190))
    img.save(path)
    print(f"{path}: {len(cells)} frames, {path.stat().st_size // 1024} KiB")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--now", default="2026-10-28T12:00", help="demo epoch (ISO, UTC)")
    ap.add_argument("--phi-star", type=float, default=None,
                    help="dusk endpoint in degrees for the demo epoch (device 'dusk started' log or phi_star probe)")
    args = ap.parse_args()

    now = datetime.datetime.strptime(args.now, "%Y-%m-%dT%H:%M").replace(tzinfo=datetime.timezone.utc)
    new_moon = last_new_moon(now)
    OUT.mkdir(parents=True, exist_ok=True)
    frames = OUT / "frames"

    def frame_dir(name, i):
        p = frames / name
        p.mkdir(parents=True, exist_ok=True)
        return p

    # 1. Boot sweep: new moon -> now, 12 even steps. dark_zero on every
    # frame but the last (the device's montage path: the final frame is the
    # full current phase).
    steps = 12
    t0, t1 = new_moon.timestamp(), now.timestamp()
    cells, labs = [], []
    for k in range(steps):
        t = t0 + (t1 - t0) * k / (steps - 1)
        utc = iso(datetime.datetime.fromtimestamp(t, datetime.timezone.utc))
        p = render_frame(frame_dir("boot", k), utc, darkzero=k < steps - 1)
        cells.append(Image.open(p).convert("RGB"))
        labs.append(label(datetime.datetime.fromtimestamp(t, datetime.timezone.utc)))
    montage(OUT / "boot_sweep.png", f"Boot sweep -- one synodic month ({label(new_moon)} -> {label(now)})", cells, labs)

    # 2. Free spin: settled pose, 8 rotations at 45 deg.
    cells, labs = [], []
    for rot in range(0, 360, 45):
        p = render_frame(frame_dir("spin", rot), iso(now), rot=rot)
        cells.append(Image.open(p).convert("RGB"))
        labs.append(f"rot {rot}\xb0")
    montage(OUT / "free_spin.png", f"Free spin -- {label(now)} (8 rotations at 45\xb0)", cells, labs)

    # 3. Dusk: pole-axis spin 0 -> phi* (device endpoint, 1 deg/frame).
    phi = args.phi_star
    if phi is None:
        # The endpoint is epoch-specific: pass it with --phi-star (device
        # log 'dusk started: N frames (+N deg about the pole)' or the
        # phi_star probe compiled against components/moon).
        print("pass --phi-star DEG (the dusk endpoint for the demo epoch)")
        sys.exit(1)
    n = 9
    cells, labs = [], []
    for k in range(n):
        pole = round(phi * k / (n - 1))
        p = render_frame(frame_dir("dusk", pole), iso(now), pole=pole)
        cells.append(Image.open(p).convert("RGB"))
        labs.append(f"pole {pole}\xb0")
    montage(OUT / "dusk.png", f"Dusk -- pole spin to the dark side (0 -> {phi:.0f}\xb0)", cells, labs)


if __name__ == "__main__":
    main()
