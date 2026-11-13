#!/usr/bin/env python3
"""Generate the docs media: the fade-to-black screen-switch film (from uisim
screenshots) plus the NASA gate reference frames.

The only animation left on the home is the fade-to-black screen switch (the
spin / boot-sweep / dusk films were retired with their subsystems
2026-11-13).

Outputs (committed):
  docs/img/fade.mp4         the fade-to-black screen switch (airplane ->
                            settings hub: fade out, load, fade in -- the
                            UI_SCREEN_SWITCH_FADE legs, one whole-panel
                            flush per step)
  docs/img/nasa/            sample NASA SVS 5415 reference frames

Frame PNGs land in docs/img/frames/ (gitignored). Needs: uisim screenshots
(host_sim/uisim/build.sh run), PIL, ffmpeg.
"""

import datetime
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

REPO = pathlib.Path(__file__).resolve().parents[1]
FR = REPO / "docs" / "img" / "frames"
OUT = REPO / "docs" / "img"
UISIM = REPO / "host_sim" / "uisim" / "out"
NASA = REPO / "assets" / "images" / "moon" / "final_400x400"


def run(cmd):
    print(" ", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def clear(d):
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)


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

    # 1. fade-to-black screen switch: airplane (from) -> settings hub (to).
    #    The device dims the panel's OWN hardware brightness (the CO5300
    #    0x51 command, bsp_display_brightness_set) 100 -> 0 over the
    #    outgoing screen, loads the new screen and flushes it atomically
    #    while the panel is still black, then ramps 0 -> 100. The ramp is
    #    a LINEAR percentage schedule, so the frame at step s of the OUT
    #    leg is screen * (1 - s/12) and of the IN leg screen * (s/12) --
    #    exactly a linear blend toward/away from black, which is what we
    #    synthesize here (12 steps = FADE_STEPS; held frames pad the film
    #    to a readable 8 fps). The device's orientation pipeline rotates
    #    every screen +90 deg CW (the base mount offset), so both frames
    #    are rotated the same way first.
    print("== fade-to-black switch ==")
    frm = Image.open(UISIM / "modem_sleep.png").convert("RGB")
    to = Image.open(UISIM / "settings_hub.png").convert("RGB")
    frm = frm.rotate(-90, expand=False)
    to = to.rotate(-90, expand=False)
    W, H = to.size

    def scale(img, f):
        return Image.eval(img, lambda p: int(p * f))

    d = FR / "fade"
    clear(d)
    k = 0
    # hold the outgoing screen, dim it (12 steps to black)
    for _ in range(2):
        frm.save(d / f"sim.{k:04d}.png")
        k += 1
    for s in range(1, 13):
        scale(frm, 1 - s / 12).save(d / f"sim.{k:04d}.png")
        k += 1
    # hold on black, brighten the new screen in (12 steps to full)
    for _ in range(2):
        Image.new("RGB", (W, H), (0, 0, 0)).save(d / f"sim.{k:04d}.png")
        k += 1
    for s in range(1, 13):
        scale(to, s / 12).save(d / f"sim.{k:04d}.png")
        k += 1
    # hold the finished screen
    for _ in range(2):
        to.save(d / f"sim.{k:04d}.png")
        k += 1
    encode("fade", 8, d / "sim.%04d.png", OUT / "fade.mp4", size=W)

    # 2. NASA sample frames (the gate's reference set; a few for the docs).
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
