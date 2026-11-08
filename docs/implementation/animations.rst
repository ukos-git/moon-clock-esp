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

   <video src="../_static/img/boot_sweep.mp4" controls muted playsinline width="400"></video>

   The boot sweep, filmed from the bake (``boot_sweep.mp4``, from the bake — :doc:`the build system <../reference/build-system>` regenerates it with ``docs/gen_media.py``).

At boot the moon starts on a black screen and replays the **current
synodic cycle** — from the last new moon (a dark disk) to *now*, at
most 29.53 days — in ``UI_MOON_ANIM_FRAMES`` frames (default and
production: **1344**; the count was 336 until 2026-11-07, when the
per-frame illumination jump read as a visible stutter and the density
was quadrupled). The span is found by
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
published frame as one atomic whole frame — the whole-panel
``lv_refr_now``, measured 304 ms median on the S3 / 207 ms on the P4.
The **backpressure** rule —
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

Because the display (304 ms) is faster than the bake (~1.3 s at full
resolution), the bake paces the sweep and the wait is usually ~0; the
5 s cap is a safety bail (the only tick-paused case — sleep entry —
sets ``s_sweep_abort``, which the loop polls too). When the TF sweep
cache serves (below), the sweep task instead *displays replayed
intermediate frames itself* — the spin's direct panel write paced to
``UI_MOON_TFCACHE_FPS``, the third sanctioned exception — and
pre-sets the shown counter for those frames, so this rule holds
trivially and the tick never sees an intermediate.

