#!/usr/bin/env python3
"""Enclose a uisim screen in the round product housing, alpha outside.

The UI pages' annotated screenshots (docs/img/screens/annot/*_{s3,p4}.png)
are the uisim frame pasted into a flat rectangle with a legend strip on
the side — on the docs' black theme the black UI background is
indistinguishable from the page background (the settings screens read as
a floating blob). This tool renders the same annotated shot as the
DEVICE: the uisim frame at true panel geometry inside the real Waveshare
product photo's glass (the same make_glass finish and hero photo the
start page's figure uses — composite_panel.py), the numbered callout
badges + leaders from annotate.py on top, the legend strip dropped (every
UI page already carries the full Elements table, so the strip duplicated
it), and everything outside the device's outer rim transparent (with a
soft grounding shadow, as in composite_dark.py).

CLI mirrors annotate.py so moonmedia's _annotate_all swaps scripts:

    enclose.py BASE.png --spec docs/ui/labels/settings-hub.json \
             --panel 466 -o docs/img/screens/annot/settings-hub_s3.png

Panel → (hero photo, fit): 466 → waveshare-hero.webp / fit.json (the S3
1.75 AMOLED product photo); 720 → waveshare-p4-hero.webp / fit_p4.json
(the P4 4C product photo; fit_device.py --bright-style fit derived it
from the white studio backdrop edge + the 101.52/126 mm dimension ratio).

Badges whose position spec puts them on the bezel are clamped to sit
just inside the outer rim, so no callout floats in the transparent
surround.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import annotate  # noqa: E402  (badge + load_font, the callout drawing)
import composite_panel as cp  # noqa: E402  (make_glass: the shared glass finish)

DESIGN = 466  # the spec coordinate space (annotate.py's contract)
PANELS = {
    466: (HERE / "waveshare-hero.webp", HERE / "fit.json"),
    720: (HERE / "waveshare-p4-hero.webp", HERE / "fit_p4.json"),
}
BADGE_R = annotate.BADGE_R      # 9 design px
LEADER = annotate.LEADER        # 14 design px
BADGE_GAP = annotate.BADGE_GAP  # 9 design px


def enclose(base_path, spec_path, panel, out_path):
    hero_path, fit_path = PANELS[panel]
    fit = json.loads(fit_path.read_text())
    cx, cy = fit["cx"], fit["cy"]
    r_content, r_rim = fit["R_content"], fit["R_rim_outer"]
    # photo px per design px: the whole panel frame fills the glass circle
    s = 2 * r_content / DESIGN

    hero = Image.open(hero_path).convert("RGB")
    w, h = hero.size
    hero_np = np.asarray(hero, dtype=np.float32)

    # --- kill the product photo's own wallpaper at the content edge
    # (the P4 hero shows a colourful wallpaper out to the display edge;
    # our paste's 1.5 px feather would blend its saturated rim pixels into
    # a chromatic fringe). The photo's ring there is pure black anyway —
    # repaint the last few px black so the feather blends against black.
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.hypot(xx - cx, yy - cy)
    hero_np[r < r_content + 3] = 0.0

    # --- the glass content: uisim frame at true panel geometry
    d, alpha, shade, spec = cp.make_glass(r_content)
    img = Image.open(base_path).convert("RGB")
    if img.size != (panel, panel):
        raise SystemExit(f"{base_path}: expected {panel}x{panel}, got {img.size}")
    c = np.asarray(img.resize((d, d), Image.LANCZOS), dtype=np.float32)
    c = np.clip(c * shade[..., None] + spec[..., None] * 255.0, 0, 255)
    # numpy paste (not PIL — the float32 hero_np is a copy, a PIL paste()
    # after asarray would never reach the array the output is built from)
    px, py = int(round(cx - r_content)), int(round(cy - r_content))
    px, py = max(px, 0), max(py, 0)
    dd = min(d, w - px, h - py)
    reg = hero_np[py:py + dd, px:px + dd]
    a3 = alpha[:dd, :dd][..., None]
    hero_np[py:py + dd, px:px + dd] = reg * (1.0 - a3) + c[:dd, :dd] * a3

    # --- alpha: opaque over the device, transparent over the studio
    # backdrop. A fixed-radius feather cannot do it: the white backdrop
    # starts well inside the fitted rim (a soft antialiased edge), which
    # leaves a white halo ring on the dark theme. Key the surround off the
    # photo's own near-white pixels beyond the glass instead (both heroes
    # are bimodal — bezel band max ~180, backdrop >= 252), and keep the
    # radial feather to fade the S3's dark studio-shadow gradient outside
    # the rim.
    bg_a = np.clip((250.0 - hero_np.min(axis=2)) / 8.0, 0, 1)
    feather = np.clip((r - (r_rim - 2)) / (r_rim * 0.08), 0, 1)
    device_a = 1.0 - feather
    device_a = np.where(r > r_content, np.minimum(device_a, bg_a), device_a)
    # Grounding shadow: composite_dark's ellipse, but deep enough (ry 0.62R)
    # that the crescent OUTSIDE the rim survives — composite_dark hides most
    # of its shadow behind the device, a transparent surround cannot.
    syy = np.clip(
        1.0 - ((xx - cx) / (r_rim * 1.15)) ** 2
        - ((yy - (cy + r_rim * 0.55)) / (r_rim * 0.62)) ** 2, 0, 1
    )
    shadow_a = syy * 0.18 * (1.0 - device_a)  # shadow lives outside the device
    out_a = np.maximum(device_a, shadow_a)
    rgb = hero_np
    sh = (out_a > device_a)[..., None]
    rgb = np.where(sh, 0.0, rgb)             # shadow ink is black

    out = Image.fromarray(
        np.dstack([rgb.astype(np.uint8), (out_a * 255).astype(np.uint8)]), "RGBA"
    )

    # --- callouts: badges + leaders in photo coords
    spec_j = json.loads(Path(spec_path).read_text())
    labels = spec_j["labels"] if isinstance(spec_j, dict) else spec_j
    if not labels:
        raise SystemExit("empty label spec")
    big = annotate.load_font(int(round(13 * s)))
    ddraw = ImageDraw.Draw(out)
    max_c = r_rim - 2 - BADGE_R * s          # badge centre ceiling (photo px)
    max_p = r_content - 3                    # element-point ceiling (photo px)
    for l in labels:
        x, y = cx + (l["x"] - DESIGN / 2) * s, cy + (l["y"] - DESIGN / 2) * s
        # "tap anywhere"-style points sit in the panel SQUARE's corners,
        # which are off the round glass: pull them just inside the glass so
        # the leader does not start in the transparent surround.
        pr = np.hypot(x - cx, y - cy)
        if pr > max_p:
            x, y = cx + (x - cx) * max_p / pr, cy + (y - cy) * max_p / pr
        dx, dy = {"n": (0, -1), "s": (0, 1), "w": (-1, 0), "e": (1, 0)}[l["dir"]]
        off = (LEADER + BADGE_R + BADGE_GAP) * s
        bx, by = x + dx * off, y + dy * off
        # clamp to the bezel so the callout never floats in the surround
        vx, vy = bx - cx, by - cy
        vr = np.hypot(vx, vy)
        if vr > max_c:
            bx, by = cx + vx * max_c / vr, cy + vy * max_c / vr
            vx, vy = bx - cx, by - cy
            vr = np.hypot(vx, vy)
        br = BADGE_R * s
        ux, uy = (vx / vr, vy / vr) if vr else (0.0, 0.0)
        ddraw.line([x, y, bx - ux * br, by - uy * br], fill=(242, 243, 245),
                   width=max(1, int(round(2 * s))))
        annotate.badge(ddraw, bx, by, br, l["id"], big)

    # --- crop to the device + shadow margin
    m = int(round(r_rim * 1.30))
    box = (max(0, int(cx) - m), max(0, int(cy) - m),
           min(w, int(cx) + m + 1), min(h, int(cy) + m + 1))
    out = out.crop(box)

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    out.save(out_path)
    print(f"wrote {out_path} ({out.size[0]}x{out.size[1]})")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("base", help="the raw uisim panel PNG")
    ap.add_argument("--spec", required=True, help="the label spec JSON (466-design px)")
    ap.add_argument("--panel", type=int, required=True, choices=(466, 720))
    ap.add_argument("-o", required=True, help="output path")
    args = ap.parse_args()
    enclose(args.base, args.spec, args.panel, args.o)


if __name__ == "__main__":
    main()
