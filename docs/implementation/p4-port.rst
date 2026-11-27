The P4 port, in code
====================

The P4 board (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC, "4C") is a second
*panel* for the same product: a 720×720 round IPS over MIPI-DSI, a
different touch controller, and — the part that changes the system's
shape — **Wi-Fi on a different chip**. This page is the port, in
the code that does it: the board shim, the Kconfig split, the
panel-conditional display path, and the remote-C6 radio.

The two silicons, side by side
------------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 34 44

   * -
     - S3 (1.75" AMOLED)
     - P4 (720 round IPS)
   * - Panel
     - CO5300, 466×466, QSPI (SPI2_HOST), RGB565
     - JD9365, 720×720, **MIPI-DSI**, RGB888
   * - Touch
     - CST9217 (I2C)
     - GT911 (I2C)
   * - IMU
     - QMI8658C (I2C)
     - (same class; board plumbing)
   * - RTC
     - PCF85063A behind TCA9554 (ALM wake)
     - none (app-level deep sleep; software one-shot)
   * - Wi-Fi
     - on-chip (ESP32-S3)
     - **remote ESP32-C6 over SDIO** (``esp_hosted``)
   * - Flash
     - 16 MB (factory 2,048 K)
     - 32 MB (factory 9,216 K; ``partitions_p4.csv``)
   * - PSRAM
     - 8 MB octal (not SPI-DMA-capable)
     - X16/XIP (instruction fetch *and* data share it)
   * - Moon disk
     - ``MOON_PX`` 400, ``MOON_R`` 200
     - ``MOON_PX`` 650, ``MOON_R`` 325 (upscaled from 618)

The architecture (:doc:`the architecture <architecture>`) is what makes "a second panel" a
*port* rather than a *rewrite*: the moon core is board-blind, the UI
is panel-scaled, and the board shim is the only place "S3" and "P4"
coexist as an ``#if``.

The whole port as seams in one picture — one app tree above, the
P4's two chips below, the ``esp_wifi`` call leaving
``wifi_manager.c`` unchanged and arriving at the C6 through
``esp_wifi_remote`` + ``esp_hosted``:

.. graphviz:: ../_graphs/p4-port-seams.dot
   :alt: The P4 port as seams — board-blind app tree above, the shim as the only #if, the JD9365 panel and the remote C6 over SDIO below, the 3-file defaults split beside it
   :caption: The black box: the whole port reduces to one gate — the shim is the ONLY place "S3" and "P4" coexist as an ``#if``; the build side is the 3-file defaults split with its own build dir and own lock (:doc:`the merge <../devices/index>`). The two details open the app tree and the two chips.
   :align: center

.. graphviz:: ../_graphs/p4-port-seams-app.dot
   :alt: The app tree — moon core, ui, clock, wifi all board-blind above the components/board gate
   :caption: The core keeps exactly one ``#if`` (``MOON_PX`` 400/650); the ui scales everything through ``UI_PX(x) = x·panel/466`` with baked font tiers (fonts / fonts_p4); the clock drops the PCF85063A (``CONFIG_CLOCK_USE_PCF85063A=n`` — the RTC is S3-side hardware) onto a software minute tick + esp_timer countdown; ``wifi_manager.c`` never changes — it just gets the C6 handed to it as the host esp_wifi API.
   :align: center

.. graphviz:: ../_graphs/p4-port-seams-chips.dot
   :alt: The P4's two chips — the JD9365 DSI panel driven from the shim, the remote C6 radio fed by wifi_manager
   :caption: The panel is the JD9365 720×720 round IPS over MIPI-DSI DPI DMA, RGB888, ONE framebuffer + NONE tear mode (the 3-FB flip showed stale poses — :commit:`ee4e688e`), the XC BSP behind ``CONFIG_BSP_LCD_TYPE_720_720_4_INCH``. The radio is esp_wifi_remote 0.14.5 + esp_hosted 1.4.7 — RPC over SDIO, 4-bit at 40 MHz (CLK 18, CMD 19, D0–D3 14–17, slave reset GPIO 54) to the C6 slave, STA only, ``SOFTAP_SUPPORT=n`` on both sides; the three component-manager guard keys (``int``, default 0 — a bool would hard-error) silence the esp_hosted 3.1.0 registry's USB-transport clauses.
   :align: center

The board shim
--------------

``components/board`` is the seam. One API, two implementations
selected by ``CONFIG_BOARD_P4``:

.. code-block:: c
   :caption: components/board/board_display.c (the whole display seam)

   #include "board_display.h"

   esp_lcd_panel_handle_t board_panel(void) {
   #if defined(CONFIG_BOARD_P4)
       return bsp_display_get_panel_handle();
   #else
       return bsp_display_get_panel();
   #endif
   }

   esp_io_expander_handle_t board_io_expander_init(void) {
       /* The P4 board has no IO expander (no PCF85063A interrupt line). */
   #if defined(CONFIG_BOARD_P4)
       return NULL;
   #else
       return bsp_io_expander_init();
   #endif
   }

Two facts the shim encodes. First, the P4 BSP's display API returns
a *panel handle* while the S3 BSP's returns the panel directly — the
shim normalizes that so ``moon_ui.c`` calls ``board_panel()`` and
never knows which BSP it is in. Second, the P4 has **no IO expander**:
no PCF85063A, no ALM pin, which is where the deep-sleep asymmetry
(:doc:`the clock and sleep machine <clock-sleep>`) comes from — the P4's "deep sleep" is app-level (the
crystal clock runs through it), so its alarm is a software one-shot
and an alarm-pending device stays in light sleep.

