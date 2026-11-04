#!/usr/bin/env python3
"""Composite uisim 466x466 screens into the Waveshare product photo.

The book's device composites (docs/book/_static/photos/sim_*.png) are a
uisim frame at true panel geometry over the Waveshare S3-Touch-AMOLED
product photo (waveshare-hero.webp, vendored here). This module is the
white-backdrop compositing core; composite_dark.py is the regeneration
entry point for the tracked assets (it replaces the photo's white studio
backdrop with the book's dark page background).

Pipeline per frame (the device path):
  1. RGB565 quantization — the CO5300 is an RGB565 panel; every frame hits
     the QSPI stream as 565 (LVGL expansion: c = (q<<shift)|(q>>(8-shift))).
  2. AMOLED hardware dim for the light-sleep shot: 0x51 brightness 25%
     scales EMITTED light, so dim in linear light after quantization
     (linearize sRGB -> x0.25 -> re-encode).
  3. Upscale to the glass active circle (43.76 mm window of the 48.96 mm
     disk, from the dimension drawing) and circular-mask.
  4. Glass finish: subtle edge shading + faint upper-left specular, then
     paste under the real bezel ring of the Waveshare hero photo.

Inputs (both must exist):
  waveshare-hero.webp, fit.json   vendored here (fit_device.py derives
                                   fit.json from the photo)
  host_sim/uisim/out/<screen>.png the uisim frames — gitignored, so run
                                   `host_sim/uisim/build.sh run` first.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
FIT = json.loads((HERE / "fit.json").read_text())
CX, CY = FIT["cx"], FIT["cy"]
# The Kasa fit converged on the lit-content edge (the inner bezel
# boundary — the one sharp circle in the photo; the rim outer edge is a
# soft shadow gradient). R_CONTENT is where our content must fill to;
# the rim ring stays the photo's own.
R_A = FIT["R_content"]
R_RIM = FIT["R_rim_outer"]
HERO = HERE / "waveshare-hero.webp"
UISIM = HERE.parents[2] / "host_sim" / "uisim" / "out"

# The five book figures: uisim screen name -> composite filename.
# light_sleep is the panel's hardware 0x51 brightness at 25% (dim).
JOBS = [
    ("modem_sleep", "sim_home.png", None),
    ("light_sleep", "sim_light_sleep.png", 0.25),
    ("alarm", "sim_alarm.png", None),
    ("settings_wifi_password", "sim_keyboard.png", None),
    ("deep_sleep", "sim_deep_sleep.png", None),
]


def srgb_to_lin(v):  # v in [0,1]
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(v):
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055)


def quant565(rgb):
    """888 -> 565 -> 888 (bit-replication, as LVGL / the panel path)."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    out = np.empty_like(rgb)
    out[..., 0] = (r5 << 3) | (r5 >> 2)
    out[..., 1] = (g6 << 2) | (g6 >> 4)
    out[..., 2] = (b5 << 3) | (b5 >> 2)
    return out


def hard_dim(rgb, frac):
    """Hardware brightness 0x51: dim emitted light in linear space."""
    v = srgb_to_lin(rgb / 255.0) * frac
    return (lin_to_srgb(np.clip(v, 0, 1)) * 255).astype(np.uint8)


def make_glass(R):
    d = int(2 * R)
    yy, xx = np.mgrid[0:d, 0:d]
    r = np.hypot(xx - (d - 1) / 2, yy - (d - 1) / 2)
    # Circular mask with a 1.5 px feather (the glass edge clips the last row).
    alpha = np.clip((R - 0.5 - r) / 1.5 + 1, 0, 1)
    # Edge shading: the curved glass darkens its own rim slightly
    # (only the last ~2% — the content is full-bleed to the bezel).
    t = np.clip((r - R * 0.98) / (R * 0.02), 0, 1)
    shade = 1.0 - 0.14 * t * t
    # Faint upper-left specular on the glass (broad, very low alpha),
    # in the local d x d grid: offset up-left from the circle centre.
    sx, sy = d / 2 - R * 0.38, d / 2 - R * 0.42
    spec = np.exp(-((xx - sx) ** 2) / (2 * (R * 0.55) ** 2)
                  - ((yy - sy) ** 2) / (2 * (R * 0.5) ** 2))
    spec = (spec * 0.045).astype(np.float32)
    return d, alpha.astype(np.float32), shade.astype(np.float32), spec


def composite(screen_name, out_path, dim=None):
    hero = Image.open(HERO).convert("RGB")
    d, alpha, shade, spec = make_glass(R_A)
    img = Image.open(UISIM / f"{screen_name}.png").convert("RGB")
    rgb = quant565(np.asarray(img, dtype=np.uint8))
    if dim:
        rgb = hard_dim(rgb, dim)
    content = Image.fromarray(rgb, "RGB").resize((d, d), Image.LANCZOS)
    c = np.asarray(content, dtype=np.float32) / 255.0
    c = c * shade[..., None]
    # Specular adds a little white over the content.
    c = c + spec[..., None]
    c = np.clip(c, 0, 1)
    layer = Image.fromarray((c * 255).astype(np.uint8), "RGB")
    mask = Image.fromarray((alpha * 255).astype(np.uint8), "L")
    # Paste centered on the fitted glass center.
    px, py = int(round(CX - R_A)), int(round(CY - R_A))
    hero.paste(layer, (px, py), mask)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    hero.save(out_path)
    print(f"wrote {out_path} (active d={d}, dim={dim})")


if __name__ == "__main__":
    # Scratch entry point (the white-backdrop composites). The book's
    # tracked figures are the dark-backdrop ones — regenerate those with
    # composite_dark.py. The default out dir is a scratch dir so a bare
    # run never clobbers the tracked sim_*.png.
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--out",
        default=str(Path(__file__).parent / "scratch"),
        help="output dir for the white-backdrop composites (scratch)",
    )
    args = ap.parse_args()
    for name, out, dim in JOBS:
        composite(name, Path(args.out) / out, dim)