**Resolution** — the intermediate frames bake at
``UI_MOON_ANIM_BAKE_PX`` (production: **240**, into the scratch
``s_sweep_small`` buffer) and are upscased NN to the full 400×400 for
display; the **final frame** (the current phase the spin resumes
from) always bakes at full ``MOON_PX``. The bake cost scales with
:math:`(S/400)^2`; the measured per-frame table from the accepted
336-frame capture (per-frame costs carry over to the 1344-frame
default; on the S3 board — the Waveshare ESP32-S3-Touch-AMOLED-1.75 —
from the pipeline's own header) is:

.. list-table::
   :header-rows: 1
   :widths: 20 24 26 30

   * - Bake size
     - Bake per frame
     - Sweep wall (1344 f)
     - Note
   * - 200 px
     - ~0.21 s
     - ~10 min
     - coarsest
   * - 240 px
     - ~0.28 s
     - ~11 min
     - the pre-8954e88 default (frame count
       quadrupled 2026-11-07, visible
       jump fix)
   * - 320 px
     - ~0.48 s
     - ~16 min
     - —
   * - 400 px (the default: native MOON_PX,
       since 8954e88)
     - 1.21–1.35 s
       (measured 2026-11-08, 864 albedo)
     - **2193 s ≈ 36.5 min** (measured
       2026-11-08, bench S3)
     - full res; montage pixel-identical
       to the static/spin bakes

The 200/240/320 rows are 336-frame-era per-frame captures scaled
arithmetically to 1344 frames; only the 400 px row (the shipped
default) is a 1344-frame measurement. The per-frame bake grew
~0.72 → ~1.2–1.35 s against the earlier note — the 864-albedo PSRAM
load-latency floor (the P4 sees the same effect at 2048: bake median
1543 ms @ 650 px).

plus a measured 271–281 ms of display + tick per frame (resolution
independent). A reduced bake size makes the on-screen montage coarser
(400/S × 400/S NN blocks) but **physically correct** — the lighting
and relief model is resolution-independent.

**TF card cache** (``UI_MOON_TFCACHE``, default on —
``components/ui/moon_tfcache.c``). The table above is the
*card-less* path. With a TF card in the socket the sweep instead
**replays bake results** at display cadence: one gray JPEG per hour
of the synodic cycle, streamed through the same convert block (the
``moon_gray565`` warm/event tint LUT colors replay frames exactly
like baked ones). It never *replaces* the bake path — it sits in
front of it:

- **Layout** — ``/sdcard/moon_cache/<calcver>_<target>_<bake_px>/<epoch_hour>.jpg``
  with ``epoch_hour = floor(epoch/3600)``. One entry per hour, at
  most 29.53 d × 24 ≈ **709 files** of a few KB each; every store
  evicts hours older than the current synodic cycle, so the cache
  never outgrows one cycle.
- **Versioned folder** — ``<calcver>`` is ``git describe --tags
  --always --dirty`` embedded at build time (``MOON_CALC_VER``, set
  by the root ``CMakeLists.txt``; commitizen's ``cz bump`` produces
  the ``v*`` tags — until then the folder carries the short hash).
  When the sweep first needs the cache and the current version
  folder is missing (new calc, wiped card), it is created and **all
  sibling version folders are deleted** — frames of an old
  calculation are stale by definition and never mix.
- **Self-describing** — each folder carries a ``meta.txt``
  (``calcver``, ``target``, ``bake_px``, ``moon_albedo_w``,
  ``relief``, ``jpeg_q``); any missing key or mismatch — above all
  ``bake_px`` when ``UI_MOON_ANIM_BAKE_PX`` changes — wipes and
  recreates the folder before baking, so switching resolutions can
  never serve wrong-resolution frames even inside one calc version.
- **Codec** — ``esp_new_jpeg`` **gray** JPEG at quality
  ``UI_MOON_TFCACHE_JPEG_Q`` (default **90** — the panel verdict
  chain, in order: q 10, the knob-demo recipe class, showed visible
  blocking in the flat dark and bright areas; q 32 *still* showed it
  on the panel during the warm replay (2026-11-08) even though the
  flats measured clean; 90 is the visually-transparent class for gray
  DCT encoding. Measured q90 files are 29 KB per hour at 400 px and
  73 KB at 650 px — a whole cached cycle is ~20 MB (S3) / ~51 MB
  (P4), comfortably inside any card — and encode/decode cost is
  resolution-bound, not quality-bound (S3 encode 48 ms, decode
  37 ms/frame at 400 px), so the replay path pays almost nothing for
  it. ``meta.txt`` pins it, so a change wipes and re-bakes the
  folder).
  The official decoder
  input is RGB, not gray, so the module **self-probes** at init
  (encode a 16×16 gradient, decode it back, keep the format whose
  max ``|diff|`` ≤ 24): GRAY → RGB565 gray-duplicate → RGB888
  gray-duplicate. Decode output is always RGB888 and the R channel
  *is* the gray.
- **Prefetch** — a producer task (``moon_tfprefetch``, pinned to
  **core 0**, PSRAM stack — with the sweep consumer on core 1 an
  equal-or-higher producer was preempted except during the
  consumer's pacing pauses and the queue starved, capping the warm
  replay at 4.6/3.6 fps; core 0 holds only the mostly-idle LVGL task,
  ~7–9 % during the sweep) walks the hour list ahead of the display;
  the queue depth is derived from free PSRAM,
  ``clamp(free / (S²·1.15 + 4 KiB), UI_MOON_TFCACHE_PREFETCH_MIN_SLOTS, 12)``,
  so 1-bit SD read jitter never sits on the frame path. The fetch
  path **allocates nothing**: one persistent input buffer (the size a
  valid cached file can reach) is allocated *before* the slot queue —
  on the 8 MB S3 only ~568 KB of PSRAM is free at prefetch start (the
  relief cache + assets + bake scratch own the rest), and a per-call
  file buffer racing a full queue for that head died at q90 file
  sizes (frame 39: ``cache off for this boot: no psram (fetch)``,
  2026-11-08).
- **Direct-write replay** (``UI_MOON_TFCACHE_FPS``, default 25) —
  replayed *intermediate* frames are displayed by the sweep task
  itself through ``moon_spin_write_panel()``: the spin's own sanctioned
  write — step-rotated by the current panel step, the live UI
  keep-mask composited over the moon window from the LVGL mirror, one
  continuous window write (S3) / 65-row strips (P4) under
  ``s_panel_mtx``, no LVGL refresh. The composite copies the keep
  runs (the ``(y, x0, x1)`` list built with the mask,
  ``spin_mask_runs_rebuild``) instead of scanning all
  ``MOON_PX²`` mask bits per frame. **Mask and mirror are one
  atomic pair:** ``spin_mask_rebuild_once`` writes each kept
  pixel's mirror value together with its mask bit, under
  ``s_panel_mtx``, from the *same* full-screen snapshot — and the
  replay seeds both (``s_mask_dirty`` + a bounded wait on the tilt
  monitor) before frame 0. It has to: the replay itself does zero
  LVGL flushes, and the boot's early label flushes can precede the
  flush-wrapper install, so a flush-history-only mirror composites
  black over the clock for the whole replay (observed 2026-11-08).
  If the snapshot allocation fails (a full prefetch queue squeezes
  the S3 pool to a ~100 KB head), the rebuild **keeps the existing
  pair** rather than falling back to band-only — which would destroy
  a good glyph mask and desync the mirror from it.
  The tick's whole-panel flush
  (304 ms S3 / 207 ms P4) would cap a tick-displayed replay at ~3
  fps; the direct write runs at the spin's frame cost instead.
  Constant-cadence pacing like the spin task — a slow frame is never
  repeated or skipped — and the sweep summary logs the **measured**
  fps and worst inter-frame gap. The **final** frame still rides the
  tick's atomic ``lv_refr_now`` (it reconciles LVGL with the panel
  and re-anchors the spin), card-less sweeps are tick-displayed and
  bake-paced as above, and if the spin's panel buffers are missing
  the replay falls back to tick display.
