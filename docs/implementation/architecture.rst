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

The short version
-----------------

Everything on this page answers one question. The device has a single
screen, a complete moon image takes over a second to compute, and the
viewer must never catch the screen *mid-computation*. The four answers:

1. **Divide by knowledge, not by file count.** Each layer of the stack
   knows exactly one kind of thing: moon pixels, screens, minutes,
   directions, silicon. The moon math never learns it runs on hardware,
   which is why the identical source compiles into the device *and* the
   host simulator.
2. **Split the work across the two brains, and never let them share
   chores.** The ESP32 has two CPUs. Core 0 is the painter — it draws
   the screen, watches for touch, updates the clock, and must respond
   every 50 ms. Core 1 is the mill — it grinds out moon frames, ~1.3 s
   at a stretch. A bake that ever shared a core with the painter would
   stall the screen.
3. **Hand work off through two buckets and two keys.** Computed images
   live in two PSRAM buffers — a gray sketch and its colored print —
   and two mutexes decide who may touch what: one key for the image,
   one key for the panel's wires.
4. **One rule above all: the screen never shows work-in-progress.**
   Every frame arrives as a single atomic burst. The two sanctioned
   exceptions (a pure brightness ramp, and the staged bake reveal) bend
   the letter, never the intent.

Everything below is the machinery behind those four sentences: the
layer stack, the task roster, the buffers and locks, and the invariant
itself.

Vocabulary
----------

Fourteen terms carry the page — the operating-system and hardware
basics are defined here, and the display-side ones also have fuller
entries in the :doc:`glossary <../glossary>`:

- **Task** — one independently-running program; the real-time OS
  (FreeRTOS) interleaves many of them so fast that they appear to run
  at the same time.
- **Core / pinning** — a physical CPU; the ESP32-S3 has two. *Pinning*
  a task binds it to one named core, which is how the design guarantees
  bakes and the painter never compete for the same CPU.
- **Mutex (lock) / critical section** — a key to a room: exactly one
  holder at a time, everyone else waits outside. A *critical section*
  is the stretch of code spent holding the key.
- **Atomic** — the viewer sees the whole thing or nothing; there is no
  observable half-state (glossary: *atomic frame*).
- **Flush / strip** — a *flush* copies a patch of pixels from RAM to
  the panel; panels like small pieces, so flushes go as horizontal
  *strips* (~25 rows on the S3). LVGL's default drain repaints a whole
  screen strip-by-strip, top-to-bottom, over about a second — the
  hazard the atomic rule exists to neutralize.
- **DMA** — direct memory access: a copy engine that moves bytes
  without CPU help. The S3's DMA carries at most 32 KiB per chunk and
  only reads internal RAM, which is why flush strips are staged there.
- **PSRAM** — the big RAM chip beside the ESP32: plenty for frame
  buffers, but reachable only through that limited DMA path.
- **LVGL** — the graphics library that keeps the tree of screens,
  labels and arcs and turns it into pixel writes.
- **Bake / producer** — the ~1.3 s computation of one complete moon
  frame from ephemeris and textures; the tasks that run it (orbit,
  rebake, hourly, boot replay) are the *producers*, publishing into the
  shared buffers.
- **LUT convert** — the pipeline's last step: a 256-entry look-up
  turns each 8-bit gray pixel into a 16-bit RGB565 color — one
  "gather" over the frame.
- **Relief gradient cache** — the 4 MB wall of precomputed terrain
  shading, built once at boot and read by every bake (glossary:
  *relief gradient cache*).
- **BSP / board shim** — the *BSP* (board support package) is the
  vendor's low-level driver for the panel chip; the *shim*
  (``components/board``) is our thin layer that makes an S3 and a P4
  look like one machine to everything above it.
- **Panel watchdog (``panel_wdg``)** — the guard task: if the panel key
  is not handed back within 3 s (a wedged bus), it restarts the device
  rather than leave a frozen screen up (glossary: *panel-io watchdog*).
- **0x51 brightness ramp** — the panel command (CO5300 ``0x51`` / the
  P4 backlight PWM) that dims or brightens the display while writing no
  pixels at all — which is why the fade flows fit inside the atomic
  rule's budget.

The layer stack
---------------

**The short version.** Picture a building where every floor can only
reach the one directly below it. Each floor owns one kind of knowledge:
``moon`` knows moon pixels, ``ui`` knows screens, ``clock`` knows
minutes, ``board`` knows the silicon, and the vendored BSPs know the
exact panel chips. Two facts make the project tractable: the moon core
has *no* dependencies at all (that is what "portable" means — the same
source compiles into the device and into the host simulator), and the
shim is the one place where "S3" and "P4" are different machines —
above it, every call site just says ``board_panel()``.

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

**The short version.** The device always runs about five workers at
once, and the table below is their crew roster: who runs, on which of
the two CPUs, and what they own. One line explains the whole design:
the LVGL painter lives on core 0 and must answer every 50 ms, so every
CPU-bound bake is *pinned* to core 1 and runs as its own task — a 1.3 s
bake can then never stall the tick that drains the previous frame's
strips.

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

Read the roster column by column. The **Core** column splits the crew:
the painter and its watchdog on core 0, every bake producer on core 1,
and ``wifi_sync`` pinned to *neither* — its only job is to do slow
network work out of everyone's way. The **Pinned?** column says ``yes``
exactly where timing matters: the LVGL task must stay where the touch
pipeline is, the bakes must stay where they cannot starve it. The
**Owns** column holds one curiosity: ``panel_wdg`` owns a watchdog whose
entire job is to notice when somebody forgot to hand back the panel key
(:doc:`the display pipeline <display>`).

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

**The short version.** Three pieces of shared memory, two keys. The
pipeline is a two-station paint shop: a producer sketches the moon in
grays into ``s_frame`` (8-bit, 160 KB), then prints it in color into
``s_rgb`` (RGB565, 320 KB) — and ``s_rgb`` is the only buffer the screen
ever reads. The relief cache is a 4 MB wall of precomputed terrain
shading that every bake consults. The two mutexes keep the stations
from tripping over each other: one lock for the image, one lock for the
panel's wires.

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

Read the hand-off as a relay race with two keys and no overlap: the
baker's leg runs entirely inside the image key — sketch, print,
release. The painter's leg starts only after, with the panel key.
Keeping the two legs disjoint is what lets the next bake and the
current streaming flush run at the same time, each trusting only its
own key.

The atomic-frame invariant
--------------------------

**The short version.** This is the rule the section is named for, in
one sentence: *the screen never shows work-in-progress.* A frame is
either not yet displayed or fully displayed — never half-drained. The
danger is LVGL's default manners: it repaints asynchronously, ~25-row
strips top-to-bottom over about a second, so any moment inside a switch
shows a hybrid of old and new. The fix is one synchronous burst — a
single ``lv_refr_now`` while holding the image lock. The two sanctioned
"exceptions" bend the letter, not the intent: a brightness ramp writes
no pixels at all, and the bake reveal replaces one image with a ladder
of *designed* images in which every rung is itself a complete, atomic
state.

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

**The short version.** Why take two mutexes and a watchdog for one
picture? Because the failure mode is not a crash — it is a *plausible
lie*: the top half of one moon over the bottom half of the next, every
pixel real, nothing in the logs to complain. On a moon-phase clock,
showing the wrong moon is a broken product. The locks are what turn
"the screen always shows a consistent image" from a hope into a
guarantee, and the rest of this section prices that guarantee.

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
