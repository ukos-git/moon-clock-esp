# Moon v1 — vendor data (gitignored)

`vendor/` is re-acquired by [get_vendor.py](get_vendor.py) (stdlib python3,
sha256- + size-pinned, the same pattern as
`assets/images/moon/get_assets.py` / `moon-kit/`):

```bash
python3 hardware/moonv1/parts/get_vendor.py --extract
```

| File | Contents | Source (fetched 2026-11-19) |
|---|---|---|
| `ESP32-S3-Touch-AMOLED-1.75.pdf` | S3 AMOLED 1.75 full schematic (AXP2101 PMIC + charge, PCF85063A RTC, TCA9554 expander, CO5300 QSPI panel, CST9217 touch, ES8311 codec; 8-pin header pinout) | [Waveshare resources](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents) |
| `ESP32-S3-Touch-AMOLED-1.75-3D.zip` | **STEP board model** (`ESP32-S3-Touch-AMOLED-1_75.stp` — real geometry for the housing S3 seat), DWG + PDF dimension drawings | same |
| `ESP32-S3-Touch-AMOLED-1.75-B-3D.zip` | `-B` variant DWG + PDF dimension drawings | same |
| `ESP32-P4-WIFI6-Touch-LCD-XC-Schematic.pdf` | P4 4C schematic (JD9365 MIPI-DSI, GT911, C6 over SDIO, ES8311) | [Waveshare resources](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-XC/Resources-And-Documents) |
| `ESP32-P4-WIFI6-TOUCH-LCD-3_4C.zip` | 3.4C STEP + DWG + PDF (family reference) | same |
| `ESP32-P4-WIFI6-TOUCH-LCD-4C-3D.zip` | **4C STEP board model** (`ESP32-P4-WIFI6-TOUCH-LCD-4C.stp` — real geometry for the housing P4 seat), DWG + PDF (incl. a second engineering-drawing revision) | same |

`--extract` unzips each archive next to itself (`<stem>/`). Extracted content
is untrusted vendor data: inspect before relying on it, and run any parsing
scripts against it with `python3 -I`.

Not yet acquired: the knob board's resource page 404'd on 2026-11-19 — the
knob housing seat is sized from the physical board + `docs/devices/knob.rst`
(35 mm dial, 360 panel) until the vendor data exists; re-check the
[product page](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) for
a resources link. **Resolved 2026-11-19:** the knob product page publishes an
outline-dimensions photo — the stock CNC case is **Ø66.00 × 22.00 mm**
(USB-C + 3.5 mm jack + button on the rim), and the user's unit lives in that
case, so the housing knob seat is a well for the stock case, not the bare
board.

## Mechanical facts extracted (2026-11-19, feed `hardware/housing/params.json`)

- **S3 466** (from `ESP32-S3-Touch-AMOLED-1.75-3D/*.pdf` + `.stp`): board OD
  **Ø48.96**, viewing area Ø44.16 / glass TP AA Ø43.76, bezel 2.60; display
  module stack 2.05 mm (side view "Total 2.05"), component stack ≤ 10.40;
  mounting holes **3× Ø2.00** ("3×M2.00-H3.5"), 34° marks, measured from the
  300 dpi rear view ±0.25 mm: (18.0, 0.0), (−13.8, +11.8), (−13.6, −11.2);
  USB-C on the bottom edge, 8-pin header on the left.
- **P4 4C** (from `ESP32-P4-WIFI6-TOUCH-LCD-4C-3D/` + official product
  dimension images): board OD **Ø126.00**, display Φ101.52 (glass 102.95,
  sub-module 106.69); total stack **15.00 mm**, display face protrudes
  **6.00 mm** above the board plane; mounting holes **4× Ø4.00** ("4×M4.00")
  on a 75×75 square: (±37.50, ±37.50); USB-C left, microSD top, battery
  header right. (The Ø86/13.10 drawing pages in the 4C zip are the *display
  sub-module*, not the main board — the DXF's outer Ø126 ring + the official
  Φ126.00 dimension images are the main board.)
- The vendor STEP/DXF sets are multi-view assembly files (glass module +
  board exploded); the dimension PDFs + official product images are the
  authoritative sources and are what `params.json` cites.