- **Write-through** — any frame the device computes that is not yet
  on the card is encoded and stored after it publishes, so the next
  boot finds it.
- **Hourly frames** — with the cache serving, the intermediate
  frames are the hour entries (epoch = ``hour·3600 + 1800``, the
  hour *center*, never ahead of *now*) plus the **final frame,
  always the live full-``MOON_PX`` bake**;
  ``UI_MOON_ANIM_FRAMES`` applies only to the card-less path. With
  fewer than one whole cached hour the sweep keeps the classic
  count-based list.
- **The card-less path is the code above, byte for byte** — missing
  card, mount failure, foreign card (never formatted), meta
  mismatch, decode error, a mid-sweep pull or a failed store all
  disable the cache silently for the boot and the sweep continues
  bake-paced. The same tint LUT colors both sources identically;
  they differ only in *how* the converted frame reaches the panel
  (the tick's atomic flush card-less, the spin's direct write on
  warm replay), so a replayed frame and a baked frame are
  indistinguishable on screen.
- **Mount** — per-board, in ``card_mount()``: the S3 socket is **SPI
  1-bit** (MOSI 1 / SCK 2 / MISO 3 / CS 41) and mounts on
  ``esp_vfs_fat_sdspi_mount`` over **SPI3_HOST** — SPI2 is the QSPI
  panel's bus, so the module initializes SPI3 itself (the SDSPI
  contract makes the caller own ``spi_bus_initialize``) rather than
  using the BSP helper that would ride SPI2 (``docs/devices/s3``).
  The P4 has a real **4-bit SDMMC** TF slot (CLK 43 / CMD 44 / D0–D3
  39–42, its own host, disjoint from the C6 Wi-Fi SDIO link —
  ``docs/devices/p4``) and uses the BSP's ``bsp_sdcard_mount()``.
  FATFS long filenames (``CONFIG_FATFS_LFN_HEAP``) are **required** —
  the hour keys and the version folder exceed 8.3.

**Measured 2026-11-08** on the two bench boards (the ``tfcache:
stats:`` line at sweep end, per-boot; final frame always the live
bake):

