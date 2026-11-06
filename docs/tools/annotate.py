#!/usr/bin/env python3
"""Annotate a uisim screen shot: numbered callout badges + a legend strip.

The spec JSON is authored ONCE in 466-design px (the S3's native size;
the whole UI is UI_PX(x) = x*panel/466), so the tool scales every
coordinate by panel/466 for the target panel (466 or 720).

Spec shape (a bare list, or {"labels": [...]}):
    [
      {"id": 1, "text": "WiFi entry", "x": 112, "y": 303, "dir": "w"},
      ...
    ]
``dir`` is the direction the BADGE sits from the element point (n/s/e/w);
a short leader line connects the badge to the point. The panel image
itself gets only the small badges + leaders; the text lives in the
legend strip on the right, so the UI stays legible.

Usage:
    annotate.py BASE.png --spec docs/ui/labels/<screen>.json --panel 466
                 -o docs/img/screens/annot/<screen>_s3.png
"""

import argparse
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

DESIGN = 466          # the spec coordinate space
BADGE_R = 9           # badge radius (design px)
LEADER = 14           # point -> badge-edge length (design px)
BADGE_GAP = 9         # extra offset so the badge floats off the point
STRIP_GAP = 14        # panel -> strip (design px)
ROW_H = 24            # legend row height (design px)
STRIP_PAD = 12        # strip inner padding (design px)
STRIP_BADGE_R = 8     # legend badge radius (design px)


def load_font(size):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        os.path.expanduser("~/.local/share/fonts/DejaVuSans.ttf"),
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                pass
    # PIL >= 10.1: a scalable default font
    return ImageFont.load_default(size=size)


def badge(draw, cx, cy, r, num, font, fill=(242, 243, 245), ink=(16, 17, 21)):
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    s = str(num)
    w = font.getlength(s)
    draw.text((cx - w / 2, cy - r * 0.78), s, font=font, fill=ink)


def annotate(base_path, spec, panel, out_path):
    scale = panel / DESIGN
    img = Image.open(base_path).convert("RGB")
    if img.size != (panel, panel):
        raise SystemExit(f"{base_path}: expected {panel}x{panel}, got {img.size}")

    labels = spec["labels"] if isinstance(spec, dict) else spec
    if not labels:
        raise SystemExit("empty label spec")

    big = load_font(int(round(13 * scale)))   # badge numbers
    row = load_font(int(round(15 * scale)))   # legend text

    # --- measure the strip before drawing anything
    strip_w = int(round((STRIP_PAD * 2 + STRIP_BADGE_R * 2 + 10
                         + max(row.getlength(l["text"]) for l in labels)
                         + STRIP_PAD) * scale))
    out = Image.new("RGB", (panel + int(round(STRIP_GAP * scale)) + strip_w, panel),
                    (16, 16, 16))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)

    strip_x = panel + int(round(STRIP_GAP * scale))
    # strip background slightly lighter than the panel so the boundary reads
    d.rectangle([strip_x, 0, strip_x + strip_w, panel], fill=(24, 24, 26))

    for l in labels:
        x = l["x"] * scale
        y = l["y"] * scale
        dx, dy = {"n": (0, -1), "s": (0, 1), "w": (-1, 0), "e": (1, 0)}[l["dir"]]
        # badge centre: off the point by leader + radius + gap along dir
        bx = x + dx * (LEADER + BADGE_R + BADGE_GAP) * scale
        by = y + dy * (LEADER + BADGE_R + BADGE_GAP) * scale
        # keep the badge fully on the panel
        r = BADGE_R * scale
        if not (r <= bx <= panel - r and r <= by <= panel - r):
            raise SystemExit(
                f"label {l['id']} ({l['text']}): badge off-panel at ({bx:.0f},{by:.0f})"
            )
        d.line([x, y, bx - dx * r, by - dy * r], fill=(242, 243, 245),
               width=max(1, int(round(2 * scale))))
        badge(d, bx, by, r, l["id"], big)

    # legend strip: number badge + text per row, top-aligned
    ty = STRIP_PAD * scale
    for l in labels:
        br = STRIP_BADGE_R * scale
        bx = strip_x + STRIP_PAD * scale + br
        by = ty + ROW_H * scale / 2
        badge(d, bx, by, br, l["id"], row, fill=(242, 243, 245), ink=(16, 17, 21))
        d.text((bx + br + 8 * scale, by - 7.5 * scale), l["text"], font=row,
               fill=(242, 243, 245))
        ty += ROW_H * scale

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    out.save(out_path)
    print(f"wrote {out_path} ({l_out_size(out)})")


def l_out_size(img):
    return f"{img.size[0]}x{img.size[1]}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("base", help="the raw uisim panel PNG")
    ap.add_argument("--spec", required=True, help="the label spec JSON (466-design px)")
    ap.add_argument("--panel", type=int, required=True, choices=(466, 720))
    ap.add_argument("-o", required=True, help="output path")
    args = ap.parse_args()
    with open(args.spec) as f:
        spec = json.load(f)
    annotate(args.base, spec, args.panel, args.o)


if __name__ == "__main__":
    main()
