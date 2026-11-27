The P4 port, in code
====================

.. brief:
   Reader: a firmware developer who works on both clock boards or ports to a new panel.
   They already know: C, ESP-IDF builds and Kconfig, the S3 display path.
   After this page they can: find every place the S3 and P4 builds differ, and say why each difference exists.
   Page type: explanation.

The P4 board (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC, "4C") is a second *panel* for the same product.
It has a 720×720 round IPS over MIPI-DSI, a different touch controller and, most important for the system's shape, **Wi-Fi on a different chip**.
This page walks through the code that carries the port: the board shim, the Kconfig split, the panel-conditional display path and the remote C6 radio.

The two chips side by side
--------------------------

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

The layering (:doc:`the architecture <architecture>`) is what makes a second panel a *port* rather than a *rewrite*.
The moon core does not know which board it runs on, the UI scales with the panel, and the board shim is the only place where "S3" and "P4" meet in an ``#if``.

The figures show the whole port as seams.
One app tree sits above the P4's two chips.
The ``esp_wifi`` call leaves ``wifi_manager.c`` unchanged and reaches the C6 through ``esp_wifi_remote`` and ``esp_hosted``.

.. graphviz:: ../_graphs/p4-port-seams.dot
   :alt: The P4 port as seams. The board-independent app tree sits above, the shim is the only #if, and the JD9365 panel and the remote C6 over SDIO sit below.
   :caption: The port as a black box. The shim is the only place "S3" and "P4" meet in an ``#if``; the build side is the 3-file defaults split with its own build dir and own lock (:doc:`the merge <../devices/index>`). The next two figures open the app tree and the two chips.
   :align: center

.. graphviz:: ../_graphs/p4-port-seams-app.dot
   :alt: The app tree. The moon core, ui, clock and wifi components sit above the components/board gate and do not depend on the board.
   :caption: The app tree. The core keeps one ``#if`` (``MOON_PX`` 400/650). The ui scales everything through ``UI_PX(x) = x·panel/466`` with baked font tiers (fonts / fonts_p4). The clock drops the PCF85063A (``CONFIG_CLOCK_USE_PCF85063A=n``, because the RTC is S3-side hardware) for a software minute tick plus an esp_timer countdown. ``wifi_manager.c`` does not change; the C6 reaches it as the host esp_wifi API.
   :align: center

.. graphviz:: ../_graphs/p4-port-seams-chips.dot
   :alt: The P4's two chips. The shim drives the JD9365 DSI panel, and wifi_manager feeds the remote C6 radio.
   :caption: The two chips. The panel is the JD9365 720×720 round IPS over MIPI-DSI DPI DMA, RGB888, one framebuffer with tear mode NONE, behind the XC BSP's ``CONFIG_BSP_LCD_TYPE_720_720_4_INCH``. The radio is esp_wifi_remote 0.14.5 + esp_hosted 1.4.7: RPC over SDIO, 4-bit at 40 MHz (CLK 18, CMD 19, D0–D3 14–17, slave reset GPIO 54) to the C6 slave, STA only, ``SOFTAP_SUPPORT=n`` on both sides. Three component-manager guard keys (``int``, default 0; a bool would hard-error) silence the USB-transport clauses of the esp_hosted 3.1.0 registry.
   :align: center

The board shim
--------------

``components/board`` is the seam.
It has one API with two implementations, selected by ``CONFIG_BOARD_P4``:

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

The shim encodes two facts.
First, the P4 BSP's display API returns a *panel handle*, while the S3 BSP's returns the panel directly.
The shim hides the difference, so ``moon_ui.c`` calls ``board_panel()`` and never knows which BSP it runs on.
Second, the P4 has **no IO expander**: no PCF85063A and no ALM pin.
That is where the deep-sleep difference comes from (:doc:`the clock and sleep machine <clock-sleep>`).
The P4's "deep sleep" is app-level and the crystal clock keeps running through it, so its alarm is a software one-shot, and a device with an alarm pending stays in light sleep.