.. list-table:: Boot sweep wall, TF-cache state × board (bench 2026-11-08)
   :header-rows: 1
   :widths: 22 10 14 26 28

   * - Cache state
     - Board
     - Sweep wall
     - Per intermediate frame
     - Cache accounting
   * - card-less (fallback)
     - S3
     - 2193 s
     - bake 1.36–1.63 s
     - 1344-frame classic list, cache untouched
   * - card-less
     - P4
     - ~2430 s
     - bake ~1.81 s
     - 1344-frame classic list, cache untouched
   * - cold card (write-through pass)
     - S3
     - 1164 s
     - bake 1.33 s + put 264 ms (encode 48, card write 216) ≈ 1.77 s
       (SPI 1-bit)
     - put 703/0, evict 1158 ms; prefetch 2 slots × 156 KB
       (332 KB PSRAM head)
   * - cold card
     - P4
     - 1415 s
     - bake 1.89 s + put 313 ms (encode 92, card write 221) ≈ 2.2 s
       (SDMMC 4-bit)
     - put 703/0, evict 541 ms; prefetch 12 slots × 412 KB
   * - warm card (replay)
     - S3
     - 118 s (~5.97 fps, worst gap 1416 ms)
     - read 56 ms + decode 37 ms ⇒ 177 ms/frame; direct panel write
       49 ms
     - hits 703 miss 0 put 0, 20437 KB / 39660 ms read (≈0.5 MB/s,
       1-bit SPI), 29 KB per file
   * - warm card
     - P4
     - 132 s (~5.35 fps, worst gap 1996 ms)
     - read 55 ms + decode 79 ms ⇒ 184 ms/frame; direct panel write
       87 ms
     - hits 703 miss 0 put 0, 51247 KB / 38803 ms (≈1.3 MB/s, 4-bit
       SDMMC), 73 KB per file

Three readings. The **cold pass is already the cheaper bake**:
hourly granularity bakes 703 intermediates instead of the card-less
path's 1344, so even a full write-through pass runs in ~19 min
(S3) / ~24 min (P4). The **warm replay is read+decode-bound**, never
display-bound — the 49/87 ms panel writes sit far inside the
177/184 ms frame, the prefetch queue keeping the panel fed — and
lands the whole synodic story in **~2 min** on both boards at the
shipped q90. The **hourly JPEGs stay small**: 29 KB per frame at
400 px, 73 KB at 650 px — ~20 MB (S3) / ~51 MB (P4) for the whole
cycle.

Two honest caveats. The accounting is *per-boot*, the card is
*cumulative*: the hour list ages in as wall clock advances (it is
built at sweep start), so a boot right after the cold pass can still
miss a few tail hours and put them back — one measured P4 boot read
hits 680, miss 23, **put 23/0**; its next boot read 703/0/0. And
most S3 boots (3 of the 5 bench captures; a repeat warm replay ran
clean) carry the *pre-existing* boot classify burst — up to four
``IDLE0`` watchdog prints around a ~20 s cold day-cache scan on the
LVGL task at boot — identical card-less, intermittent, root-caused
to the sky-events day cache and tracked apart from this feature;
the P4 runs are clean (0 watchdog prints). That burst can also
swallow the replay's bounded 2 s mask re-seed wait (one S3 warm
boot, sweep start t≈45 s inside a t≈36–56 s burst, printed
``mask rebuild did not run within 2 s``) — by design the replay
then serves the existing mask+mirror pair, built post-wrapper by
the init rebuild and kept fresh by the flush wrapper.

Detach mid-sweep: a read fault on the producer or the consumer
disables the cache silently and the remaining frames fall back to
bake pacing — the card-less boot (above) is that path end to end;
the physical mid-sweep pull completes the matrix (:doc:`on-device
verification <../verification/device-verify>`).

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

   <video src="../_static/img/free_spin.mp4" controls muted playsinline width="400"></video>

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

   <video src="../_static/img/dusk.mp4" controls muted playsinline width="400"></video>

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

   <video src="../_static/img/fade.mp4" controls muted playsinline width="400"></video>

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
composite. That is why the fade is one of the three sanctioned
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
   * - Boot sweep (1344 f, native bake — S3
       400 px / P4 650 px)
     - 1344/1344 frames, wall 2193 s ≈ 36.5 min
       (2026-11-08 bench capture; bake median
       1211–1354 ms @ 400 px, shown 271–281 ms;
       0 QSPI errors, no panic — IDLE0 WDT
       starvation noise during the sweep only)
     - 1344/1344 frames, 0 WDT
       (~40 min wall @ 650 px,
       2026-11-07 capture)
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
