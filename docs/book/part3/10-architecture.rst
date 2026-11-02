Chapter 10 — System architecture
================================

Part II described what the renderer computes. This part describes where
each computation runs, who owns which buffer, and which rules keep a
240 MHz dual-core microcontroller with a 32 KiB DMA budget from tearing
an image in half on screen. The architecture has three properties
worth stating up front, because every design decision below serves
them:

1. **One image buffer, one atomic flush.** Every moon-pipeline frame
   reaches the panel as a single whole-panel display under one
   critical section. There are exactly two sanctioned exceptions, and
   both are documented here.
2. **The physics is portable.** The moon core — ephemeris, bake,
   relief, rotation, the LUT — is a dependency-free C library that
   compiles unchanged into the device *and* the host simulator.
   Everything device-specific sits above it: the board shims, the LVGL
   UI, the panel's direct-write path.
3. **The display is the sleep surface.** The same pixels the clock
   shows while awake are what the light-sleep screen shows; the
   animations (Chapter 14) are not decorative overlays — they are the
   transition itself.

The layer stack
---------------

::

  +---------------------------------------------------------------+
  | main.cpp — app_main: BSP bring-up, wifi boot, clock init,     |
  |   the boot sweep host, deep-sleep entry                        |
  +---------------------------------------------------------------+
  | components/clock — clock.c (minute-driven display, 90-deg     |
  |   roll), clock_sntp (NTP), clock_idle (activity/sleep state   |
  |   machine), clock_alarm_scheduler (PCF85063A alarm rules),    |
  |   clock_tz (53 timezone offsets), settings_store (SPIFFS)     |
  +---------------------------------------------------------------+
  | components/wifi — wifi_manager (connect, deinit, credentials), |
  |   wifi scan (settings screen)                                  |
  +---------------------------------------------------------------+
  | components/ui — screens.c (all screens, UI_PX panel-scaled),   |
  |   actions.c (navigation + wake/sleep flow), moon_ui.c (the     |
  |   moon pipeline: bake tasks, spin, sweep, dusk, panel write)   |
  +---------------------------------------------------------------+
  | components/moon — the portable core (Chapter 11):             |
  |   moon_astro (ERFA ephemeris -> moon_geom_t), moon_bake        |
  |   (the per-pixel model, Chapter 6), moon_render (rotate +     |
  |   g565 LUT), moon_fixp (Q15.15), moon_assets (albedo + DEM)   |
  +---------------------------------------------------------------+
  | components/board — the board shim: board_display(),           |
  |   board_panel(), board_touch(), board_imu(), board_rtc()      |
  |   (S3 and P4 both, one API)                                    |
  +---------------------------------------------------------------+
  | vendored BSPs — esp32_s3_touch_amoled_1_75 (CO5300 QSPI),     |
  |   ESP32_P4 (JD9365 MIPI-DSI) — display init, touch, IMU, RTC  |
  +---------------------------------------------------------------+

The ``components/moon`` core is the only code that knows what a moon
pixel is. ``components/ui`` knows what a *screen* is. The clock
component knows what a *minute* is. The board shim is where "S3" and
"P4" stop being two codebases: every call site sees
``board_panel()``/``board_display()``, and only the shim's two
implementations know which silicon is underneath.

The tasks
---------

The system runs on FreeRTOS. The load-bearing tasks and what core each
is pinned to:

.. list-table::
   :header-rows: 1
   :widths: 18 10 40 32

   * - Task
     - Core
     - Pinned?
     - Owns
   * - ``main`` (app_main)
     - 0
     - no
     - boot sequence, the sweep's *display* at boot (it joins the
       LVGL task for the atomic flush), deep-sleep entry
   * - ``LVGL`` (adapter)
     - 0
     - yes (``task_pin_to_core 0``)
     - the screen tree, the 50 ms ``moon_tick`` lv_timer, the
       minute-driven clock update, the fade/dim ramps
   * - ``moon_spin``
     - 1
     - **yes** (``xTaskCreatePinnedToCoreWithCaps``)
     - the free-spin render: integrates the angle, runs
       ``moon_rotate`` over the whole 400×400 frame, writes the moon
       area straight to the panel
   * - ``panel_wdg``
     - 0
     - yes
     - the panel-io watchdog (Chapter 13): a 3 s-bounded test of the
       panel mutex, ``esp_restart()`` on overrun
   * - ``wifi_sync`` (background)
     - none
     - no
     - ``wifi_manager_init`` + SNTP sync off the LVGL task (the fix
       for the 1.7 s wake freeze, Chapter 15)
   * - bake tasks (orbit / sweep / dusk)
     - none
     - no (spelled out per pipeline in Chapter 14)
     - ``moon_bake`` per frame into the shared PSRAM buffers

Two design choices are worth calling out. First, the spin render is
**pinned to core 1** because the panel write is CPU-bound QSPI
streaming: the LVGL task (also on core 0, with the touch pipeline,
the clock minute update, and the fade ramps) must never share a core
with the spin's frame gather. Second, the bake pipelines (sweep,
dusk, orbit) run as *tasks* rather than on the LVGL task: a 1.3 s
frame bake must not block the 50 ms tick that drains the previous
frame's strips.

The buffers and who touches them
--------------------------------

