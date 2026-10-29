#!/usr/bin/env python3
"""Generate the docs media: exact panel frames (the device's own core via
moon_sim) -> MP4 movies, plus the concentric screen-switch reveal.

Outputs (committed):
  docs/img/boot_sweep.mp4   the boot synodic sweep (336 frames @240 px,
                            new moon -> now, the device's own sweep)
  docs/img/free_spin.mp4    the settled free spin (36 frames @466 px, 10 deg)
  docs/img/dusk.mp4         the pole-axis sleep spin (0 -> phi* @240 px)
  docs/img/reveal.mp4       the concentric screen-switch reveal
                            (airplane -> settings hub, center-out 25-row
                            strips, the 9dad4fe UI_SCREEN_SWITCH_REVEAL)
  docs/img/nasa/            sample NASA SVS 5415 reference frames

Frame PNGs land in docs/img/frames/ (gitignored). Needs: moon_sim
(cmake --build host_sim/build), uisim screenshots (host_sim/uisim/build.sh
run), PIL, ffmpeg.
"""

import datetime
import math
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

REPO = pathlib.Path(__file__).resolve().parents[1]
SIM = REPO / "host_sim" / "build" / "moon_sim"
FR = REPO / "docs" / "img" / "frames"
OUT = REPO / "docs" / "img"
UISIM = REPO / "host_sim" / "uisim" / "out"
NASA = REPO / "assets" / "images" / "moon" / "final_400x400"
SYNODIC = 29.530588853 * 86400.0
REF_NEW_MOON = datetime.datetime(2026, 1, 18, 11, 57, tzinfo=datetime.timezone.utc)

NOW = datetime.datetime.now(datetime.timezone.utc).replace(second=0, microsecond=0)


def iso(dt):
    return dt.strftime("%Y-%m-%dT%H:%M")


def last_new_moon(now):
    k = math.floor((now - REF_NEW_MOON).total_seconds() / SYNODIC)
    return REF_NEW_MOON + datetime.timedelta(seconds=k * SYNODIC)


def run(cmd):
    print(" ", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def clear(d):
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)


def render_list(name, lines, size):
    d = FR / name
    clear(d)
    lst = d / "list.txt"
    lst.write_text("\n".join(lines) + "\n")
    run([SIM, "--list", str(lst), "--size", str(size), "--out", str(d)])


def encode(name, fps, src, dst, frames=None, size=None):
    args = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps),
            "-i", str(src)]
    v = ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20"]
    if size:
        v += ["-vf", f"scale={size}:{size}"]
    if frames is not None:
        v += ["-frames:v", str(frames)]
    run(args + v + [str(dst)])


def main():
    t0 = datetime.datetime.now(datetime.timezone.utc)
    nm = last_new_moon(NOW)
    print(f"NOW={iso(NOW)}  last new moon={iso(nm)}")

    # 1. boot sweep: the device's own sweep (336 frames @240 px).
    print("== boot sweep (336 frames @240 px) ==")
    lines = []
    for i in range(336):
        t = nm + datetime.timedelta(seconds=(NOW - nm).total_seconds() * i / 335.0)
        lines.append(f"{iso(t)} 0 0")
    render_list("boot", lines, 240)
    encode("boot", 5, FR / "boot" / "sim.%04d.png", OUT / "boot_sweep.mp4", size=240)

    # 2. free spin: 36 frames @466 px, 10 deg steps.
    print("== free spin (36 frames @466 px) ==")
    render_list("spin", [f"{iso(NOW)} {r} 0" for r in range(0, 360, 10)], 466)
    encode("spin", 8, FR / "spin" / "sim.%04d.png", OUT / "free_spin.mp4", size=466)

    # 3. dusk: pole 0..179 @240 px; cut at the fully-dark endpoint (the
    #    bright-pixel minimum = phi*).
    print("== dusk (pole 0..179 @240 px) ==")
    render_list("dusk", [f"{iso(NOW)} 0 {p}" for p in range(180)], 240)
    files = sorted((FR / "dusk").glob("sim.*.png"))
    counts = []
    for f in files:
        g = Image.open(f).convert("L")
        counts.append(sum(1 for px in g.getdata() if px > 48))
    # smooth (5-frame box) and find the minimum in the last third
    smooth = [sum(counts[max(0, i - 2):i + 3]) / len(counts[max(0, i - 2):i + 3])
              for i in range(len(counts))]
    third = len(counts) // 3
    endpoint = third + min(range(third, len(counts)), key=lambda i: smooth[i])
    endpoint = min(endpoint + 2, len(counts) - 1)
    print(f"  dusk endpoint (phi*) at frame {endpoint} (~{endpoint} deg)")
    # trim the PNG list to the endpoint so ffmpeg sees a contiguous sequence
    (FR / "dusk_trim").mkdir(exist_ok=True)
    for i in range(endpoint + 1):
        shutil.copy(files[i], FR / "dusk_trim" / f"sim.{i:04d}.png")
    encode("dusk", 5, FR / "dusk_trim" / "sim.%04d.png", OUT / "dusk.mp4", size=240)

    # 4. concentric reveal: airplane (from) -> settings hub (to), the same
    #    25-row strips in the same center-out order the device writes.
    print("== concentric reveal ==")
    frm = Image.open(UISIM / "modem_sleep.png").convert("RGB")
    to = Image.open(UISIM / "settings_hub.png").convert("RGB")
    W, H = to.size
    strips = []
    i = 0
    while i * 25 < H:
        y0 = i * 25
        y1 = min(y0 + 25, H)
        strips.append((y0, y1))
        i += 1
    strips.sort(key=lambda s: abs((s[0] + s[1] - 1) // 2 - H // 2))
    d = FR / "reveal"
    clear(d)
    for k in range(len(strips) + 2):  # +2 hold frames on the finished screen
        img = frm.copy()
        for j in range(min(k, len(strips))):
            y0, y1 = strips[j]
            img.paste(to.crop((0, y0, W, y1)), (0, y0))
        img.save(d / f"sim.{k:04d}.png")
    encode("reveal", 7, d / "sim.%04d.png", OUT / "reveal.mp4", size=W)

    # 5. NASA sample frames (the gate's reference set; a few for the docs).
    print("== NASA sample frames ==")
    nd = OUT / "nasa"
    clear(nd)
    frames = sorted(NASA.glob("moon.*.png"))
    pick = [0, len(frames) // 4, len(frames) // 2, 3 * len(frames) // 4, len(frames) - 1]
    for idx in pick:
        dst = nd / f"nasa_{idx:05d}.png"
        shutil.copy(frames[idx], dst)
        print(f"  {dst.name} <- {frames[idx].name}")

    print(f"done in {(datetime.datetime.now(datetime.timezone.utc) - t0).total_seconds():.0f} s")


if __name__ == "__main__":
    sys.exit(main())