The Kconfig split (three files)
-------------------------------

The build defaults are a **3-file split**, hand-curated (not
``idf.py save-defconfig``-generated):

.. code-block:: text
   :caption: The sdkconfig defaults split

   sdkconfig.defaults            shared (LVGL, the moon core, the UI)
   sdkconfig.defaults.esp32s3    S3-only (board select, flash, PSRAM)
   sdkconfig.defaults.esp32p4    P4-only (board select, flash, PSRAM)

IDF appends the per-target file (``sdkconfig.defaults.<target>``)
automatically when it exists; the later file wins. The split exists
because ``idf.py save-defconfig`` writes *only the shared file* —
run it with the split and it clobbers the board-specific lines. The
root ``CMakeLists`` has a target/board ``FATAL_ERROR`` guard that
catches a stray board line in the wrong file at build time. The P4's
board select lives in ``sdkconfig.defaults.esp32p4``
(``CONFIG_BOARD_P4=y``); the P4 builds with its **own build dir and
its own dependency lock** (``-B build/p4``,
``dependencies.lock.p4`` — both gitignored), because the two targets
pull different BSPs and the lock must not mix them.

.. code-block:: bash
   :caption: The two builds (sequential — never parallel)

   idf.py build                                  # S3 (the default target)
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 build   # P4

The moon constants (one ``#if``)
--------------------------------

The moon disk scales with the panel in one place:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the panel-scaled disk)

   #if defined(CONFIG_BOARD_P4)
   #define MOON_PX 650
   #else
   #define MOON_PX 400
   #endif
   #define MOON_IMG_POS ((BSP_LCD_H_RES - MOON_PX) / 2)
   ...
   #define MOON_R ((float)MOON_PX / 2.0f)

Everything below derives from ``MOON_PX`` — the disk radius, the
image position, the strip count — so the port
is a *scale factor*, not a second layout. (The P4 disk grew 618 → 650
after the M3/M4 work; the bake cost table in :doc:`performance <../verification/performance>` carries both.)

The UI scale factor
-------------------

The LVGL screens are panel-scaled by ``ui_px.h``'s ``UI_PX`` macro —
the ~70 pixel literals in ``screens.c`` are written in UI units and
scale with the panel (S3 byte-identical at 466, via a stash control).
The P4 gets its own font subset (``fonts_p4/``: 148/74/50 px) with
**identical ``lv_font_t`` symbols** (``CMake``'s ``FONT_DIR`` on
``CONFIG_BOARD_P4``) so the UI code references the same font names on
both boards — six of the seven faces re-bake the S3's Opts verbatim,
the seventh (regular icons) adds the clock U+F017 for this board's
7th ring entry: :ref:`the type sketch <type-family-ui>`, priced by
:ref:`the glyph ladder <glyph-ladder-perf>`.

The P4's display path: the strips are *smaller*, not larger
-----------------------------------------------------------

