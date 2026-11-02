Chapter 3 — The P4 4C port
===========================

The second target is the **Waveshare ESP32-P4-WIFI6-Touch-LCD-XC**
("4C"): a 720×720 round IPS panel driven by a **JD9365** MIPI-DSI
controller, **GT911** touch, a remote **C6 Wi-Fi chip over SDIO**
(ESP-Hosted), 32 MB flash, and a 400 MHz quad-core RISC-V P4 core.
Porting to it was a full second board, not a configuration toggle —
the panel is a different *technology* (MIPI-DSI vs QSPI) with a
different controller, a different pixel format, and a different DMA
story. This chapter covers what the port took and what the
measurements showed.

Why a second board
------------------

The S3 AMOLED is the product's face, but its panel is 466×466 with a
25-row flush-strip ceiling (Chapter 2). The P4's 720×720 panel makes
the same moon larger and, with the RISC-V cores, gives the bake real
headroom. The design goal for the port was:

- **The portable C core is shared verbatim.** No changes to
  ``components/moon/core`` — the same translation units compile for
  both targets. Board-specific differences live in the UI layer
  (panel scale, fonts, the DSI flush path) and the board shim.
- **One Kconfig board choice, three config files.**
  ``sdkconfig.defaults`` (shared) plus
  ``sdkconfig.defaults.esp32s3`` / ``sdkconfig.defaults.esp32p4``
  (per-target, auto-appended when present; later file wins). The P4
  build sets ``CONFIG_BOARD_P4=y`` from the per-target file; the root
  ``CMakeLists`` cross-checks the target against the board choice and
  fails the build with a ``FATAL_ERROR`` if they disagree.

Panel differences that matter to the pipeline
---------------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * - Property
     - S3 (CO5300, QSPI)
     - P4 (JD9365, MIPI-DSI)
   * - Resolution
     - 466×466
     - 720×720
   * - Color
     - RGB565
     - RGB888
   * - Flush path
     - QSPI strip writes (25-row internal-RAM strips, 32 KiB DMA cap)
     - DSI async DMA (the spin's ~3,900 "previous draw not finished" rejects are the async-DMA throughput floor, absorbed by a retry — Chapter 13)
   * - Moon scale
     - MOON_PX 400, MOON_R 200
     - MOON_PX 618, MOON_R 309 (2026-11-01 WIP: 650)
   * - Teardown mode
     - n/a
     - 1-FB ``LV_COLOR_FORMAT`` single-buffer with ``LV_DISPLAY_RENDER_MODE_NONE`` (the 3-FB partial-mode scanout flip showed the spin pose from an old frame's last visit — Chapter 13)

The P4's spin strips are 103 rows deep (the DPI FIFO underruns at 206
on X16+XIP PSRAM), and the gray→RGB conversion is an identity swap
(888 panel, no 5-6-5 quantization) — the art pass's warm LUT still
applies because it is a *color* choice, not a quantization artifact.

What the port took (M0–M4)
--------------------------

The port landed as a milestone sequence (the letters below are the
milestone labels used in the project's tracking):

- **M0 — bring-up**: board shim + board Kconfig + the 3-file
  sdkconfig split + ``partitions_p4.csv`` (9 MB ``factory``; the
  32 MB chip's header still says 16 MB, so esptool's size-mismatch
  warning is expected) + ``EXCLUDE_COMPONENTS`` per target + a
  separate gitignored ``dependencies.lock.p4`` (the P4's C6/esp_hosted
  dependency set differs from the S3's).
- **M1 — plumbing**: rev v1.3 board, PSRAM at 200 MHz, JD9365 + GT911
  + 32 MB all verified.
- **M2 — Wi-Fi**: the C6 over SDIO (esp_hosted 1.4.7,
  esp_wifi_remote 0.14.5): association + SNTP + AP scan verified.
- **M3 — moon @ 720**: board-conditional ``moon_ui.c`` (no new
  Kconfig): MOON_PX/MOON_R scaled, spin strips 103 rows, the
  ``--panel 720`` simulator path.
- **M4 — UI upscale**: ``ui_px.h`` scales ~70 literals in
  ``screens.c`` (S3 stays byte-identical at 466 via the
  ``--panel``-aware build); the P4 fonts (148/74/50 px) carry
  identical ``lv_font_t`` symbols selected by CMake on
  ``CONFIG_BOARD_P4``; the keep-mask (burn-in spin's UI mask) was
  re-derived on the upscaled arc.

M5 (clock degradation + manual time entry on P4) and M6 (docs) are the
open items as of this book.

Measured results on the P4
--------------------------

On-device captures (full run, 0 WDT / 0 panic):

- Boot sweep: 336/336 frames @ 618 px completed.
- Final bake: 599 ms at 618 px (vs ~959 ms at 650 px after the 2026-11-01
  WIP landed).
- Dusk: ~43 s for the full constant-projection spin.
- The async-DMA rejects during the spin are absorbed — 0 dropped
  strips after the ``moon_panel_draw_wait`` retry.

The binary is 1,575,152 B (0x1808f0) after the 2026-11-01 TZ/settings
work landed — 16% of the 9 MB factory partition. The S3 binary at the
same point was 1,459,744 B (0x164620), 31% of its 2 MB partition.
