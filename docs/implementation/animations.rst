The animation pipelines
=======================

A moon clock is mostly *waiting* — waiting for the next minute, the
next hour, the next wake. The project's answer to waiting was to make
the waiting itself the interface: four motion pipelines, all built on
the one atomic-display rule (:doc:`the architecture <architecture>`) and the one panel-write
discipline (:doc:`the display pipeline <display>`):

1. **The boot sweep** — the current synodic month, played at boot.
2. **The free spin** — the ball-in-hand burn-in rotation, forever.
3. **The dusk** — the light-sleep entry: a constant-projection pole
   spin to the dark side.
4. **The fade** — the screen-switch dim, which writes no pixels.

All four share one pipeline shape: a *bake task* (core 1) produces
frames off screen into the shared PSRAM buffers; the *tick* (LVGL
task, 50 ms) displays each published frame as **one atomic whole
frame**; backpressure (the bake waits for the display) keeps the two
from tearing a frame in half.

The boot sweep
--------------

.. raw:: html

   <video src="../img/boot_sweep.mp4" controls muted playsinline width="400"></video>

   The boot sweep, filmed from the bake (``boot_sweep.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

At boot the moon starts on a black screen and replays the **current
synodic cycle** — from the last new moon (a dark disk) to *now*, at
most 29.53 days — in ``UI_MOON_ANIM_FRAMES`` frames (default and
production: **336**). The span is found by
``new_moon_epoch()``, a two-pass backward scan of ``moon_geometry``'s
illumination (6 h coarse grid over at most one synodic month, then a
20-min fine pass around the coarse minimum):

.. code-block:: c
   :caption: components/ui/moon_ui.c (new_moon_epoch, abridged)

   for (int64_t e = utc_epoch - 6 * 3600; e >= utc_epoch - SYNODIC_S; e -= 6 * 3600) {
       moon_geometry(e, &g);
       if (g.illum < best_illum - 1e-6f) { best_illum = g.illum; best = e; }
   }
   /* fine pass: 20-min grid over +/- 6 h around the coarse minimum */

Two passes because ``moon_geometry`` costs a few dozen milliseconds
each (the ERFA series), and a full 20-min grid over 29.53 days would
be ~2,200 calls. The 1e-6 slack keeps a flat/tied coarse step from
drifting the fine pass to a neighboring cycle's minimum.

The bake task (core 1, pinned, PSRAM stack) bakes the frames **off
screen** and publishes them one at a time; the tick displays each
published frame as one atomic whole frame (the same ~150 ms
``lv_refr_now`` the spin's display used). The **backpressure** rule —
the bake does not start frame *f*+1 until the tick has displayed
frame *f* — is what keeps a mixed frame (new top rows over old bottom
rows) off screen:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the sweep's backpressure)

   if (f > 0) {
       int waited = 0;
       while (!s_sweep_abort && s_sweep_shown < (uint32_t)f) {
           vTaskDelay(pdMS_TO_TICKS(10));
           ...
       }
   }

Because the display (~150 ms) is faster than the bake (~1.3 s at full
resolution), the bake paces the sweep and the wait is usually ~0; the
5 s cap is a safety bail (the only tick-paused case — sleep entry —
sets ``s_sweep_abort``, which the loop polls too).

**Resolution** — the intermediate frames bake at
``UI_MOON_ANIM_BAKE_PX`` (production: **240**, into the scratch
``s_sweep_small`` buffer) and are upscased NN to the full 400×400 for
display; the **final frame** (the current phase the spin resumes
from) always bakes at full ``MOON_PX``. The bake cost scales with
:math:`(S/400)^2`; the measured per-frame table on the 336-frame
sweep (on the S3 board — the Waveshare ESP32-S3-Touch-AMOLED-1.75 —
from the pipeline's own header) is:

.. list-table::
   :header-rows: 1
   :widths: 20 24 26 30

   * - Bake size
     - Bake per frame
     - Sweep wall (336 f)
     - Note
   * - 200 px
     - ~0.21 s
     - ~2.4 min
     - coarsest
   * - 240 px (default)
     - ~0.28 s
     - ~2.8 min
     - chosen to stay under ~3 min
   * - 320 px
     - ~0.48 s
     - —
     - —
   * - 400 px
     - ~0.72 s
     - ~5.3 min
     - full res; montage pixel-identical
       to the static/spin bakes

plus a fixed ~0.23 s of display + tick per frame (resolution
independent). A reduced bake size makes the on-screen montage coarser
(400/S × 400/S NN blocks) but **physically correct** — the lighting
and relief model is resolution-independent.

While the sweep runs: the spin render is paused (the flag poll), the
idle timer is held (the user is present for the sweep), and a sleep
transition aborts the sweep (``moon_sweep_interrupt``). On completion
the tick re-anchors the spin at the unrotated pose the last frame
showed, and the spin resumes from exactly there — the sweep hands the
burn-in rotation back with a seamless pose.

**Boot only.** Waking from sleep (touch, tilt, or an alarm dismiss)
shows the current moon phase **static**, with no animation — the wake
pre-bakes the static frame (:doc:`the clock and sleep machine <clock-sleep>`) so the first wake frame is the
current phase, not the dark-side disk.

The free spin
-------------

.. raw:: html

   <video src="../img/free_spin.mp4" controls muted playsinline width="400"></video>

   The free spin (burn-in rotation), filmed from the bake (``free_spin.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

The burn-in rotation is a *free* spin: the rendered frame is the
baked phase (one full ``MOON_PX`` bake), rotated in-plane by an
angle that integrates freely — no re-bake follows the hand, so the
spin never waits on the 1.3 s bake:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the spin render, abridged)

   double dt = (double)(t0 - s_spin_last_ms) / 1e6;
   if (dt < 0.0) dt = 0.0; else if (dt > 0.5) dt = 0.5; /* a resume step */
   bool active = clock_idle_last_activity_ms() < (uint64_t)CONFIG_UI_MOON_SPIN_SETTLE_S * 1000ULL;
   double w_target = active ? (6.283185307179586 / (double)CONFIG_UI_MOON_SPIN_PERIOD_S)
                            : (6.283185307179586 / (double)CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S);
   double alpha = 1.0 - exp(-(dt * 3.0) / (double)CONFIG_UI_MOON_SPIN_SETTLE_S);
   s_spin_omega += (w_target - s_spin_omega) * alpha;
   s_spin_theta += s_spin_omega * dt;
   float rad = (float)fmod(s_spin_theta, 2 * M_PI);
   moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2,
               MOON_PX / 2, MOON_PX / 2, moon_cos15(rad), moon_sin15(rad));
   xSemaphoreGive(s_rgb_mtx);
   if (!s_fade_busy)
       moon_spin_write_panel();

The production rates (``sdkconfig``):

.. list-table::
   :header-rows: 1
   :widths: 26 18 56

   * - Constant
     - Value
     - Meaning
   * - ``UI_MOON_SPIN_PERIOD_S``
     - 300 s/turn
     - the ACTIVE rate — the device was touched or tilted within the
       settle window (the idle system's activity timer tracks both)
   * - ``UI_MOON_SPIN_SETTLED_PERIOD_S``
     - 600 s/turn
     - the SETTLED rate — the slow burn-in creep the moon must keep
       doing at rest (a constant, phase-true rate; the old phase-true
       *coast* was effectively invisible and read as a freeze)
   * - ``UI_MOON_SPIN_FPS``
     - 8 fps
     - the display cadence; a dropped frame is a bigger angular step,
       never a stall
   * - ``UI_MOON_SPIN_SETTLE_S``
     - 10 s
     - the settle window; the rate smoothing's time constant is
       ~SETTLE/3

Three properties of this loop are load-bearing:

- **Time-based angle.** ``theta += omega*dt`` with real elapsed time,
  so the angular speed is exact regardless of frame drops. The
  0.5 s ``dt`` clamp bounds a resume (a sleep/wake gap) to half a
  second of spin — the resume cannot step a whole turn.
- **The rate, not the angle, survives the re-bake.** A re-bake
  zeroes ``theta`` inside its own critical section (``moon_spin_reanchor``),
  so the render can never mix a new frame with a stale pre-re-bake
  angle (an orientation jump) — but the *rate* carries over: a settled
  coast keeps coasting across the re-bake.
- **The pose survives the sleep.** ``moon_park`` (sleep entry)
  advances ``theta`` by the spin rate over the pause gap (clamped
  0.5 s) and displays that frame atomically — the moon rests at the
  exact pose the spin was heading to, and ``s_spin_last_rad`` (the
  last displayed angle) is what the wake path resumes the spin from.

The frame is the **direct panel write** (:doc:`the display pipeline <display>`): ~8 ms gather
+ ~4 ms composite + ~30–40 ms QSPI at 8 fps — a ~45 ms frame budget
in a 125 ms slot. While a fade ramp runs (``s_fade_busy``), the spin
keeps integrating its angle but **skips the panel write**, so the
0x51 ramp is the only panel-io traffic in flight (the freeze
prevention, :doc:`the display pipeline <display>`); the first frame after the switch lands from
the exact pose.

The orbit (tilt viewport)
-------------------------

Between the free spin (in-plane) and a full re-bake sits the **orbit**:
when the tilt sensor holds a sustained off-axis pose, the *viewport*
itself rotates (the camera basis folds about the screen normal by the
tilt angle) — a re-bake at the tilted orbit, displayed strip by strip
(latest-wins: only the newest pending orbit is baked; older tilt
samples are dropped). The orbit bake is a normal ``moon_bake`` into
``s_frame`` with the orbit-folded camera basis; its completion is one
atomic whole-frame flush, and the spin re-anchors at the new pose.
The orbit is what makes "tilt the clock and the moon shows a
different face" true, at the cost of a 1.3 s bake per sustained
change (the tilt confirmation — 1 s windowed mean, integer
quadrant, sustained confirmation, :doc:`the clock and sleep machine <clock-sleep>` — exists precisely so a
single sample never pays that cost).

The dusk
--------

.. raw:: html

   <video src="../img/dusk.mp4" controls muted playsinline width="400"></video>

   The dusk (light-sleep entry), filmed from the bake (``dusk.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

The light-sleep entry plays the **dusk**: the camera rotates about
the Moon's **pole** (body-frame +z), at the current phase, from the
exact on-screen pose the free spin parked it at, until the fully
**unilluminated** side faces the viewer — then the deep-sleep
countdown is armed. Sleep depth follows the *animation*, never a
fixed timer: the countdown fires only once the dark side is on
screen.

The dusk is cheap because of the **constant-projection property**
(:doc:`the dark side <../science/darkside>`): the renderer's color is a function of the body-frame
surface direction and the epoch's fixed body-frame Sun only — the
view basis selects *which* direction is visible but never enters the
*color*. So the full body-frame equirect color map (lit side: albedo
× displaced-normal Lambert × tone LUT; dark side: the faint backlit
surface; 864×432 8-bit cells) is **built once** for the dusk epoch
(``moon_shade_map_build``, ~1.5–2 s on the S3), and every pole-spin
frame **re-projects** that fixed map through the folded camera basis
— a ``moon_rotate``-class gather (one map load per pixel, ~50 ms),
**not** a lighting re-bake (~1.3 s). A fixed surface point keeps
exactly its map cell's color on every frame, so the terminator cannot
flicker.

Each frame folds the camera basis **three ways**, in order:

.. code-block:: text
   :caption: The dusk's camera fold

   orbit(o)    the tilt viewport of the parked frame (moon_orbit_geometry)
   inplane(th) the parked free-spin angle (moon_inplane_geometry) --
               the on-screen pose is a theta-rotated gather of the
               static frame, so the theta-folded basis reproduces it:
               the seamless entry seam
   pole(phi)   the animation itself (moon_pole_geometry): phi advances
               1 degree per frame from 0 to phi*, the shortest arc to
               the dark side (at most a half turn)

The endpoint is where :math:`\mathbf{E}\cdot\mathbf{S}` is minimized
(the visible disk fully night-side). With :math:`\mathbf{E}` the
orbit-folded view direction (the *actual* starting view — the tilt
peeks the view, so the endpoint is computed from the folded
:math:`\mathbf{E}`, not the plain one) and :math:`\mathbf{S}` the
body-frame Sun:

.. math::

   E(\phi) \cdot S = A\cos\phi + B\sin\phi + C, \qquad
   \phi^* = \mathrm{atan2}(-B,\, -A)

which is the shortest-arc minimum (the host-verified against a
full-circle grid). ``phi*`` in frames (1°/frame) is the animation's
duration — 14–95 s, display-paced at ~0.2 s/frame; the animation
*is* the sleep indicator.

The dark side is **computed** (``dark_zero = 1``) for every dusk
frame — the shade map's dark cells and the final bake's night side
are written the faint backlit surface (:doc:`the dark side <../science/darkside>`), so no
phase-dependent earthshine speckle survives to the rim while the far
side still shows the lunar surface. The gate keeps ``dark_zero = 0``
on its own ``pole_test`` path — the earthshine *model* is unchanged;
only the device-visible rendering drops it.

The **final frame** is a full ``MOON_PX`` bake at ``phi*`` — the one
pixel-exact dark-side lighting pass, which lingers on the sleep
screen through the countdown. On completion the task pre-bakes the
**static** frame (epoch + orbit viewport, unrotated), row by row so
an interrupt is honored: the wake shows the *current* phase, not the
dark-side disk, and the spin resumes from exactly the parked pose.

The rim sliver (``dk_sup``)
---------------------------

The dusk's endpoint has one edge case (:doc:`the dark side <../science/darkside>` documents the model;
this is where it lives in the pipeline): the pole spin never changes
the view *latitude*, so at ``phi*`` the visible disk can still carry
a thin geometrically-lit rim. The bake writes the backlit night side
for ``d < dk_sup`` instead of ``d < 0`` there — and since that sliver
is sun-lit, its backlit value clips to **pure black**. The
``MOON_DK_MARGIN`` erosion (:doc:`the dark side <../science/darkside>`) covers the intermediate
re-projected frames, whose nearest-neighbor map sample could otherwise
round a dark pixel to a lit cell and re-light the ray.

The fade (no pixels written)
----------------------------

.. raw:: html

   <video src="../img/fade.mp4" controls muted playsinline width="400"></video>

   The fade-to-black screen switch, filmed from the bake (``fade.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

Every screen switch (except the both-moon switch — airplane / WiFi /
the sleep screens share the one moon image) dims the **panel's own
hardware brightness** (0x51) to 0, switches the screen while the
panel is still black, and ramps back:

.. code-block:: c
   :caption: components/ui/moon_ui.c (the fade ramp)

   static void fade_ramp(int from_pct, int to_pct) {
       for (int s = 1; s <= FADE_STEPS; s++) {
           int pct = from_pct + (to_pct - from_pct) * s / FADE_STEPS;
           xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
           bsp_display_brightness_set(pct);
           xSemaphoreGive(s_panel_mtx);
           if (s < FADE_STEPS)
               vTaskDelay(pdMS_TO_TICKS(CONFIG_UI_SCREEN_SWITCH_FADE / FADE_STEPS));
       }
       s_panel_pct = to_pct;
   }

Production: ``UI_SCREEN_SWITCH_FADE`` = 240 ms, ``FADE_STEPS`` = 12
(20 ms per step). Each step is one ~100 µs SPI param transaction —
the ramp is free, so the steps are many and short, and the luminance
steps stay small enough not to read as discrete steps. The fade-in
leg displays the new screen as **one atomic whole-panel
``lv_refr_now`` while the panel is still black** — the invariant's
atomic display, off the visible path — and then the brightness comes
up and the fully rendered screen appears at once.

The old overlay fade (a black rectangle composited over the screen,
stepped) charged one full-screen ``lv_refr_now`` per step — ~175 ms
of QSPI strip traffic each — and the panel's left-to-right strip fill
showed as a slow **crawl** on every switch. The 0x51 fade writes no
pixels at all; the panel scales everything on screen at once (the
moon, the labels, the spin's direct writes) with no overlay to
composite. That is why the fade is one of the two sanctioned
exceptions to the atomic rule: the rule is preserved *off the visible
path*, not violated.

The light-sleep dim is the same ramp to a floor: the entry ramps
**down** to ``UI_LIGHT_SLEEP_DIM_PCT`` (production: 25%) over the
fade duration, and the wake / an alarm ring ramps it back to 100.
The dusk keeps spinning at the dim level — the whole display glows
down, the moon included.

Measured timings
----------------

The device-side numbers these pipelines produce (full
benchmark context in :doc:`performance <../verification/performance>`):

.. list-table::
   :header-rows: 1
   :widths: 30 22 48

   * - Pipeline
     - S3 (466 px, 400 disk)
     - P4 (720 px, 650 disk)
   * - Boot sweep (336 f @ 240 px)
     - ~2.8 min (wall)
     - 336/336 frames, 0 WDT
   * - Sweep final frame (full res)
     - ~1.3 s
     - 599 ms @ 618 px / 959 ms @ 650 px
   * - Spin frame (gather + write)
     - ~8 ms + ~30–40 ms
     - 103-row DSI strips; 0 underrun
   * - Dusk shade-map build
     - ~1.6 s (one-time)
     - ~282 ms (one-time)
   * - Dusk frame (re-project)
     - ~50 ms
     - —
   * - Dusk total
     - —
     - ~43 s (1°/frame, display-paced)

The pattern worth internalizing: **everything expensive is built
once** (the relief cache at boot, the shade map at dusk entry, the
static frame at completion), and the per-frame work is gather-class
(8–50 ms). The pipelines are display-paced, not bake-paced, which is
why the sweep and dusk run for minutes without the touch core ever
seeing a bake.

Next: the machine that decides *when* all of this runs — the clock,
the idle/sleep state machine, and the wake paths (:doc:`the clock and sleep machine <clock-sleep>`).