The Kconfig split (three files)
-------------------------------

The build defaults are a **3-file split**, kept by hand (``idf.py save-defconfig`` does not generate them):

.. code-block:: text
   :caption: The sdkconfig defaults split

   sdkconfig.defaults            shared (LVGL, the moon core, the UI)
   sdkconfig.defaults.esp32s3    S3-only (board select, flash, PSRAM)
   sdkconfig.defaults.esp32p4    P4-only (board select, flash, PSRAM)

IDF appends the per-target file (``sdkconfig.defaults.<target>``) automatically when it exists, and the later file wins.
The files are kept by hand because ``idf.py save-defconfig`` writes only the shared file; run with the split, it clobbers the board-specific lines.
The root ``CMakeLists`` has a target/board ``FATAL_ERROR`` guard that catches a stray board line in the wrong file at build time.
The P4's board select (``CONFIG_BOARD_P4=y``) lives in ``sdkconfig.defaults.esp32p4``.

The P4 builds with its own build dir, its own dependency lock and its own live sdkconfig (``-B build/p4``, ``dependencies.lock.p4``, ``sdkconfig.p4``; all gitignored).
The two targets pull different BSPs, and the lock must not mix them.
The ``-D SDKCONFIG`` is required while the root ``sdkconfig`` pins ``IDF_TARGET=esp32s3``; after the first configure it becomes a cache entry, but keep passing it.

.. code-block:: bash
   :caption: The two builds (run one after the other, never in parallel)

   idf.py build                                  # S3 (the default target)
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build   # P4

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

Everything else derives from ``MOON_PX``: the disk radius, the image position and the strip count.
The port is a *scale factor*, not a second layout.
The P4 disk grew from 618 to 650 px after the M3/M4 milestones; the bake cost table in :doc:`performance <../verification/performance>` carries both sizes.

The UI scale factor
-------------------

The ``UI_PX`` macro in ``ui_px.h`` scales the LVGL screens with the panel.
The ~70 pixel literals in ``screens.c`` are written in UI units, and the S3 build stays byte-identical at 466 (checked against a stash control).
The P4 gets its own font subset (``fonts_p4/``: 148/74/50 px) with **identical ``lv_font_t`` symbols**, selected by CMake's ``FONT_DIR`` on ``CONFIG_BOARD_P4``.
The UI code therefore uses the same font names on both boards.
Six of the seven faces re-bake the S3's options verbatim.
The seventh (regular icons) adds the clock glyph U+F017 for this board's 7th ring entry; see :ref:`the type sketch <type-family-ui>`, priced by :ref:`the glyph ladder <glyph-ladder-perf>`.

The P4's display path: the strips are *smaller*, not larger
-----------------------------------------------------------

The S3's constraint is the 32 KiB SPI DMA cap (:doc:`the display pipeline <display>`).
The P4's constraint is different.
Its PSRAM is **X16 and XIP**: both cores fetch *instructions* from the same PSRAM that the strip reads come out of.
A long burst of strip reads from PSRAM can starve the DPI controller's FIFO ("can't fetch data from external memory fast enough, underrun"), so the P4 keeps its bursts short.
:doc:`/project/history` records the measurements that set the burst size.

The display strip pitch is a *pacing* constant for the invalidations of the atomic displays:

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