The moon pipeline's buffers are all in PSRAM (``MALLOC_CAP_SPIRAM``):

.. list-table::
   :header-rows: 1
   :widths: 16 14 70

   * - Buffer
     - Size (S3, 400 px disk)
     - Writers / readers
   * - ``s_frame``
     - 400×400 × 1 B = 160 KB
     - bake tasks write (gray 8-bit); the spin reads it to rotate
   * - ``s_rgb``
     - 400×400 × 2 B = 320 KB
     - the spin's ``moon_rotate`` writes (RGB565); LVGL reads it as
       the moon image (``s_dsc.data``); the spin's panel write reads
       it via the step-rotate
   * - ``s_sweep_small``
     - 240×240 × 1 B (if reduction on)
     - the sweep's intermediate frames bake here and are upscased into
       ``s_rgb`` on display (Chapter 14)
   * - ``s_shade_map``
     - 864×432 × 1 B = 373 KB
     - the dusk's pre-shaded body-frame map (Chapter 14)
   * - ``s_front``
     - 400×400 × 2 B
     - the *front mirror*: LVGL's on-panel content in the moon area,
       panel byte order; refreshed by the ``draw_bitmap`` wrapper on
       every LVGL strip
   * - ``s_disp``
     - 400×400 × 2 B
     - the spin's per-frame composite (rotated moon + kept UI), the
       single window written to the panel
   * - ``s_ui_mask``
     - 400×400 × 1 B
     - the per-pixel UI keep-mask (Chapter 13)
   * - relief gradient cache
     - 2048×1024 × 2 B = 4 MB
     - built once at boot (``moon_relief``), read by every bake

The two locks that order all of it:

.. list-table::
   :header-rows: 1
   :widths: 16 30 54

   * - Lock
     - Serializes
     - Held for
   * - ``s_rgb_mtx``
     - every reader/writer of ``s_rgb`` *and* ``s_frame`` (the bake,
       the spin's gather, the orbit row sweep, the static LUT
       convert, the atomic ``lv_refr_now``)
     - one frame: a bake row sweep, a gather (~8 ms), or the
       whole-panel flush
   * - ``s_panel_mtx``
     - every QSPI/DSI panel transaction: LVGL flushes (via the
       ``draw_bitmap`` wrapper), the spin's direct write, the 0x51
       brightness writes (fade, dim, backlight)
     - one strip (~10 ms) or one spin frame (30–80 ms)

``s_rgb_mtx`` answers "who may read the image"; ``s_panel_mtx``
answers "who may touch the panel's io". They are deliberately
different resources: the spin holds ``s_rgb_mtx`` during the gather
and *releases it* before taking ``s_panel_mtx`` for the write, so a
bake task can re-bake the next phase while the spin's current frame is
still streaming to the panel.

The atomic-frame invariant
--------------------------

**Every moon-pipeline frame hits the panel as one atomic whole-panel
display.** On the LVGL path that means one ``lv_refr_now`` under
``s_rgb_mtx`` (``moon_ui_screen_switched``): the new screen — moon
image, labels, seconds arc — is rendered into LVGL's draw buffer and
flushed to the panel in one synchronous burst, so no strip from the
old screen and no strip from the new one ever coexist.

Without the invariant, LVGL's async 25-row strip drain repaints a
screen top-to-bottom over about a second — the "wake artifact" the
original design fought. The atomic rule is what makes a screen switch
look like a screen switch.

There are exactly **two sanctioned exceptions**, and both are
properties of the panel, not of LVGL:

1. **The fade-to-black screen switch** (``UI_SCREEN_SWITCH_FADE``):
   the switch dims the panel's own hardware brightness (CO5300
   command 0x51) to 0, writes *no pixels at all* during the dim,
   performs the one atomic flush while the panel is still black, and
   ramps the brightness back. The invariant is preserved *off the
   visible path*: the atomic flush runs, it just runs while nothing is
   visible (Chapter 14).
2. **The burn-in spin's direct panel write** (Chapter 13): the spin's
   frame is written straight to the panel's RAM with
   ``esp_lcd_panel_draw_bitmap`` under ``s_panel_mtx``, bypassing
   LVGL entirely. It is still atomic — one whole 400×400 window per
   frame, held under the mutex for the entire write — and it is the
   only non-LVGL writer the panel has.

Everything else that shows moon content obeys the rule: the boot
sweep's frames (one atomic flush per published frame, reduced-res
bake upscased), the dusk's re-projected frames (same), the wake (a
static frame), the orbit sweep (one atomic flush of the whole
converted frame).

Why the invariant is worth two locks
------------------------------------

The atomic rule is cheap when the pipeline is idle and expensive to
violate when it is not: the failure mode is not a crash, it is a
*half-screen frame* — the top 200 rows of one phase over the bottom
200 rows of the previous. That is precisely what a moon-phase clock
must never show, because the half-frame is *plausible*: both halves
are real moon pixels. The invariant is what makes "the screen shows
a consistent image" a theorem instead of a race.

The cost is two mutexes, a panel ``draw_bitmap`` wrapper (Chapter 13),
and a watchdog task that restarts the device if a panel transaction
ever parks forever on the QSPI bus lock (Chapter 13). The rest of
Part III is those mechanisms, one chapter each.
