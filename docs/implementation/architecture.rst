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
   critical section. There are exactly two sanctioned exceptions,
   documented here: the hardware brightness ramp (which writes no
   pixels) and the bake reveal's *atomicity-per-state* (:doc:`the
   bake reveal <bake-reveal>`).
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
     main["main.cpp — app_main: BSP bring-up, wifi boot, clock init, static moon bake, deep-sleep entry"]
     clock["components/clock — minute display, NTP, idle/sleep state, alarm scheduler, timezone, settings"]
     wifi["components/wifi — wifi_manager, wifi scan"]
     ui["components/ui — screens, navigation + wake/sleep flow, the moon pipeline (bake · orbit · panel write)"]
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
     - boot sequence (spawns the boot replay; with the replay off it
       bakes the static current phase itself), deep-sleep
       entry
   * - ``LVGL`` (adapter)
     - 0
     - yes (``task_pin_to_core 0``)
     - the screen tree, the 50 ms ``moon_tick`` lv_timer, the
       minute-driven clock update, the fade/dim ramps
   * - bake tasks (orbit / rebake / hourly / boot replay)
     - 1
     - **yes** (pinned, PSRAM-stack)
     - ``moon_bake`` per frame into the shared PSRAM buffers;
       ``moon_rebake`` re-bakes the static frame off the LVGL task,
       ``moon_hourly`` produces the once-per-hour frame (a bake or a TF
       hour-cache fetch) and ``moon_replay`` plays the stored hour
       frames ascending before baking the final now-frame — the tick
       displays each finished frame
       atomically
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

Two design choices are worth calling out. First, the bake producers
(orbit, rebake, hourly) are **pinned to core 1** because a bake
is CPU-bound work: the LVGL task (on core 0, with the touch pipeline,
the clock minute update, and the fade ramps) must never share a core
with a bake or a gather. Second, those pipelines run as *tasks*
rather than on the LVGL task: a 1.3 s frame bake must not block the
50 ms tick that drains the previous frame's strips — the tick's only
pipeline job is displaying what the producers publish.

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
     - bake tasks write (gray 8-bit); the LUT convert
       (``moon_convert_to_rgb``) reads it
   * - ``s_rgb``
     - 400×400 × 2 B = 320 KB
     - the LUT convert writes it (RGB565, one gather from
       ``s_frame``); LVGL reads it as the moon image
       (``s_dsc.data``)
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
       the orbit row sweep, the static LUT convert, the atomic
       ``lv_refr_now``)
     - one frame: a bake row sweep, a LUT gather, or the whole-panel
       flush
   * - ``s_panel_mtx``
     - every QSPI/DSI panel transaction: LVGL flushes (via the
       ``draw_bitmap`` wrapper), the 0x51 brightness writes (fade,
       dim, backlight)
     - one strip (~10 ms) or one 0x51 ramp step

``s_rgb_mtx`` answers "who may read the image"; ``s_panel_mtx``
answers "who may touch the panel's io". They are deliberately
different resources: a bake task holds ``s_rgb_mtx`` while it fills
``s_frame``/``s_rgb`` and releases it before the display path ever
takes the panel lock, so the next bake can start while the current
frame is still streaming out.

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

There are exactly **two sanctioned exceptions**. The first is a property
of the panel, not of LVGL: the **hardware brightness ramp** (CO5300
``0x51`` / the P4 backlight PWM), which writes no pixels. Two flows
ride it:

1. **The fade-to-black screen switch** (``UI_SCREEN_SWITCH_FADE``):
   the switch dims the panel's own hardware brightness (CO5300
   command 0x51) to 0, writes *no pixels at all* during the dim,
   performs the one atomic flush while the panel is still black, and
   ramps the brightness back. The invariant is preserved *off the
   visible path*: the atomic flush runs, it just runs while nothing is
   visible (:doc:`the animation pipelines <animations>`).
2. **The hourly refresh** (``UI_MOON_HOURLY_ENABLE``): once per hour the
   moon redraws behind this SAME ramp — fade to black, a core-1 producer
   rebuilds the frame (a bake, or a TF hour-cache fetch — no panel
   traffic), the finished frame lands as ONE atomic whole-panel flush
   while black, then the brightness ramps back. It reuses the screen
   switch's fade rule verbatim, and the clock, alarm row and seconds
   keep running through it (:doc:`the animation pipelines <animations>`).

The second exception is a *reframing* of the rule rather than a bypass
of it — the **bake reveal** (``UI_MOON_BAKE_REVEAL`` / the knob's
``KNOB_UI_BAKE_REVEAL``, user decision 2026-11-14): a frame that is
being *computed* is displayed as a designed sequence of consistent
states — the black canvas, then one completed strip band per 50 ms
tick, then the healed old frame on any abort — where **each state is
itself one atomic whole-band ``lv_refr_now`` under ``s_rgb_mtx`` with
the drain witness**. The rule forbids a frame torn MID-ROW by the
async drain; between reveal states the panel always shows a complete,
deliberately-chosen image, never the drain's partial work
(*atomicity-per-state*). The unbaked area is black by the same
decision (never a half-old/half-new composite), and the finished frame
still lands as one atomic whole-panel flush afterwards, so the
invariant's contract — commit-on-show, witness, "a consistent image
is a theorem" — holds at every stable state (:doc:`the bake reveal
<bake-reveal>`).

(The burn-in spin's direct panel write and the TF sweep cache's
direct-write replay were further exceptions until 2026-11-13, when
the spin subsystem was removed entirely at the user's request —
*"It does more things worse than good."* — and the boot sweep (the TF
replay's producer) followed later the same day. The TF cache lives on
as the hourly refresh's hour cache, and every moon frame now rides the
LVGL path.)

Everything else that shows moon content obeys the rule: the boot and
wake displays (one atomic flush of the current-phase bake — at boot the
reveal of that bake, where armed, is the band-atomic sequence of the
second exception above) and the orbit
sweep (one atomic flush of the whole converted frame). (The boot
sweep's frames — card-less and warm TF replay alike — and the dusk's
re-projected frames had obeyed it one flush per published frame until
their retirement.)

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