The strips only pace LVGL invalidations (the orbit's strip-by-strip display).
Every flush goes through the LVGL adapter's own buffers, and ``moon_panel_draw_wait`` (below) still wraps every write.

The async-DSI retry (``moon_panel_draw_wait``)
----------------------------------------------

The P4's DPI panel copies each strip with an **async DMA2D** and holds its draw semaphore until the copy lands.
A ``draw_bitmap`` call that follows too closely returns ``ESP_ERR_INVALID_STATE`` ("previous draw operation is not finished"), and the strip is **dropped**.
The S3's QSPI panel writes synchronously, so the first attempt always succeeds and the retry costs nothing there.
The wrapper retries until the panel accepts the write (the listing is shortened; :cfunc:`moon_panel_draw_wait` has the full diagnostics):

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

Callers must hold ``s_panel_mtx``.
On the P4 the retry absorbs the async-DMA throughput floor, which shows up in the atomic flushes' back-to-back bursts.
The rejects are the panel's *throughput* limit, not a bug.
The single framebuffer with tear mode ``NONE`` (:commit:`ee4e688e`) avoids the triple-buffer flip that showed a stale pose from a framebuffer's last visit.
The retries keep the remaining throughput floor from dropping a frame; :doc:`/project/history` has the reject counts measured on the bench.

The byte-order seam
-------------------

The S3's QSPI io sends 16-bit pixels MSB-first (the *swapped* form).
The P4's MIPI-DSI wants LVGL-native order: its adapter bridge does not swap, because ``lv_draw_sw_rgb565_swap`` fires only for the "OTHER" panel interface.
Both facts are handled at the panel boundary, where the right bridge for each board pre-swaps LVGL's strips.
The bridges are the port's entire colour-order concern (:doc:`the display pipeline <display>`).

The remote C6 Wi-Fi
-------------------

The P4 has **no on-chip radio**.
Wi-Fi runs on a separate **ESP32-C6** over **SDIO**, driven by ``esp_hosted`` (1.4.7) and ``esp_wifi_remote`` (0.14.5).
This has three consequences:

- The Wi-Fi API is the same (``esp_wifi_*``). The ``esp_wifi_remote`` shim presents the C6's radio through the host API, so ``wifi_manager.c`` does not depend on the board: it calls ``esp_wifi_init``/``esp_wifi_connect`` and does not know the radio is a different chip on an SDIO bus.
- The C6 link was verified over SDIO (association, SNTP and scan) at the M2 milestone, before the moon port (M3) and the UI upscale (M4) started.
- The scan (settings screen) and the on-demand sync (:doc:`the clock and sleep machine <clock-sleep>`) run the same background-task shape on both boards. With the remote radio, the SDIO bus is the bottleneck that the background task hides.

The milestones (M0–M4)
----------------------

Milestones gated the port, and each was verified on the device before the next started (the on-device numbers are in :doc:`performance <../verification/performance>`):

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

The on-device acceptance runs of M3 and M4 are in :doc:`/project/history`; the full binary chronology is in :doc:`the binary-size chronology <../verification/binary>`.

What the port does not touch
----------------------------

- **The moon core.** There is no ``#if defined(CONFIG_BOARD_P4)`` in ``components/moon``. The port lives entirely in ``moon_ui.c`` (the display path), ``board_display.c`` (the seam) and the build files.
- **The atomic invariant.** The P4 obeys the same rule: one atomic whole-panel display, with one sanctioned exception, the fade's hardware brightness ramp. The compute watermark runs in 13-row strips on the P4, but the partial bake is never displayed (:doc:`the compute progress <compute-progress>`).
- **The panel-io watchdog.** The same restart guard based on ``s_panel_mtx`` runs on the P4. Its DSI bus has its own stall modes, but the bounded-hold and watchdog discipline does not depend on the board.

The P4 is the same *architecture* (portable core, panel-scaled UI, one seam) proven on a second panel, a second bus and a second, remote radio.
That the port is a handful of ``#if`` branches and one extra build dir validates the layering (:doc:`the architecture <architecture>`).
It is also what makes the performance comparison of the two boards meaningful: the same maths and the same invariants on two chips.

Where to go next
----------------

* :doc:`Performance <../verification/performance>`: the two boards compared on the same maths.
* :doc:`The display pipeline <display>`: the S3's DMA cap, the drain witness and the bridges.
* :doc:`The clock and sleep machine <clock-sleep>`: why the P4 sleeps at app level.
