#!/usr/bin/env python3
"""Regenerate the five book device composites with a dark backdrop.

The Waveshare product photo's studio backdrop is white; on the book's
dark pages that white card glares (ch.1 hero, ch.15 x3, ch.21). This
reuses composite_panel's device build (fit + glass finish, unchanged)
and replaces the studio backdrop with the book's dark background
(#131416, furo dark --color-background-primary) plus a soft shadow
under the device, so the composites sit on the page instead of
fighting it.

Regenerates docs/_static/photos/sim_{home,light_sleep,alarm,
keyboard,deep_sleep}.png. Run (from the repo root):

    host_sim/uisim/build.sh run          # the uisim frames (gitignored)
    python3 docs/tools/composite_dark.py

The hero photo + fit are vendored next to this script
(waveshare-hero.webp, fit.json — derive the fit with fit_device.py if
the photo is ever swapped).
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import composite_panel as cp  # noqa: E402

PHOTOS = HERE.parent / "_static" / "photos"
DARK = (19, 20, 22)  # #131416, furo dark --color-background-primary
CX, CY = cp.FIT["cx"], cp.FIT["cy"]
R = cp.FIT["R_rim_outer"]


def dark_backdrop(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    a = np.asarray(im, dtype=np.float32) / 255.0
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.hypot(xx - CX, yy - CY)
    # Device disc (bezel + its rim highlight) stays as the photo built it.
    # Feather just outside R (rim) over ~0.08*R so the studio backdrop
    # goes dark right at the device edge instead of leaving a soft halo.
    feather = np.clip((r - (R - 2)) / (R * 0.08), 0, 1)
    dark = np.array(DARK, dtype=np.float32) / 255.0
    # Soft drop shadow: a dimmed ellipse just below the device.
    syy = np.clip(
        1.0 - ((xx - CX) / (R * 1.25)) ** 2 - ((yy - (CY + R * 0.62)) / (R * 0.42)) ** 2, 0, 1
    )
    shadow = syy[..., None] * 0.10 * (1 - feather)[..., None]
    a = a * (1 - feather[..., None]) + dark * feather[..., None]
    a = a - shadow
    a = np.clip(a, 0, 1)
    Image.fromarray((a * 255).astype(np.uint8), "RGB").save(path)
    print(f"backdrop dark: {path}")


if __name__ == "__main__":
    scratch = HERE / "scratch"
    scratch.mkdir(exist_ok=True)
    PHOTOS.mkdir(parents=True, exist_ok=True)  # media is build output — a clean tree has no dir
    for name, out, dim in cp.JOBS:
        tmp = scratch / f"dark_{out}"
        cp.composite(name, tmp, dim)
        dark_backdrop(tmp)
        Image.open(tmp).save(PHOTOS / out)
        print(f"-> {PHOTOS / out}")