The S3's constraint was the 32 KiB SPI DMA cap (:doc:`the display pipeline <display>`). The P4's
constraint is different: the panel and its PSRAM are **X16 and XIP** —
both cores fetch *instructions* from the same PSRAM the strip reads
come out of. Measured 2026-10-31, one 206-row (255 KB) direct strip
write occasionally starved the DPI controller's FIFO ("can't fetch
data from external memory fast enough, underrun" — one underrun per
~28 s of settled spin). The fix halved the burst: **103 rows** (6
strips, 127 KB); for the 650 px disk the direct-write era settled at
**65 rows** (10 strips, 84.5 KB) — an even smaller burst, the same
proven-safe class. That era's write path retired with the spin
subsystem (:commit:`79937e37`, :doc:`the animation pipelines <animations>`);
what survives is its strip pitch, demoted to a *pacing* constant for
the atomic displays' invalidations:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the display strip pitch)

   #if defined(CONFIG_BOARD_P4)
   #define MOON_STRIP_ROWS 130
   #else
   #define MOON_STRIP_ROWS 25
   #endif
   #define MOON_NSTRIPS (MOON_PX / MOON_STRIP_ROWS)
   #if (MOON_PX % MOON_STRIP_ROWS) != 0
   #error "MOON_PX must be a multiple of MOON_STRIP_ROWS"
   #endif

The strips now only *pace LVGL invalidations* (the orbit's
strip-by-strip display); every flush goes through the LVGL adapter's
own buffers, and ``moon_panel_draw_wait`` (below) still rides every
write.

The async-DSI retry (``moon_panel_draw_wait``)
----------------------------------------------

