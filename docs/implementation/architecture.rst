System architecture
===================

The science section described what the renderer computes. This section describes where
each computation runs, who owns which buffer, and which rules keep a
240 MHz dual-core microcontroller with a 32 KiB DMA budget from tearing
an image in half on screen. The architecture has three properties
worth stating up front, because every design decision below serves
them:

1. **One image buffer, one atomic flush.** Every moon-pipeline frame
   reaches the panel as a single whole-panel display under one
   critical section. There are exactly three sanctioned exceptions,
   and all are documented here.
2. **The physics is portable.** The moon core — ephemeris, bake,
   relief, rotation, the LUT — is a dependency-free C library that
   compiles unchanged into the device *and* the host simulator.
   Everything device-specific sits above it: the board shims, the LVGL
   UI, the panel's direct-write path.
3. **The display is the sleep surface.** The same pixels the clock
   shows while awake are what the light-sleep screen shows; the
   animations (:doc:`the animation pipelines <animations>`) are not decorative overlays — they are the
   transition itself.

The layer stack
---------------

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     main["main.cpp — app_main: BSP bring-up, wifi boot, clock init, boot sweep, deep-sleep entry"]
     clock["components/clock — minute display, NTP, idle/sleep state, alarm scheduler, timezone, settings"]
     wifi["components/wifi — wifi_manager, wifi scan"]
     ui["components/ui — screens, navigation + wake/sleep flow, the moon pipeline (bake · spin · sweep · dusk · panel write)"]
     moon["components/moon — the portable core (Ch. 11): ephemeris, bake, render, Q15.15, assets"]
     board["components/board — the board shim: display / panel / touch / IMU / RTC (S3 + P4, one API)"]
     bsp["vendored BSPs — S3: CO5300 QSPI · P4: JD9365 MIPI-DSI"]
     main --> clock
     main --> wifi
     main --> ui
     ui --> moon
     clock --> board
     wifi --> board
     ui --> board
     board --> bsp
   </pre>

The stack reads top-down (each layer calls the ones below it; the
``components/moon`` core calls no layer at all — it is the portable,
dependency-free library :doc:`the C core <core-c>` walks). The per-file detail each
box summarizes lives in the pages that own that layer: the clock
files in :doc:`the clock and sleep machine <clock-sleep>`, the UI files in :doc:`the animation pipelines <animations>`, :doc:`the animation pipelines <animations>` and :doc:`the clock and sleep machine <clock-sleep>`, the core in
:doc:`the C core <core-c>`, the shim and BSPs in :doc:`the display pipeline <display>` and :doc:`the P4 port <p4-port>`.

The ``components/moon`` core is the only code that knows what a moon
pixel is. ``components/ui`` knows what a *screen* is. The clock
component knows what a *minute* is. The board shim is where "S3" (the Waveshare
ESP32-S3-Touch-AMOLED-1.75) and "P4" (the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC, "4C") stop being two codebases: every
call site sees
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
     - the panel-io watchdog (:doc:`the display pipeline <display>`): a 3 s-bounded test of the
       panel mutex, ``esp_restart()`` on overrun
   * - ``wifi_sync`` (background)
     - none
     - no
     - ``wifi_manager_init`` + SNTP sync off the LVGL task (the fix
       for the 1.7 s wake freeze, :doc:`the clock and sleep machine <clock-sleep>`)
   * - bake tasks (orbit / sweep / dusk)
     - none
     - no (spelled out per pipeline in :doc:`the animation pipelines <animations>`)
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
       ``s_rgb`` on display (:doc:`the animation pipelines <animations>`)
   * - ``s_shade_map``
     - 864×432 × 1 B = 373 KB
     - the dusk's pre-shaded body-frame map (:doc:`the animation pipelines <animations>`)
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
     - the per-pixel UI keep-mask (:doc:`the display pipeline <display>`)
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

There are exactly **three sanctioned exceptions**, and all are
properties of the panel, not of LVGL:

1. **The fade-to-black screen switch** (``UI_SCREEN_SWITCH_FADE``):
   the switch dims the panel's own hardware brightness (CO5300
   command 0x51) to 0, writes *no pixels at all* during the dim,
   performs the one atomic flush while the panel is still black, and
   ramps the brightness back. The invariant is preserved *off the
   visible path*: the atomic flush runs, it just runs while nothing is
   visible (:doc:`the animation pipelines <animations>`).
2. **The burn-in spin's direct panel write** (:doc:`the display pipeline <display>`): the spin's
   frame is written straight to the panel's RAM with
   ``esp_lcd_panel_draw_bitmap`` under ``s_panel_mtx``, bypassing
   LVGL entirely. It is still atomic — one whole 400×400 window per
   frame, held under the mutex for the entire write — and it is the
   only non-LVGL writer the panel has.
3. **The TF sweep cache's direct-write replay**
   (``UI_MOON_TFCACHE`` / ``UI_MOON_TFCACHE_FPS``,
   :doc:`the animation pipelines <animations>`): warm-replayed
   *intermediate* sweep frames are displayed by the sweep task itself
   with the spin's very write (``moon_spin_write_panel`` — step
   rotated, UI keep-mask composited, under ``s_panel_mtx``), paced to
   ``UI_MOON_TFCACHE_FPS``. The tick's whole-panel refresh measures
   304 ms (S3) / 207 ms (P4) and would cap a tick-displayed replay at
   ~3 fps; the direct write keeps the same step-rotated, keep-masked
   whole-window atomicity as the spin. The sweep's **final** frame
   still rides the tick's atomic ``lv_refr_now`` — it reconciles LVGL
   with the panel and re-anchors the spin.

Everything else that shows moon content obeys the rule: the boot
sweep's frames (one atomic flush per published frame on the
card-less path, reduced-res bake upscased; the warm TF replay is
exception 3), the dusk's re-projected frames (same), the wake (a
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

The cost is two mutexes, a panel ``draw_bitmap`` wrapper (:doc:`the display pipeline <display>`),
and a watchdog task that restarts the device if a panel transaction
ever parks forever on the QSPI bus lock (:doc:`the display pipeline <display>`). The rest of
The implementation section is those mechanisms, one page each.
