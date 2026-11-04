#!/usr/bin/env python3
"""Generate the docs media: exact panel frames (the device's own core via
moon_sim) -> MP4 movies, plus the fade-to-black screen switch.

Outputs (committed):
  docs/img/boot_sweep.mp4   the boot synodic sweep (336 frames @240 px,
                            new moon -> now, the device's own sweep)
  docs/img/free_spin.mp4    the settled free spin (36 frames @466 px, 10 deg)
  docs/img/dusk.mp4         the pole-axis sleep spin (0 -> phi* @240 px)
  docs/img/fade.mp4         the fade-to-black screen switch (airplane ->
                            settings hub: fade out, load, fade in -- the
                            UI_SCREEN_SWITCH_FADE legs, one whole-panel
                            flush per step)
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

    # 3. dusk: the DEVICE's light-sleep dusk, mirrored 1:1 by
    #    moon_sim --dusk (one-time shade map at the endpoint's rim-sliver
    #    kill bound + the lit-fade companion, one re-projected frame per
    #    degree of pole spin along the shortest arc to the fully
    #    unilluminated view, then the final dk_kill bake). The lit side
    #    fades to the backlit dark side over the spin (the "earth ray"
    #    complete-kill, 2026-11-04) -- the film shows that fade. 240 px.
    print("== dusk (device --dusk mirror @240 px) ==")
    d = FR / "dusk"
    clear(d)
    run([SIM, "--dusk", "--utc", iso(NOW), "--size", "240", "--out", str(d)])
    files = sorted(d.glob("dusk.[0-9][0-9][0-9].png"))
    print(f"  {len(files)} dusk frames (shortest arc to the dark side)")
    # clear the trim dir: a longer list from a previous run would leave
    # stale high-numbered frames behind and the ffmpeg sequence would read
    # them.
    clear(FR / "dusk_trim")
    for i, f in enumerate(files):
        shutil.copy(f, FR / "dusk_trim" / f"sim.{i:04d}.png")
    encode("dusk", 5, FR / "dusk_trim" / "sim.%04d.png", OUT / "dusk.mp4", size=240)

    # 4. fade-to-black screen switch: airplane (from) -> settings hub (to).
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