The P4's DPI panel copies each strip with an **async DMA2D** (its draw
semaphore is held until the copy lands). Back-to-back
``draw_bitmap`` calls return ``ESP_ERR_INVALID_STATE`` ("previous draw
operation is not finished") and the strip is **dropped**. The S3's QSPI
panel writes synchronously, so the first attempt always succeeds and
the retry costs nothing there. The wrapper retries until the panel
accepts the write:

.. code-block:: c
   :caption: components/ui/moon_ui.c (moon_panel_draw_wait)

   static esp_err_t moon_panel_draw_wait(esp_lcd_panel_t *panel, int x_start, int y_start,
                                         int x_end, int y_end, const void *color_data, bool strict) {
       if (!strict && panel_hold_live())
           return ESP_OK; /* dropped: the sync storm owns the bus (the hold) */
       for (int attempt = 0; attempt < 100; attempt++) {
           esp_err_t r = s_orig_draw(panel, x_start, y_start, x_end, y_end, color_data);
           if (r == ESP_OK) {
               snprintf(s_last_accept_desc, ..., "%s y%d..%d", pcTaskGetName(NULL), ...);
               s_last_accept_tick = lv_tick_get();
               return ESP_OK;
           }
           ... /* rate-limited attempt-0 anomaly log, see the display page */
           vTaskDelay(1); /* a full tick: pdMS_TO_TICKS(1) = 0 at HZ=100 */
       }
       /* ~100 attempts, but each rejected attempt costs the io onFly
        * poll (~10 ms) plus the tick: ~2 s per strip before the give-up
        * line names the last ACCEPTED write (task + strip + age). */
       ESP_LOGW(TAG, "panel draw not accepted: task %s (y %d..%d); last accepted: %s @ %lu ms ago (the culprit)", ...);
       return ESP_ERR_TIMEOUT;
   }

Callers must hold ``s_panel_mtx``. On the P4 this retry absorbs the
async-DMA throughput floor — measured **~3,900 rejects, 0 dropped
strips** over a long session in the spin era (the rejects were
spin-only — 0 during the sweep and dusk; today the same retry rides
the atomic flushes' back-to-back bursts). They are the
panel's *throughput* limit, not a bug: the 1-FB + ``NONE`` tear mode
(fix :commit:`ee4e688e`) removed the triple-buffer flip that showed a stale
pose from a framebuffer's last visit, and the retries are what keeps
the remaining throughput floor from dropping a frame.

The byte-order seam
-------------------

The S3's QSPI io sends 16-bit pixels MSB-first (the *swapped* form);
the P4's MIPI-DSI wants LVGL-native order (its adapter bridge does not
swap — ``lv_draw_sw_rgb565_swap`` fires only for the "OTHER" panel
interface). Both facts are handled at the panel boundary: LVGL's
strips are pre-swapped by the right bridge per board, and the spin-era
direct write carried its own identity/byte-swap inline — retired with
the spin subsystem (:doc:`the display pipeline <display>`), leaving
the bridges as the port's entire color-order concern.

The remote C6 Wi-Fi
-------------------

The P4 has **no on-chip radio**: Wi-Fi runs on a separate
**ESP32-C6** over **SDIO**, driven by ``esp_hosted`` (1.4.7) +
``esp_wifi_remote`` (0.14.5). The implications:

- The Wi-Fi API is the *same* (``esp_wifi_*``) — the
  ``esp_wifi_remote`` shim presents the C6's radio through the host
  API, so ``wifi_manager.c`` is **board-blind**: it calls
  ``esp_wifi_init``/``esp_wifi_connect`` and does not know the radio
  is a different chip on an SDIO bus.
- The bring-up is the M2 milestone: C6 over SDIO verified (association
  + SNTP + scan) before the moon port (M3) and the UI upscale (M4)
  started.
- The scan (settings screen) and the on-demand sync (:doc:`the clock and sleep machine <clock-sleep>`) run
  the same background-task shape on both boards — the *remote* radio
  just means the SDIO bus is the bottleneck the background task hides.

The milestones (M0–M4)
----------------------

The port was gated by milestones, each verified on the device before
the next started (the full on-device numbers in :doc:`performance <../verification/performance>`):

.. list-table::
   :header-rows: 1
   :widths: 8 30 62

   * - M
     - Name
     - Verified
   * - M0
     - Board bring-up
     - rev v1.3 read; PSRAM 200 MHz; JD9365 + GT911 + 32 MB OK
   * - M1
     - Board plumbing
     - the shim + Kconfig + 3-file sdkconfig split +
       ``partitions_p4.csv`` + ``EXCLUDE_COMPONENTS`` + per-target
       lock
   * - M2
     - App Wi-Fi
     - C6 over SDIO (``esp_hosted``) assoc + SNTP + scan
   * - M3
     - Moon @ 720
     - ``MOON_PX``/``MOON_R`` board-conditional (no new Kconfig);
       spin strips 103 rows (DPI-FIFO underrun at 206 = X16+XIP
       PSRAM); identity byte-swap; ``uisim --panel 720``
   * - M4
     - UI upscale
     - ``ui_px.h`` UI_PX panel-scaled ``screens.c`` (~70 literals,
       S3 byte-identical @ 466 via a stash control); ``fonts_p4/``
       148/74/50 with identical ``lv_font_t`` symbols; keep-mask
       315/357 finalized on the upscaled arc; ``uisim --wheel``
       panel-scaled

The on-device acceptance (M3+M4, the 650 px disk): the boot sweep ran
**336/336** frames with 0 WDT/panic; the sweep's final full-res bake
was **599 ms @ 618 px / 959 ms @ 650 px**; the dusk ran ~43 s; the
binary was 1,575,152 B at the TZ-land time (the full binary
chronology in :doc:`the binary-size chronology <../verification/binary>`).
Re-verified 2026-11-07 with the quadrupled sweep: **1344/1344**
frames, 0 WDT/panic, ~40.5 min wall, bake median 1543 ms @ 650 px
(the 2048-albedo era's 1138–2083 ms spread).

What the port did *not* touch
------------------------------

- **The moon core** — zero ``#if defined(CONFIG_BOARD_P4)`` in
  ``components/moon``. The port is entirely in ``moon_ui.c`` (the
  display path), ``board_display.c`` (the seam), and the build files.
- **The atomic invariant** — the P4 obeys the same rule (one atomic
  whole-panel display; the ONE sanctioned exception: the fade's
  hardware brightness ramp). The bake reveal's second exception —
  whose band-atomic states ran the P4's 13-row bands after the
  2026-11-15 tuning (26 pre-tuning) — was retired :commit:`ee4e3efd` with
  the compute-progress change (:doc:`the compute progress
  <compute-progress>`; the compute watermark runs the same 13-row
  strips, but the partial bake is never displayed). In the spin era
  the P4's two extra exceptions (the spin's direct write and the TF
  replay's) went per-strip (65 rows) rather than the S3's single
  window, but were still ``s_panel_mtx``-held, per-frame atomic
  units — all retired :commit:`79937e37` with the spin.
- **The panel-io watchdog** — the same ``s_panel_mtx``-based restart
  guard runs on the P4 (its DSI bus has its own stall modes; the
  bounded-hold + watchdog discipline is board-blind).

The P4 is not "the same product, bigger screen." It is the same
*architecture* — portable core, panel-scaled UI, one seam — proven on
a second panel, a second bus, and a second (remote) radio. That the
port is a handful of ``#if`` branches and one new build dir is the validation
of the layering (:doc:`the architecture <architecture>`), and it is what makes the performance
comparison of the two boards (:doc:`performance <../verification/performance>`) meaningful: same math,
same invariants, two silicons.
