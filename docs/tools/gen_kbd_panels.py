"""Render the round-keyboard lab's on-hardware panels and montage them.

Drives headless Chromium (playwright) over
docs/_static/keyboard/round-keyboard-lab.html, screenshots the nine 1:1
(466x466) panel SVGs in the lab's own dark AMOLED theme, and lays them
out as a 3x3 montage with rank + verdict labels into
docs/_static/figs/kbd_panels.png — the clickable lab's static half for
the keyboard-lab page (docs/keyboard-lab.rst).

The labels (rank, verdict chip, name) are read from the rendered DOM, so the
figure cannot drift from the lab page it depicts. The four off-hardware
methods (voice, camera/OCR, QR/NFC/BLE handoff, T9 decode) are text cards in
the lab and are documented in the chapter's list-table instead.

Run:  python3 docs/tools/gen_kbd_panels.py
(needs system python with playwright + PIL; the chromium binary comes from
the ms-playwright cache)
"""
from __future__ import annotations

import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.abspath(os.path.join(HERE, "..", "_static", "keyboard", "round-keyboard-lab.html"))
OUT = os.path.abspath(os.path.join(HERE, "..", "_static", "figs", "kbd_panels.png"))
WORK = "/tmp/kbd_panels"

# The lab's dark-theme accent colours, for the verdict chips.
CHIP_COL = {
    "keep": "#57b183",
    "ship": "#7b9bff",
    "hold": "#d3a049",
    "rej": "#d97f7f",
    "exp": "#939aa6",
    "v2": "#c9a15f",
    "infeas": "#d97f7f",
}
BG = "#0d0f13"
INK = "#e9ebef"
MUT = "#939aa6"


def capture():
    from playwright.sync_api import sync_playwright

    os.makedirs(WORK, exist_ok=True)
    meta = {}
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 1680, "height": 1200}, device_scale_factor=2)
        pg.goto("file://" + LAB, wait_until="load")
        # The book shows the AMOLED: force the lab's dark theme (the page's
        # prefers-color-scheme hook would follow the host's setting instead).
        pg.evaluate("document.documentElement.setAttribute('data-theme','dark')")
        pg.wait_for_selector(".card .pdisk", timeout=15000)
        pg.wait_for_timeout(800)  # font swap + one rAF tick of the shared loop
        cards = pg.query_selector_all("#gallery > .card")
        assert len(cards) == 9, len(cards)
        for i, card in enumerate(cards, 1):
            rank = card.query_selector(".rank").inner_text().strip()
            chip_el = card.query_selector(".chip")
            chip = chip_el.inner_text().strip()
            chip_cls = chip_el.get_attribute("class").split()[-1]
            name = card.query_selector("h3").inner_text().strip()
            png = card.query_selector(".pdisk").screenshot(type="png")
            fn = os.path.join(WORK, "panel_%02d.png" % i)
            with open(fn, "wb") as f:
                f.write(png)
            meta[rank] = {"png": fn, "chip": chip, "cls": chip_cls, "name": name}
        b.close()
    with open(os.path.join(WORK, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("captured %d panels" % len(meta))
    return meta


def font(path, size):
    return ImageFont.truetype(path, size)


def main():
    meta = capture()
    dejavu = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
    f_rank = font(dejavu + "-Bold.ttf", 26)
    f_chip = font(dejavu + "-Bold.ttf", 22)
    f_name = font(dejavu + ".ttf", 20)

    panels = [meta["#%d" % r] for r in range(1, 10)]
    pw = 640  # the panel square, in the montage
    pad, gut = 26, 20
    lab1, lab2 = 34, 30
    cell_w = pw + 2 * pad
    cell_h = pad + lab1 + lab2 + 10 + pw + pad
    cols = rows = 3
    W = cols * cell_w + (cols + 1) * gut
    H = rows * cell_h + (rows + 1) * gut
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    for i, m in enumerate(panels):
        r, c = divmod(i, cols)
        x = gut + c * (cell_w + gut)
        y = gut + r * (cell_h + gut)
        im = Image.open(m["png"]).convert("RGB")
        im = im.resize((pw, pw), Image.LANCZOS)
        img.paste(im, (x + pad, y + pad + lab1 + lab2 + 10))
        col = CHIP_COL.get(m["cls"], MUT)
        # rank + verdict on one line, name below
        rank_txt = "#" + str(i + 1)
        d.text((x + pad, y + pad), rank_txt, font=f_rank, fill=INK)
        w_rank = d.textlength(rank_txt, font=f_rank)
        d.text((x + pad + w_rank + 12, y + pad + 3), m["chip"], font=f_chip, fill=col)
        d.text((x + pad, y + pad + lab1), m["name"], font=f_name, fill=MUT)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("wrote", OUT, img.size)


if __name__ == "__main__":
    main()
