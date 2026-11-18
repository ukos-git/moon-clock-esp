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
a resources link.
