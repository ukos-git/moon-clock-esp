Performance, measured
=====================

This page is the performance comparison the project owes: the
same moon bake, timed on **six engines** — the C core on the host,
the C core on each of the three boards (the Waveshare
ESP32-S3-Touch-AMOLED-1.75, the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC, and the Waveshare
ESP32-S3-Knob-Touch-LCD-1.8), the Python prototype (numpy, float64,
vectorized), and the JavaScript port (V8, scalar) —
across **every disk size** the product renders, plus the per-pipeline
breakdowns (geometry, relief build, rotation, shade map, re-project)
and the on-device CPU costs. Every measurement is a **plot** (time
series and time-vs-input-count are data, not rows of a table), every
command is in the text, and the numbers that come from the device are
quoted from accepted captures (:doc:`on-device verification <device-verify>`) — the device is not touched for
this page.

The plots are drawn by ``docs/tools/gen_perf_figs.py`` from
``docs/tools/perf_data.json`` (committed next to the generator); the
data file is written by ``docs/tools/perf_measure.py``, which runs the
whole suite — both ``moon_bench`` builds at all three settings, the
Python prototype, and the JS port under Node:

.. code-block:: bash
   :caption: Re-measure everything, then redraw the figures

   docs/.venv/bin/python docs/tools/perf_measure.py --iters 5
   docs/.venv/bin/python docs/tools/gen_perf_figs.py

The benchmark harness
---------------------

The host numbers come from ``host_sim/bench.c`` (a tracked bench
harness that links the *exact* device core — the same
``moon_astro.c``/``moon_bake.c``/``moon_render.c``/``moon_fixp.c`` +
vendored ERFA the device compiles, at ``-O2``):

.. code-block:: bash
   :caption: Building and running the bench by hand

   cd host_sim
   cmake -B build -S . && cmake --build build --target moon_bench -j
   ./build/moon_bench --utc 2026-04-15T12:00 --iters 5 \
       --sizes 200 240 320 400 466 618 650

   # the A/B variants:
   ./build/moon_bench --utc 2026-04-15T12:00 --sizes 400 --no-relief
   ./build/moon_bench --utc 2026-04-15T12:00 --sizes 400 --darkzero --neutral

``perf_measure.py`` runs exactly these for both targets (the S3-asset
build ``build/`` and the P4-asset build ``build-p4/``), plus the two
comparators: the validated prototype's ``moon.animation.render_frame``
under ``assets/images/moon/.venv``, and the interactive sim's
``moonlab.js`` bake under Node (``docs/tools/perf_bench_moonlab.js``).
All at one fixed epoch (2026-04-15T12:00) so every engine's numbers
are comparable, median of 5 iterations.

The bench times each step as the device runs it: ``moon_geometry``
(the ERFA ephemeris, once per bake), ``moon_relief`` (the one-time
gradient cache build), ``moon_bake`` (the per-pixel model),
``moon_rotate`` (the spin's Q15.15 gather), ``moon_shade_map_build``
(the dusk's one-time map), and ``moon_reproject`` (the dusk's
per-frame map gather). The relief build is first-run-only (the device
builds it once at boot).

Host machine: AMD Ryzen 9 9950X3D (Zen 5, 16 cores, AVX-512),
Linux 7.0. The host numbers are *compute* numbers — the host's RAM
has no 32 KiB DMA cap, no staging copy, and ~ns-scale load latency,
so the host C bake measures the algorithm, not the memory system.
That is exactly the split this page needs: the host says *how much
work* a bake is; the device says *what the work costs* on the silicon
it ships on.

The six engines
---------------

- **C core (host)** — float32, scalar, ~ns load latency: the
  algorithm's compute cost.
- **C core (S3)** — float32 + FPU, Xtensa, octal PSRAM at ~100s ns
  random load: the shipping product (400 px disk).
- **C core (P4)** — float32 + RISC-V vector, X16 PSRAM (instruction +
  data shared): the second product (650 px disk).
- **C core (knob)** — float32 + FPU, Xtensa (the same S3-class
  ESP32-S3R8 at 240 MHz), octal PSRAM: the family's third board
  (360 px disk, :doc:`the knob display <../implementation/knob-display>`).
- **Python (numpy)** — float64, vectorized, ~ns load latency: the
  validated reference model.
- **JS (V8)** — float64, scalar: the browser simulator
  (:doc:`the interactive simulator <../science/interactive>`).

The Python and JS comparators run the *port* paths (the validated
prototype's ``moon.renderer`` via ``animation.render_frame``; the
site's ``moonlab.js`` ``MoonLab.bake`` at the display settings,
K=0.20, soft=0.03, backlit, relief on), sized to match the C bake's
square disk.

The complexity of each step
---------------------------

Before the numbers: what each step *has* to cost. The bake draws on
the board's base albedo asset (864×432 on the S3, 2048×1024 on the
P4 — the down-scale to disk size happens *by sampling*: the bake
picks one **nearest-neighbour** tap per output pixel, see
:ref:`the resolution funnel <sharpness-funnel-perf>` for why one NN
tap is enough), and the bake is native: the home frame lands on the
panel 1:1, no resample. ``N`` below is the bake's pixel count
(``size²``):

- **geometry** — one ERFA series per frame: **O(1)** in every input.
  It measures ``0.0 ms`` (below the bench's 0.05 ms resolution) at
  every size; the cheap-model decision (:doc:`the geometry <../science/geometry>`) is a non-issue at
  this resolution.
- **relief cache build (once, boot)** — one pass over the DEM
  (2,097,152 px): **O(N_dem)**, independent of disk size: ~16 ms host,
  ~1.3 s S3 (the 4-wide unroll), ~12 ms in the JS port.
- **bake** — one lighting evaluation per output pixel: **O(N)** in
  pixels = **O(size²)** in the disk's linear size. Sampling the base
  asset is **O(1) per pixel regardless of asset resolution** — a
  2048-wide P4 albedo costs the same per bake pixel as the S3's
  864-wide one (the plots show both host builds on top of each
  other).
- **rotate (spin gather)** — one Q15.15 gather per output pixel:
  **O(N)**.
- **shade-map build (once, dusk entry)** — one evaluation per map
  pixel: **O(N_map)** where ``N_map`` is the *asset* map
  (864×432 S3 / 2048×1024 P4), so **O(1) in display size** — the
  curve is flat across all seven bake sizes.
- **re-project (dusk frame)** — one map tap per display pixel:
  **O(N)** in display pixels; the tap is **O(1)** whatever the map
  resolution.
- **up-scale to the panel (sweep-era only, retired 2026-11-13)** —
  the boot sweep's 240-px baked disk drawn onto the ``panel``-px
  round display was one bilinear LVGL transform sample per *display*
  pixel: **O(display²)** in the canvas draw and the flush strips
  (:doc:`the display pipeline <../implementation/display>`). The
  sweep existed precisely to let that *draw* do the up-scaling
  cheaply (pre-8954e88); since the native bake (``MOON_PX`` 400/650)
  the home frame lands 1:1 and this cost is simply **gone** — the
  step no longer exists to be measured.

The measured log-log slopes in the figures below are the empirical
signature of these classes: ~2.0 against linear size (≡ linear in
pixels) for every per-pixel step, and 0.0 for the one-time builds.

The bake, across sizes and engines
----------------------------------

.. figure:: ../_static/figs/perf_engines.png
   :alt: Bake time vs disk size for all six engines, log-log
   :figclass: align-center

   Single-frame bake time vs disk size — host data re-measured
   2026-11-15 (median of 5), the knob point from the 2026-11-14
   reveal-bench capture. The two host C builds sit together at the
   bottom (their marker *shape* tells S3-asset from P4-asset); the
   device points (hollow markers — circles S3, squares P4, diamonds
   knob) are the accepted-capture wall times from
   :doc:`on-device verification <device-verify>`, plotted on the same
   millisecond axis. The dashed gray line is the ``size²`` reference
   through the host curve.

.. figure:: ../_static/figs/perf_vs_pixels.png
   :alt: Bake time vs pixel count, linear, through the origin
   :figclass: align-center

   The same data against *input count* (pixels = size²) on linear
   axes: every engine is a straight line through the origin — the
   O(N)-in-pixels claim, shown rather than asserted. The host C fit
   is 13.7 ns per pixel.

The plot's story, in five facts:

1. **The C core is the fastest engine at the device's sizes — by a
   wide margin.** At 400 px the host C bake is 2.25 ms; the Python
   reference is 47.6 ms (21×) — that number is
   ``moon.animation.render_frame``, the module the comparator's blue
   edge points at in :ref:`the package in one picture
   <python-surface>`; the JS port is 10.3 ms (4.6×). The C
   core is not a *re-implementation* of the Python model for speed —
   it is the *same model* written for the constraint (float32, a
   gradient cache, no per-pixel allocation), and it runs the model
   faster on a 2025 desktop than the vectorized reference does.
2. **The device cost is memory, not math.** The S3's 400 px bake is
   ~1.2–1.35 s (measured 2026-11-08, 864 albedo; an earlier capture
   read ~0.72 s) against the host's 2.25 ms — ~500×. The FPU work is
   identical (the host C bake *is* the FPU work, at 2.25 ms); the
   300× is the PSRAM: each pixel issues an albedo byte + a 2-byte
   gradient load from octal PSRAM at ~100s of ns of random-access
   latency, and the unroll (:doc:`the C core <../implementation/core-c>`) only overlaps, never
   removes, the latency. The **relief-off floor proves it**: at 466
   px the relief-off bake is 0.95 s — the albedo load alone is the
   floor, and the gradient cache's 0.35 s of extra PSRAM traffic is
   the *cheaper* option against the naive 6-load-per-pixel
   alternative (~3.3 s, :doc:`the lighting model <../science/lighting>`). The bake is
   **PSRAM-load-latency-bound**, and no FPU change will make it
   faster; only the load pattern (the cache, the unroll) will.
3. **The P4's vector unit changes the class.** The P4's 618 px bake
   (0.60 s) is *faster than the S3's 466 px bake (1.3 s)* at a
   1.8× pixel count — the RISC-V vector unit amortizes the per-pixel
   FPU ops, and the X16 PSRAM's bandwidth covers the loads. The P4
   bake is still memory-bound (0.96 s at 650 px), but it has ~15× the
   S3's headroom for the same frame.
4. **JS and Python bracket the C core from above.** The JS port
   (V8, scalar, float64) is 4.6× slower than the host C at 400 px —
   the scalar port pays the float64 + no-cache cost without a FPU
   head start — and the Python reference (SIMD, float64, but the
   full-resolution per-pixel gradient *without* the cache) is 21×.
   The cache is worth ~6× of the C core's speed against the
   reference model, on the same silicon. (The JS port is *faster
   than the Python reference* at every size — the browser sim runs
   the *cached* gradient, the prototype does not.)
5. **The knob confirms the memory-bound class from a third point.**
   The knob is S3-class silicon (ESP32-S3R8, 240 MHz, octal PSRAM) at
   a third disk size: its 360 px boot bake is **991 ms** (2026-11-14
   reveal-bench, non-revealed) — 7.6 µs per pixel against the S3's
   ~8.1 µs/px at 400–466 px. Same class, same floor: on every Xtensa
   board the PSRAM load latency, not the FPU, sets the bake time, and
   the bake time scales with the pixel count. Only the P4 (fact 3)
   escapes the class — with its vector unit *and* its wider PSRAM.

The comparison per board variant
---------------------------------

The same bake at each board's *native* disk size, side by side — every
cell an accepted capture or a gate run, never a projection:

.. list-table::
   :header-rows: 1
   :widths: 14 12 22 18 22 14

   * - Board variant
     - native bake px
     - single bake (accepted capture)
     - one-time relief build
     - revealed bake (band-flush path)
     - image gate (MAE / SSIM)
   * - S3 AMOLED 466
     - 400
     - ~1.2–1.35 s (2026-11-08, 864 albedo; an earlier capture read
       ~0.72 s)
     - ~1.3 s (2048×1024 DEM)
     - cold-cycle revealed bake 2687 ms internals; boot-replay final
       leg 2009–2615 ms (2026-11-14)
     - 6.68 / 0.908
   * - P4 4C 720
     - 650
     - 0.96 s (618 px: 0.60 s — M3/M4 captures)
     - — (not captured)
     - not captured (P4 leg of the 2026-11-14 bench deferred)
     - 6.73 / 0.907
   * - Knob 360
     - 360
     - 991 ms (2026-11-14, non-revealed)
     - 727 ms (1024×512 DEM)
     - 2028–2059 ms (2026-11-14 — the ~+105 % visible-for-computation
       tax)
     - 6.45 / 0.907

(The gate rows are ``check_gate.py --target …`` runs on 2026-11-15 at
the family HEAD; the per-board gate story is
:doc:`the image gate <image-gate>`.)

.. figure:: ../_static/figs/perf_variants.png
   :alt: per-variant comparison — throughput per pixel, relief build, revealed tax
   :figclass: align-center

   The same three facts as the table, as charts. **Left:** throughput
   per baked pixel at each board's native size (log axis) — host C read
   live from ``perf_data.json``, the device rows from the accepted
   captures; the S3 and knob bars land on top of each other because
   they *are* the same silicon paying the same PSRAM latency, and the
   P4 bar sits 3.6× left of them. **Middle:** the one-time relief
   build against the DEM grid — the knob's halved 1024×512 DEM halves
   the clocks' build, the **O(N_dem)** signature; the P4 leg is hatched
   because it was never captured (the board was off the bench).
   **Right:** the plain bake vs the revealed bake per board — solid
   bars the non-revealed baseline, dotted bars the accepted revealed
   window with its min–max error bar; this is the visible-for-
   computation tax of :doc:`the bake reveal <../implementation/bake-reveal>`
   in one glance. The generator is ``docs/tools/gen_perf_variants.py``
   (device constants quoted beside it; the host row re-reads
   ``perf_data.json`` so figure and table cannot drift apart).

Read the table as *throughput per pixel*, not wall time (the figure
above plots exactly that): the knob's
991 ms at 360 px and the S3's ~1.3 s at 400 px are the same silicon
paying the same PSRAM latency per pixel, the P4 buys its 2.5×
pixel-count lead over the S3 at *lower* wall time (fact 3), and the
revealed column is the honest price of the bake reveal —
:doc:`the bake reveal <../implementation/bake-reveal>` books the band
flushes into the bake's own budget. The relief-build column scales
with the DEM grid, exactly as the **O(N_dem)** class above predicts
(knob's halved DEM ≈ half the clocks' build).

The per-step breakdown (host C)
-------------------------------

.. figure:: ../_static/figs/perf_steps.png
   :alt: Per-step host times vs disk size, log-log, with measured slopes
   :figclass: align-center

   ``moon_bench``, 2026-04-15T12:00, -O2, median of 5 (S3-asset
   build). The legend carries each step's measured log-log slope: the
   per-pixel steps (bake, rotate, re-project) rise at slope ≈ 2
   against linear size; the one-time shade-map build is flat (slope
   0.04 — a rounding error, i.e. **O(1)** in display size).

Three structure-of-cost facts:

- **Geometry is free** (``0.0 ms`` = < 0.05 ms, one ERFA series per
  frame). The ephemeris is a rounding error against the bake — the
  cheap-model decision (:doc:`the geometry <../science/geometry>`) is a non-issue at this resolution.
- **The shade-map build is size-independent** (~2.4 ms on the
  S3-asset build, ~12.5 ms on the P4-asset build, at every bake size):
  it builds the *asset* map (864×432 S3 / 2048×1024 P4 — the 5.2×
  ratio is 5.6× the pixels), and the display size only affects the
  re-project. That is the dusk's economics in one curve — a one-time
  ~2.4 ms (host) / ~1.6 s (S3) build buys a per-frame re-project that
  scales with display size (0.3–3.0 ms host, ~50 ms S3) instead of a
  per-frame *bake* (which would be 0.7–1.3 s on the S3).
- **The rotate is ~1/13th of the bake** (0.18 ms vs 2.25 ms at 400
  px). The Q15.15 gather + fused LUT (:doc:`rotation and the LUT <../implementation/rotate-lut>`) is why the spin
  can display at 8 fps off a *once-baked* frame: the per-frame cost
  is the gather (~8 ms on the S3, 40× the host, again the PSRAM),
  not a re-bake.

The A/B variants (host C)
-------------------------

.. figure:: ../_static/figs/perf_ab.png
   :alt: Grouped bars of bake time by setting across all sizes
   :figclass: align-center

   Bake time by setting (host C, S3-asset build, same epoch, median
   of 5) at every disk size: the gate path (relief on), relief off,
   and the display path (``dark_zero=1`` + neutral LUT — the
   product's backlit-night + soft-terminator branch). At the 400 px
   product size: **relief off saves 31 %, the display path costs
   +24 %** — and the shape of the trade-off holds across the size
   range, which is why the sweep (retired 2026-11-13) could trade
   resolution for frames.

The display path's +24 % is the backlit night side (a large fraction
of the disk at a crescent) running the ``moon_backlit_night`` branch
per dark pixel, plus the soft-terminator smoothstep at the terminator.
That 24 % is the *art pass's* performance cost (:doc:`the artistic pass <../science/art-pass>`), paid by the S3 at
~0.17 s per 400 px frame — a price the sweep's 240 px bake (0.28 s
wall) and the spin's once-baked model used to make affordable (both
retired 2026-11-13). The relief
cache's 31 % is the cheap side of that trade: the one-time ~1.3 s
gradient-cache build (S3) buys back ~2 s per frame of naive
6-loads-per-pixel DEM sampling.

The device pipelines (accepted captures)
----------------------------------------

.. figure:: ../_static/figs/perf_device.png
   :alt: Horizontal bar chart of device pipeline wall times, log scale
   :figclass: align-center

   Every device pipeline cost, from the accepted captures (the
   on-device-verification standard: progress + timing + zero failure
   lines), on one log millisecond axis, colored by kind: per-frame
   bakes (blue), display-paced per-frame costs (cyan), one-time
   builds (amber), and the brightness-ramp fade (vermilion).

The figure is an **accepted capture of the spin/sweep/dusk era** — all
three motion pipelines retired 2026-11-13, so the cyan display-paced and
gather-paced bars are dated history (their live successors' hourly-refresh
bench numbers are pending). The *pipeline-level* lesson is the ratio that
made the product: the dusk and the sweep (both retired 2026-11-13) **were
display-paced, not bake-paced** (the per-frame display cost — ~0.23 s on
the S3 — is *less* than the bake, so the bake task paces and the display
keeps up with backpressure), and the spin (retired the same day) was
**gather-paced, not bake-paced** (the 45 ms frame against a 1.3 s bake was
the ~30× — 150× counting just the 8 ms gather — that made a *free* spin off
a once-baked frame possible). A pipeline whose display cost exceeded its
bake cost would show the backpressure failing (the ``shown in`` deltas
tracking the bake) — which is the verification signature :doc:`on-device
verification <device-verify>` checks. What remains on the device today is
the **static bake** (boot / wake / day tick / the hourly producer's TF-miss
path — all the same native-resolution ~1.3 s S3 / ~2.4 s P4 / ~1 s knob
bake, the 864/2048-albedo PSRAM load-latency floor; the knob's 360 px
native bake is the 991 ms point of the per-variant table above) and the
**orbit** re-bake (~1.7–2.4 s), plus the **hourly refresh**'s two 240 ms
hardware-brightness
fade legs around that bake.

The bake-reveal bench (2026-11-14, ``CONFIG_TEST_HOURLY_FORCE_S`` +
cold/warm boot captures, :doc:`the bake reveal
<../implementation/bake-reveal>`) put measured numbers on the live
paths: hourly warm TF hits land the frame in **185–235 ms** (after
the ``new_moon_epoch_cached`` memoization — pre-fix the cache-policy
leg re-ran the ~8.2 s ephemeris scan every cycle and blew the 5 s
producer budget every time, a bench-#44 catch); a cold-cycle revealed
bake measures **2687 ms** internals, whole cycle **3083 ms** — inside
the 5 s budget — with the fade-back paid at the first strip (observed:
``bake reveal: first strip up, panel brightness 0 -> 100``); the boot
replay's revealed final leg is
**2009–2615 ms** (20 band states); the knob's revealed boot bake runs
**2028–2059 ms** against its **991 ms** non-revealed baseline — the
visible-for-computation tax (band flushes consume real bake
throughput), and the knob's one-time relief build lands at **727 ms**
— half the clocks' ~1.3 s, the **O(N_dem)** signature of its halved
1024×512 DEM. Drain witness across the reveal legs: ~5 in-place heals
per boot leg, 0 site restarts, 0 ``PANEL IO STUCK``. P4: bench
deferred (board off the bench this pass).

The ladder above is the page's own prose as one picture: the hourly
refresh has exactly four outcomes the product pays for, judged against
the producer's single budget constant (``HOURLY_BUDGET_MS``,
``components/ui/moon_ui.c``) and the fade legs' ``UI_MOON_HOURLY_FADE_MS``.

.. figure:: ../_static/figs/perf_hourly.png
   :alt: hourly refresh outcomes on one log axis against the 5 s producer budget
   :figclass: align-center

   The hourly-refresh ladder — every accepted-capture outcome on one
   log millisecond axis with the 5 s ``HOURLY_BUDGET_MS`` line
   (dashed red). The TF hour-cache hit (185–235 ms, dark segment the
   accepted window) plus the two 240 ms hardware-brightness fade legs
   is **~7× cheaper than the plain bake it replaces** (1300 ms, S3
   native 400 px) and ~13× cheaper than the revealed legs; the
   revealed outcomes (dotted bars, min–max error bars) still land
   *inside* the budget — the memoization catch is what made that true:
   before ``new_moon_epoch_cached``, the cache-policy leg re-ran the
   ~8.2 s ephemeris scan every cycle (red ✗) and blew the budget every
   hour (bench #44). The knob's revealed pair is the same story on the
   third board. Solid bars non-revealed, dotted revealed. The
   generator is ``docs/tools/gen_perf_hourly.py`` (accepted captures
   quoted beside it; budget and fade are the device's own constants).

The boot-path ladder
--------------------

The ladder above is the *steady state* — what the most frequent refresh
costs once the device is running. The boot ladder is what the user
waits for first, and the boot replay (``UI_MOON_BOOT_REPLAY``,
:doc:`the animation pipelines <../implementation/animations>`) has
exactly four cost paths. Its pacing constants are the device's own —
the generator *parses the live* ``components/ui/Kconfig``, so figure
and Kconfig cannot drift apart:

- The replay producer paces at a floor of
  ``1000 / UI_MOON_REPLAY_FPS`` = **125 ms/frame** (the default 8 fps);
  the panel actually drains at the documented **~6 fps** (~167 ms/frame,
  S3 — the sweep-era TF-replay throughput the Kconfig itself quotes).
  The cache holds at most one synodic month of hourly keys — **709
  files** — so a full warm playthrough is **89 s** at the floor and
  **~118 s** at the observed rate: both well inside the default
  **240 s** budget (``UI_MOON_REPLAY_BUDGET_MS``), which is what the
  Kconfig's own claim ("a full cache at the default pacing lands well
  inside the default 240 s") means in milliseconds. An overrun cuts the
  *animation*, never the moon — the final leg still bakes the current
  phase.
- A card-less boot (no card, cold folder, or version wipe — a fresh
  flash by design, :ref:`per-version keying <calcver-keying>`) pays the
  ~**1 s** SD mount probe, then holds black until the revealed now-bake
  lays its first ink, and lands the current phase at probe +
  **2009–2615 ms**.
- ``UI_MOON_BOOT_REPLAY=n`` (and demo mode) never touch the SD bus and
  boot to the single static bake (**1300 ms**, the same accepted
  capture as the per-variant table above).
- The knob has no SD socket, so it *always* takes the black path —
  **2028–2059 ms** revealed against its **991 ms** non-revealed
  baseline — and pays no probe at all (there is no bus to probe).

.. figure:: ../_static/figs/perf_boot.png
   :alt: boot-path ladder — replay pacing and boot outcomes vs the 240 s budget
   :figclass: align-center

   The boot ladder. **Left:** the pacing arithmetic — the floor the
   Kconfig sets (1000/8 = 125 ms, light) against the observed drain rate
   the Kconfig quotes (~6 fps ≈ 167 ms, solid). **Right:** the four boot
   paths on one log millisecond axis against the 240 s
   ``UI_MOON_REPLAY_BUDGET_MS`` line (dashed red) — the probe, the
   non-revealed static bake, the revealed now-bake (dotted, accepted
   min–max), the full-cache animation at both paces (709 files × the
   panel's own per-frame cost), and the knob's always-black pair
   (violet). Solid non-revealed, dotted revealed. The generator is
   ``docs/tools/gen_perf_boot.py`` (the pacing/budget constants parsed
   from ``components/ui/Kconfig`` at figure time; the capture windows
   quoted beside it).

.. _cache-footprint-perf:

The hour-cache footprint
------------------------

**The short version.** The fourth resource axis is not time, rate or
brightness but **capacity**: what one hour of moon costs the codec
while it flows, and what a full card month costs the card. The cache
is capped at ``TF_MAX_HOURS`` = **709** files — one synodic month of
hourly keys with slack — so the honest worst case is bounded by the
encoder's own out-buffer formula, ``S² + S²/2 + 1024``, which the code
calls "the max a valid cached file can be". Nothing here was measured
on a card: the real JPEGs at the default ``UI_MOON_TFCACHE_JPEG_Q`` =
90 are far smaller than the bound and instrumented nowhere; the figure
states the bound and stops.

- The **codec scratch** is per frame, not per card: the resident pair
  the boot log prints ("ready: ... scratch=NN KB") is ``s_enc_in``
  (GRAY probe, ``S²``) plus ``s_enc_out`` (the out-capacity above);
  the two big ones — ``s_fetch_in`` (sized to the same out-capacity)
  and ``s_dec_rgb`` (3·S² RGB888) — are allocated **lazily** since
  task #38, because on the S3 the eager ``3·S²`` decode buffer starved
  the 8 MB board's slot floor and silently killed the card path *every*
  boot (:doc:`the animation pipelines <../implementation/animations>`,
  the task #38 line — a cold folder never decodes, so the memory was
  dead weight on exactly the board that could least spare it).
- A **full card month** is 709 files at each board's own resolution:
  the raw decoded payload is **108 MiB** (S3, disk 400) against
  **286 MiB** (P4, disk 650); the on-card worst case — every file at
  the encoder bound — is **163 MiB** against **429 MiB**, both far
  under any plausible card. Eviction keeps the folder honest to its
  name: nothing older than the *newer* of two floors survives — one
  synodic month back, and the new-moon floor the producers set
  (:doc:`the animation pipelines <../implementation/animations>`,
  *The TF hour cache*).
- The knob rides along as the violet footnote: no SD socket, **never
  stores** — every frame is the bake path (the boot ladder's always-
  black pair). The whole subsystem exists only with
  ``UI_MOON_TFCACHE=y`` (the default), so the figure is the default
  build. How often the card is written at all — 48 media operations a
  day on the clock boards, zero on the knob — is the churn axis of
  :ref:`the write ladder <write-ladder-perf>` below; this figure
  prices what ONE card month costs, that one how OFTEN.

.. figure:: ../_static/figs/perf_cache.png
   :alt: hour-cache footprint — per-frame codec scratch and the bounded card month, per board
   :figclass: align-center

   The capacity axis. **Left:** one hour's frame through the codec —
   the resident encode pair (the sizes the boot log prints) against
   the hatched lazy pair (fetch input, decode out) that task #38 made
   conditional, KiB linear, per board. **Right:** a full card month of
   709 hourly files — the raw decoded payload (hatched, what they
   decode *to*) against the on-card worst case at the encoder's own
   file bound (solid), MiB log; the real JPEGs q90 are far smaller,
   never measured. S3 blue, P4 vermillion; the knob never reaches the
   plot — no SD socket, the muted footer says so. The
   generator is ``docs/tools/gen_perf_cache.py`` — cap, synodic window,
   out-capacity formula, decode-buffer comment, ``MOON_PX`` pair and
   the ``UI_MOON_TFCACHE``/``JPEG_Q`` Kconfig values are **parsed from
   the live sources** at figure time; the card itself is only bounded,
   never touched.

The interactive pacing ladder
-----------------------------

**The short version.** The other performance axis is not how long an
event costs but how often the device wakes *itself*. Nine timers carry
the product: the 10 ms LVGL adapter park and UI tick, the 50 ms bake
reveal tick, the two Kconfig-paced moon rates (125 ms replay floor,
240 ms fade legs), the 1 Hz idle trio (idle check, tilt monitor,
seconds increment) and the 1300 ms RTC interrupt poll. The two shared
rhythms are deliberate — the adapter park *is* the UI tick's floor,
and the sleep ladder beats at one heart — everything else holds its
own slot, spanning two orders of magnitude from 10 ms to 1300 ms.
Every value is **parsed from the live sources** at figure time —
including the ``UI_MOON_HOURLY_FADE_MS → UI_SCREEN_SWITCH_FADE``
symbol indirection (resolved exactly like Kconfig does) — so the
ladder cannot drift from the firmware.

.. figure:: ../_static/figs/perf_pacing.png
   :alt: the device's self-wake periods on one log axis, and the same rows as events per second
   :figclass: align-center

   The pacing ladder. **Left:** every shipped timer period on one log
   millisecond axis, colour-coded by subsystem (blue = LVGL-side,
   violet = moon pipeline, amber = the idle/sleep ladder, red = the
   RTC poll), each row annotated with its source provenance (hatched
   = Kconfig-paced). **Right:** the same rows as events per second —
   derived (1000 / period), nothing new measured: the adapter parks
   the LVGL task at 100 Hz, the RTC poll runs below 1 Hz, and the
   reveal tick lands at exactly the 20 Hz the bake-reveal design
   specifies. The generator is ``docs/tools/gen_perf_pacing.py``.

.. _patience-ladder-perf:

The patience ladder (how long a wait is allowed)
------------------------------------------------

**The short version.** The pacing ladder measures the device's heart;
this one measures its **patience** — the other side of the same
currency. Where the pacing rows are *periods* (things that repeat),
these rows are *deadlines*: one-shot waits the firmware sits in
before it acts. Eleven of them carry the product, spanning 1 s to
240 s: the drain witness gating on the panel mutex (**2 s**), the
panel-io watchdog owning the true-stall case (**3 s**), the traffic
hold's tail (**4 s**) behind its cap (**15 s**), the deliberate
orientation press (**2 s**), the scan-retry gap (**1 s × 4
attempts**), the two NTP fix windows (**10 s** clock, **20 s** knob),
the knob's join wait (**15 s**) — and the two budgets the hourly and
boot ladders measure against (**5 s**, **240 s**). Every value is
**parsed from the live sources** at figure time; nothing is measured
on the bench, because none of these needs to be — they are the
firmware's own promises.

The figure's real content is the *pairing*: the drain-witness gate
and the orientation press are both 2 s with opposite intent — one is
a guard giving up on a lost GPSPI completion so LVGL proceeds (the
function's own log line: "bus still busy >2000 ms (lost trans-done
hand …)"), the other is a *deliberate* hold the user times by feel;
and the 2 s/3 s guard pair is exactly the split the flush path's
design keeps honest (``moon_flush_wait_cb``'s comment: the 2 s cap
"only fires on a genuinely stuck bus … the panel-io watchdog keeps
owning the true-stall case: still ``s_panel_mtx``-stuck, still 3 s" —
:doc:`the display pipeline <../implementation/display>`,
``flush-path.dot``). The radio rows are the same waits the
:doc:`clock machine <../implementation/clock-sleep>` shows as trust
raisers: the boot fix window is the 10 s spent *inside* ``clock_init``
before the panel ever wakes, and the knob's 20 s fix window is what
the "last known" NVS arming races against.

.. figure:: ../_static/figs/perf_guards.png
   :alt: every one-shot wait in the firmware on one log axis, and the
       same rows as multiples of the 5 s hourly producer budget
   :figclass: align-center

   The patience ladder. **Left:** every shipped deadline on one log
   millisecond axis, colour-coded by family (red = the panel-bus
   guards, amber = the human press, teal = the radio's fix windows,
   violet = the two budgets; hatched = Kconfig-paced), each row
   annotated with its source provenance. **Right:** the same rows as
   multiples of ``HOURLY_BUDGET_MS`` — derived (one division against
   the parsed constant), nothing new measured: the bus guards are
   all *fractions* of a producer cycle (0.2–0.8×; the worst stall
   response costs less than one hour's refresh), the two budgets sit
   at their identities (1×, 48×), and the radio waits — 2× to 4× —
   are why the syncs run on background tasks instead of the UI tick
   (:doc:`the wake-freeze fix <../implementation/clock-sleep>`,
   *The wake paths*). The generator is
   ``docs/tools/gen_perf_guards.py``.

The brightness ladder (ramp vs single write)
--------------------------------------------

The third axis the sleep machine lives on is neither work nor
frequency but the **panel's hardware brightness**: every state the
display settles in is written through the one 0x51 family (the
sanctioned ramp exception in :doc:`the display pipeline
<../implementation/display>`), and the reader who knows the two
rhythm families knows the whole sleep cost model. Same house rule as
the pacing ladder — every value **parsed from the live sources** at
figure time, including the ``UI_MOON_HOURLY_FADE_MS →
UI_SCREEN_SWITCH_FADE`` symbol indirection:

.. figure:: ../_static/figs/perf_dim.png
   :alt: the brightness timeline with its 0x51 ramp steps, and time-to-target per transition on a log axis
   :figclass: align-center

   The brightness ladder. **Left:** the timeline awake → light sleep →
   deep sleep → wake, plus the hourly refresh riding the same family
   — the dim to ``UI_LIGHT_SLEEP_DIM_PCT`` (25 %) and the wake ramp
   back are ``UI_SCREEN_SWITCH_FADE`` (240 ms) in ``FADE_STEPS`` (12)
   discrete 0x51 writes 20 ms apart (dots on the ramps); the
   light-sleep clock is *not* a panel write at all — it is the clock
   container's ``UI_CLOCK_ALPHA_PCT`` (20 %) LVGL style opacity over
   the already-dimmed panel; the deep entry is a SINGLE 0x51
   (``moon_ui_panel_brightness_set(0)``, ~100 µs — one transaction per
   ``fade_ramp``'s own comment, annotated not measured), and the two
   hourly legs bracket the produce window whose duration is the
   hourly-refresh figure's story, above. The dim ramp starts from the panel's
   *tracked* level (``s_panel_pct``), which is the #34 rule: a
   re-entry never flashes brighter first. **Right:** time-to-target per
   transition on one log axis — the three ramps share the parsed 240
   ms, the two single writes sit at the one-transaction floor, a
   2400×-spread that is the design: brightness animates, pixels never
   half-move. The generator is ``docs/tools/gen_perf_dim.py``.

.. _static-footprint-perf:

The static footprint (how much memory)
---------------------------------------

The fourth axis is neither work, frequency nor brightness but
**bytes**: the same three resources whose ceilings caused both NO_MEM
families (the 32 KiB SPI-DMA staging cap, the 64 KiB LVGL pool that a
panel-sized rotated layer does not fit). Every bar is **parsed or
derived from the live sources at figure time** — the allocators and
``MOON_PX`` from ``components/ui/moon_ui.c``, the 25-row double buffer
from the vendored S3 BSP, the task stacks from their ``xTaskCreate``
sites, the LVGL pool default from the lvgl Kconfig (unpinned in every
``sdkconfig``), the flash assets from the three manifests — so figure
and sources are one chain, like the pacing and brightness ladders.
Runtime watermarks (free heap, largest block) are the device's own
``heap internal/psram`` boot lines, deliberately not repeated here.

.. figure:: ../_static/figs/perf_mem.png
   :alt: three panels — PSRAM working set per board, internal-RAM commitments, flash moon assets vs their partition
   :figclass: align-center

   The static footprint. **Left:** the PSRAM working set — the clocks
   carry the three moon buffers (``s_frame`` gray + ``s_rgb`` RGB565 +
   the hourly bounce ``s_hourly_gray``, sizes ``MOON_PX² × {1,2,1}`` —
   P4 pays 2.6× S3 for the 650 px disk), plus the draw buffers, plus
   (hatched) the rotated root's panel-sized layer that exists only
   while rotated and comes from the clib malloc precisely because the
   64 KiB LVGL pool cannot hold it; the knob instead double-buffers
   the full 360² square (``moon_ui.c`` is *excluded* from the knob
   build — its working set lives in ``knob_ui``). **Middle:** the
   internal-RAM commitments — the static LVGL pool and the task
   stacks; the two ``MALLOC_CAP_SPIRAM`` stack tasks are dotted, not
   RAM debt. **Right:** the flash moon assets against their own
   partition rows — the S3's 2 MiB DEM pushes the combined image to
   94 % of its 2,560 KiB partition, the P4's 4 MiB combined sits at
   80 %, the knob's half-axis DEM (1024×512) leaves it at 34 %
   (headroom the asset pipeline's size guards keep as headroom). The
   generator is ``docs/tools/gen_perf_mem.py``.

.. _loudness-ladder-perf:

The ring's loudness (the alarm audio ladder)
---------------------------------------------

The fifth axis is neither work, frequency, brightness nor bytes but
**volume**: what the ringing state costs the ear and the bus. Like the
brightness ladder, every value is **parsed from the live sources at
figure time** — the eight ``#define``s of :cfile:`components/ui/alarm_audio.c`,
the header's ``ALARM_SOUND_COUNT``, the stop fade's ``vol -= 5`` step,
the four ``loop_gen_all`` frequency arrays and ``ALARM_NAMES`` — and
the two pacing numbers (the ~46 ms chunk, the ~33 ms level spacing)
are plain divisions of parsed constants; nothing here was measured on
the bench (the ring is heard, not instrumented). The dB tick labels
are the ``RING_VOL_FULL`` comment's own curve line, read literally:

.. figure:: ../_static/figs/perf_audio.png
   :alt: the ring and preview volume timelines with chunk ticks, and the four baked loops drawn as frequency scores
   :figclass: align-center

   The loudness ladder. **Left:** one player task, two flows — the
   ring ramp 0 → ``RING_VOL_FULL`` (75) over ``RING_RAMP_MS`` (2500 ms)
   carries a dot at every ``CHUNK_SAMPLES`` (1024) blocking
   ``esp_codec_dev_write`` boundary: the write *is* the metronome
   (``portMAX_DELAY``, self-pacing), which is why the ramp needs no
   timer at all — its volume is *computed* from
   ``xTaskGetTickCount()`` against ``ramp_ms`` each chunk, a new
   integer level only ~33 ms apart. The dashed hold is the loop until
   touch (one ``LOOP_MS`` lap per pass, duration deliberately
   schematic); the stop fade is the code's exact −5-per-10 ms
   staircase (15 steps ≈ 150 ms) riding the drained DMA on silence,
   then ``esp_codec_dev_close``. The preview is the same machine with
   ``PREV_RAMP_MS`` (300) and *exactly one* pass — looping is the
   ring's only extra. **Right:** the four baked loops as scores over
   one 1000 ms lap, frequencies and timings read out of
   ``loop_gen_all`` — the alternating A5/E5 two-tone, three 200 ms
   bursts of the 2 kHz square, the 880 Hz bell with its four
   amplitude-weighted partials under a 350 ms exponential decay, the
   C5–E5–G5–C6 arpeggio — baked once at init into
   4 × 22 050 samples × 2 B = 172 KiB PSRAM (the boot log's own
   line). On the knob the whole ladder collapses to silence: same
   header, four "Silent" names, no codec to open
   (:doc:`the knob <../devices/knob>`). The generator is
   ``docs/tools/gen_perf_audio.py``; the state machine around it is
   sketched in :doc:`the alarm screen <../ui/alarm>`.

The device CPU (one-core shares)
--------------------------------

.. figure:: ../_static/figs/perf_cpu.png
   :alt: Horizontal bars of measured one-core CPU shares
   :figclass: align-center

   On-device CPU shares from the ``CONFIG_FREERTOS_HZ`` probe captures
   (the task table in :doc:`the architecture <../implementation/architecture>`). The
   hub-idle bar carries its measured 7–9 % range.

The budget's shape is the architecture's shape: the *bake* is on a
dedicated core (core 1, the bake tasks — the spin task retired
2026-11-13 with its subsystem) and the *UI* is on
core 0 (LVGL + touch + clock) — a bake that starves the UI is a
design bug, not a timing accident. The two-core split is what let
the sweep run for ~37 min (S3, native 400 px intermediate bake,
2026-11-08 capture; ~11 min if ``UI_MOON_ANIM_BAKE_PX`` was dropped to
240) — that was the *card-less* path; with the TF cache mounted the
same sweep replayed from the card in ~2 min warm (:doc:`the
animation pipelines <../implementation/animations>`; the sweep
retired 2026-11-13). Either way
the touch pipeline never sees a bake. The hub idle sits at 7–9 % because the LVGL adapter's
``task_min_delay_ms`` floor parks the loop at ~93 Hz — the pre-fix
3500 Hz spin at 73–91 % was the ``vTaskDelay(0)`` yield, recorded in
:doc:`the architecture <../implementation/architecture>`.

.. _scheduler-ladder-perf:

The scheduler ladder (who outranks whom)
----------------------------------------

One resource axis had never been plotted: not how much work, how
often, how bright, how many bytes, how loud or how patient, but
**priority** — when two tasks want the CPU at the same instant, which
one runs. Every dot is an ``xTaskCreate*`` call site parsed at figure
time (stack, priority expression, pinned core, stack caps) against the
kernel's own constants: ``configMAX_PRIORITIES`` (25) from the IDF
FreeRTOSConfig, the adapter's LVGL priority/stack/core triple from
``esp_lv_adapter.h``, the P4's stack override straight out of
:cfile:`main/main.cpp` (with its line number), and the WDT and
timer-daemon baselines from **both** generated ``sdkconfig.h`` files —
clocks and knob build dirs — so the two headers' disagreement is
plotted too: the clocks leave CPU1's idle unwatched, the knob checks
it with panic.

The comparison the ladder makes visible is the render-path inversion
between the two families:

- **The clocks put the producers above the renderer.** The three bake
  producers (re-bake, hourly, boot replay) sit at
  ``configMAX_PRIORITIES − 3`` = 22, pinned to core 1 — sixteen levels
  above the LVGL worker's 6 — because the while-black hand-off lets
  the renderer wait: the producer rebuilds the frame off-panel while
  the panel is black and the finished frame lands as one atomic flush
  (:doc:`the animation pipelines <../implementation/animations>`). A
  bake that an LVGL redraw could starve is the bug class this band
  prevents. The sky-event worker takes the band below — "below the
  bake tasks in priority", the code's own comment — and relies on the
  same parsed fact its comment cites: IDLE1 is not WDT-watched on the
  clocks, so its 15–20 s scan can never trip the watchdog.
- **The knob puts the renderer above its producer.** ``knob_lvgl`` (10)
  outranks ``knob_swap`` (7) outranks ``knob_moon`` (5): the producer
  fills a back buffer nobody is waiting on, the swap task
  edge-detects the watermark and publishes, and the renderer is the
  foreground (:doc:`the knob display <../implementation/knob-display>`).
  The inversion is the sign flip on the right panel. It also lets the
  knob keep the producer deliberately **unregistered** from the TWDT
  (dotted edge — "its work never feeds the watchdog", the code's
  comment) while the swap stays a plain registered task ("a watchdog
  on it is a feature") — and precisely because the CPU1 idle check is
  ON with panic there, an unyielding scan still aborts the boot.
- **The radio tier is shared code, so the tie is shared too.**
  ``wifi_sync`` is priority 10 from the one :cfile:`components/ui/actions.c`
  call site on all three boards — tied with the knob's renderer (co-equal,
  time-sliced) but preempting the clocks' LVGL worker (+4), which is
  why the wake path runs the sync on that background task instead of
  on the UI tick. The scan (5) sits below both renderers on both
  families.
- **The guard tier always runs, never disturbs.** The panel-io watchdog
  (3, pinned core 0), the alarm ring player (4), the two dial tasks and
  the power supervisor (4–5) all outrank IDLE (0) but rank below every
  renderer — they can always preempt the idles (which is what the
  parsed 5 s TWDT baselines watch), yet a ring, a turn of the dial or a
  stuck-bus probe never delays a frame.
- **One band the app does not own.** The ``esp_timer`` daemon shares
  the producer band by definition, not coincidence —
  ``ESP_TASK_TIMER_PRIO`` is literally ``(ESP_TASK_PRIO_MAX - 3)`` —
  the shaded band's only non-app resident.

Right panel: the same rows as headroom over each family's own LVGL
task (one subtraction, nothing new measured). Not drawn: the
``hang_watch`` probe (its ``xTaskCreate`` sits under
``#if CONFIG_TEST_HANG_WATCH`` — a test hook, never shipped) and the
knob's alarm, which is its silent stub with no player task at all. The
generator is ``docs/tools/gen_perf_sched.py``; the same task set in
prose is the task table of :doc:`the architecture
<../implementation/architecture>`, the same task set *wired* — who
spawns whom, who wakes whom, which chip each hand asks — is
:ref:`the crew sketch <task-roster-arch>`, the memory each row
commits is :ref:`the static footprint <static-footprint-perf>` above,
and the locks those hands queue behind — two keys on the clocks, one
reentrant-wrapped key on the knob, zero queues anywhere — are
:ref:`the guard ladder <guard-ladder-perf>` and its twin
:ref:`the guards sketch <guards-arch>`.

.. figure:: ../_static/figs/perf_sched.png
   :alt: The FreeRTOS priority ladder of every shipped task on the clocks and the knob, and the same rows as headroom over each family's LVGL task
   :figclass: align-center

   The scheduler ladder. **Left:** every shipped task as one row
   sorted by priority (``pNN`` prefix), clocks on the left of the
   gutter and knob on the right — stack, pinned core, parsed provenance
   per row, the producer band shaded violet (its height is
   ``configMAX_PRIORITIES − 3``, shared with the ``esp_timer`` daemon
   by definition), the knob's producer drawn with a dotted edge for its
   deliberate TWDT non-registration, the IDLE row carrying both
   boards' parsed WDT posture. **Right:** the same rows as headroom
   over each family's own LVGL task — the clocks' producers clear the
   renderer by +16, the knob's by −5, the inversion one subtraction.

The response ladder (how soon the user is noticed)
--------------------------------------------------

**The short version.** The pacing ladder plots the device's own
heartbeat, the patience ladder its allowed waits — this third column
of the same currency family plots the **human side**: how quickly a
finger, a tilt or a dial detent becomes a visible change. Ten rows
span 3 ms to 2 s on one log millisecond axis, every value parsed from
the live sources at figure time: the knob encoder's ``ENC_POLL_MS``
poll (with the honest tick note — ``pdMS_TO_TICKS(3)`` lands at one
tick, 10 ms, at the knob's parsed ``CONFIG_FREERTOS_HZ``), the
debounce pair behind it, the 20 ms ``dial_task``, the one
``CONFIG_LV_DEF_REFR_PERIOD`` read timer every touch path shares,
the 10 ms UI tick, the 2 × ``UI_SCREEN_SWITCH_FADE`` navigation
round-trip, and the three human-scale rows.

The figure's real content is a *two-way race between input paths*.
The knob's dedicated encoder chain — poll, ``ENC_DEBOUNCE`` 2-tick
streak, ``dial_task`` consume, 33 ms repaint — is the **only** input
path in the family that is faster than the LVGL read timer: every
touch on both clocks is *detected* no sooner than
``CONFIG_LV_DEF_REFR_PERIOD``, parsed here from **all three**
generated headers and asserted equal (the one-beat claim of
``input-chain.dot``, re-verified at figure time; on the P4 that
timer is the whole story — the GT911's INT is NC and nothing wakes
it early). And the three teal rows are the same 2000 ms with three
opposite intents: the 90° step's visible cost is the code's own
sentence — "the 1 s window lags the step changes by ~1 s, and the
sustained confirmation needs a second ~1 s on top" (the row is
derived as ``2 × ORI_SAMPLE_MS × ORI_WINDOW``) — the wake's is two
consecutive above-bar samples at the 1 Hz monitor, and the press is
the one wait the *user* owns (the patience ladder's same constant,
drawn here with human intent). Right panel: the same rows as
multiples of the beat (one division, nothing new measured) — the
encoder chain clears it at 0.09–0.61×, the beat row lands at 1× by
definition, navigation pays 15× for its fade round-trip, and every
human confirmation sits at 61×: **no human ever waits on the beat,
the machine always waits on the human**. The mechanism — who reads
and how — is :ref:`the input chain <input-chain-ui>`; the same legs
counted as **asks per day** instead of wait time are :ref:`the
silicon ladder <silicon-ladder-perf>` below; the
``docs/tools/gen_perf_response.py`` generator draws both panels.

.. figure:: ../_static/figs/perf_response.png
   :alt: every detection-to-visible-response latency on one log axis, and the same rows as multiples of the 33 ms LVGL read timer
   :figclass: align-center

   The response ladder. **Left:** every shipped input path as its
   detection-to-visible-response latency on one log millisecond axis,
   colour-coded by family (amber = the knob's dedicated encoder path,
   blue = the shared LVGL beats, violet = the fade round-trip, teal =
   the human-scale holds), hatched = Kconfig-paced, each row annotated
   with its parsed provenance. **Right:** the same rows as multiples
   of ``CONFIG_LV_DEF_REFR_PERIOD`` — derived (one division), nothing
   new measured: the knob's chain beats the beat, everything else
   rides it, and the human holds tower 61× over it by design.

.. _bus-ladder-perf:

The bus ladder (how many bytes cross the wire)
----------------------------------------------

**The short version.** The remaining resource after work, frequency,
patience, brightness, memory, loudness, priority and human latency is
**bytes** — what one panel update actually pushes across the display
bus. Nine rows, three per board: the full RGB565 frame the atomic
``lv_refr_now`` crosses (S3 434,312 B, P4 1,036,800 B, knob 259,200 B),
the hourly disk-only flush (``MOON_PX``² × 2 B — 320,000 B against the
P4's 845,000, and *no such row* on the knob, which has no SD socket),
and the ONE DMA transaction the bus really sees (23,300 / 72,000 /
21,600 B). Right panel: the raw wire floor the QSPI x4 panels pay —
4 lanes × parsed 40 MHz ÷ 8 = 20 MB/s, giving the S3's whole panel
21.7 ms and the knob's 13.0 ms against the 33 ms beat parsed and
asserted equal in all three generated headers. The P4 row is
deliberately **not plotted**: its MIPI-DSI lane rate lives in the
target-gated XC BSP that is absent from the shared
``managed_components/`` tree, and inventing a number there would be
the one way to make this figure lie.

The figure's real content is the *cap line*. Both QSPI boards'
transactions land under the family's 32 KiB SPI-DMA cap
(``SPI_LL_DMA_MAX_BIT_LEN = 1<<18``, the vendored BSP's own comment at
``esp32_s3_touch_amoled_1_75.c:522``) and do so **exactly**: the S3's
``max_transfer_sz`` is the literal ``BSP_LCD_H_RES * 25 *
BSP_LCD_BITS_PER_PIXEL / 8`` (``:501``) with ``buffer_height`` the same
25 (``:530`` — the generator asserts the two parses agree, which *is*
the "one strip = one buffer = one QSPI transfer" fact of the NO_MEM
fix), and the knob's ``max_transfer_sz`` formula (``knob_board_display.c:243``)
carries the clock board's lesson in its own comment. The P4's 72,000 B
DPI transaction — the cited 50-row buffer (``moon_ui.c:193``, hatched)
— towers over the line: a cap its bus never has, which is exactly why
its 130-row strip height is "a pacing choice, not a hard limit"
(``moon_ui.c:191``'s own words). Every parsed constant carries its
``file:line`` as a bar annotation; the per-transaction command overhead
the raw floor excludes is the vendored BSP's own "*only* the
per-transaction command overhead goes up slightly". The bytes the
*hour cache* occupies on the card are the footprint figure above —
this one counts only the bytes that *move*. The staging geometry the
transaction rows describe is drawn in
:doc:`the display path <../implementation/display>`
(``docs/tools/gen_perf_bus.py`` draws both panels; the strip anatomy
shares its constants with ``gen_strip_staging.py``). The pixels those
bytes carry — the same bars counted in the currency the O(N) statements
speak — are :ref:`the pixel ladder <pixel-ladder-perf>` just below, and
the same bytes at rest in their partition rows are :ref:`the storage
ladder <storage-ladder-perf>`.

.. figure:: ../_static/figs/perf_bus.png
   :alt: bytes per panel refresh for all three boards — full frame, hourly disk flush, one DMA transaction — against the 32 KiB SPI-DMA cap, and the raw QSPI wire time of one full frame against the 33 ms LVGL beat
   :figclass: align-center

   The bus ladder. **Left:** the three rows per board in KiB —
   blue the full atomic frame, violet the hourly disk-only flush, teal
   one DMA transaction — with the 32 KiB SPI-DMA cap as a dashed rule
   both QSPI transactions fit under exactly and the P4's DPI
   transaction deliberately does not; hatched = cited (the absent XC
   BSP), italic = the knob's honest *no such leg*. **Right:** the raw
   wire floor (bytes ÷ lanes·pclk/8, derived, nothing new measured)
   against the 33 ms beat — the S3 spends two-thirds of one read-timer
   beat on its whole panel, the knob a third.

.. _pixel-ladder-perf:

The pixel ladder (how many pixels every stage touches)
-------------------------------------------------------

**The short version.** The bus ladder above counts the bytes that
*move*; this counts the pixels they move *for* — the counting-work axis
the page's complexity section has priced everything in all along
(every per-pixel step is O(N); here is N, drawn). A battery of
SystemExit-failing parses feeds the figure, like the rest of the
family:
the ``MOON_PX`` pair with the ``MOON_R = MOON_PX/2`` shape, the
strip/reveal geometry (``MOON_STRIP_ROWS`` 25/130, ``MOON_REVEAL_ROWS``
20/26 — divisibility re-asserted, the code's own ``#error`` guards),
the P4's CITED 50-row DPI buffer and the knob's one-row reveal rule
(read out of the comments that state them), the panel sizes from the
vendored BSP header / the generated
``CONFIG_BSP_LCD_TYPE_720_720_4_INCH`` / the knob shim, the
``max_transfer_sz`` formulas parsed out of the vendored sources (the
bus generator's strip-equality lesson re-applied: the S3's
``max_transfer_sz`` rows must equal its ``buffer_height``), the knob's
``MOON_SIZE = KNOB_LCD_H_RES`` — its bake square IS its panel — with
its ``s_buf[2]`` double buffer and its shared-home disk geometry
parsed, not recalled, and all three generated ``moon_consts.h`` with
the albedo asserted 2:1 equirectangular. The one-DMA-transaction rows
are the bus ladder's teal bars ÷ 2 — same parses, different currency;
the same buffers in *bytes* under their *locks* are the relay sketch
(``docs/_graphs/buffers-relay.dot``) in
:doc:`the architecture <../implementation/architecture>`.

Three counting facts the byte view cannot show:

- **The flush outruns the bake.** The whole-panel screen-switch flush
  touches 1.36× (S3) / 1.23× (P4) the pixels the bake just computed —
  the clock row, alarm row and seconds arc ride every moon across the
  bus. On the knob the ratio is 1.00 by construction (``MOON_SIZE`` is
  the panel: the moon square is the screen).
- **The hourly refresh is the flush minus the furniture**: its violet
  bar is exactly the bake square (the bus ladder's 320,000/845,000 B
  ÷ 2 — the while-black ``lv_refr_now`` commits the disk, not the
  screen), the disk-only geometry that makes an hourly re-light of
  the moon affordable every hour.
- **The relief wall is the asset-to-work ratio**: 13.1× (S3) / 5.0×
  (P4) / 4.0× (knob) the bake square of packed relief cells stand
  behind every frame — yet exactly ONE cell is read per bake pixel,
  and that one-load-per-pixel is precisely what ranked decision 1
  priced when it bought the relief gradient cache.

.. figure:: ../_static/figs/perf_pixels.png
   :alt: pixels per stage per board — bake square, gather, hourly disk flush, whole-panel flush, one DMA transaction, one reveal state — and the pixel universe of albedo, relief pack, bake square, panel
   :figclass: align-center

   The pixel ladder. **Left:** one published frame per board — the
   bake square, the gray→RGB565 gather, the hourly disk flush (the bus
   ladder's violet bar ÷ 2), the whole-panel flush with the moon
   image's share of it annotated, the one DMA transaction the bus
   really sees, and one reveal *state* (dashed: 8,000 px per band on
   the S3, 16,900 on the P4, ONE row on the knob); hatched = cited
   DPI buffer, italic = the knob's two honest *no such legs* — its
   bake writes RGB565 straight into the back buffer and it owns no
   card. **Right:** the pixel universe each bake samples from —
   albedo texels, the relief pack with its ×-the-bake factor, the
   bake square, the panel. S3 blue, P4 vermillion, knob violet; the
   generator is ``docs/tools/gen_perf_pixels.py``.

The *time* these counts cost is the head of this page, the *fineness*
of the sources they gather from is the :ref:`resolution funnel
<sharpness-funnel-perf>` below, and the *bytes* they become on the
wire are the bus ladder above. Where those bytes **live** — the
partition rows the blobs and the app sit in, counted against their
reservations — is the storage ladder next.

.. _storage-ladder-perf:

The storage ladder (how many bytes lie in the flash)
----------------------------------------------------

**The short version.** The binary page owns the *app* side of the
flash budget (:doc:`the binary chronology <binary>`); this is its
missing half — the **data** side, row by row, every number parsed at
figure time from the three partition CSVs, the three asset manifests,
the on-disk ``moon_assets.bin`` blobs, the generated
``moon_consts.h`` headers, the two clock ``sdkconfig.h`` files and the
``spiffs/`` payload. Ten occupancy bars on the left (fraction of each
reservation used); on the right, the ``moon_assets`` row — the
product's tightest budget — with the three what-ifs its CSV comments
keep. The discipline is the family's: nothing is recalled, and the
CSV comments are **re-derived, not trusted** — the layout rule
(u8 albedo, int8 DEM) re-checks every byte figure they print. That
cross-check earned its keep this very pass: the P4 comment claimed a
2560-wide albedo was "2,228,224 B", while the product of its own
numbers (5,373,952 total − 2,097,152 DEM) is 3,276,800 — the comment
was wrong and is fixed; the generator now refuses to draw until
comment and arithmetic agree again.

.. figure:: ../_static/figs/perf_storage.png
   :alt: occupancy of every flash data row per board (factory app, stacked albedo+DEM moon_assets, spiffs storage, reserved coredump) and the moon_assets row shipped plus three what-ifs — S3 at albedo 1024 fills the row to the byte, P4 at 2560-wide needs 5248K over its 5120K row, the knob with the clock's proven 2048x1024 DEM equals the S3's shipped blob
   :figclass: align-center

   **Left:** what each data row actually holds — the live binary in
   ``factory`` (same files the drift gate checks, so binary.rst, the
   table there and this bar are one chain), the ``moon_assets`` blob
   stacked albedo (amber) + int8 DEM (orange), the 956-byte spiffs
   payload in ``storage`` (the tracked ``alarm.txt`` +
   ``wifi_credentials.example.txt`` plus the gitignored
   ``wifi_credentials.txt`` the image really bakes — 940 B on a clean
   checkout; ``settings.txt`` is asserted **absent**, the app owns it
   at runtime), and the knob's 64 K ``coredump`` ring drawn italic
   *no bytes until a crash* (the space is reserved, not filled — the
   bus ladder's honest *no such leg* precedent).
   **Right:** the ``moon_assets`` row against its ceiling tick in the
   board's colour — three shipped blobs and three what-ifs. Solid =
   computed from the parsed layout; hatched = cited from the CSV's
   own comment (and re-derived before drawing). S3 blue, P4
   vermillion, knob violet; the generator is
   ``docs/tools/gen_perf_storage.py``, whose parsers also assert the
   root's ``FATAL_ERROR`` trio and the knob's replicant still *exist*
   in the two ``CMakeLists.txt`` files — the figure is the docs-side
   twin of the build-time guards and dies if one is removed.

Three counting facts the app-side budget cannot show:

- **The tightest flash budget in the product is not the app.** The
  S3 ``moon_assets`` row runs at **94.2 %** — 151,040 B free under a
  2,621,440 B ceiling — while the S3 app keeps its (now-famous) 19 %.
  And the headroom is exactly the price of the next albedo width:
  1024×512 − 864×432 = 524,288 − 373,248 = 151,040 B, so the next
  width in the root guard's own even-width set fills the row **to the
  byte** (asserted equal at figure time; the guard's fit check then
  becomes the hair trigger).
- **The DEM is the floor under every board.** The S3's blob is 85 %
  relief (2,097,152 of 2,470,400 B — the wall that made the 16 MB
  chip a *requirement*, binary.rst's chronology), the P4's is a
  50/50 split (2048×1024 albedo *and* DEM), and the knob's DEM is
  halved to 1024×512 (524,288 B). One signed byte per grid cell is
  what makes the whole flash layout close.
- **The knob's row is pre-sized to the clock's proven bake.** Its own
  864×432 albedo plus the clock's 2048×1024 DEM would weigh exactly
  2,470,400 B — the S3's shipped blob to the byte — which is the
  whole content of the CSV's "2560K is sized for the proven
  2048x1024-DEM bake too, so the table never has to change" (a
  partition change means a full flash on the bench unit, the
  comment's own reason; re-derived before the figure trusts it).

The chip reservations close the ledger: S3 4736 K of 16 MB (the
comment's "Total = 4736K", re-derived from the rows, ~11 MB unused —
the headroom binary.rst's widening rule lives on), P4 14464 K of
32 MB, knob 7296 K of 16 MB with the CSV's "~8.9 MB free" before the
coredump row and "~8.8 MB" after it (both re-derived). The
two amber/orange segments are the same bytes the sharpness funnel
samples per pixel and the pixel ladder's relief wall unpacks from at
boot; the hour cache, by contrast, stores *baked output* on the TF
card — a derived copy of what these rows make, not another resident
copy of them. How OFTEN the firmware rewrites these same media — the
churn axis, from a 241,024-byte encoder bound to an 8-byte NVS commit
— is :ref:`the write ladder <write-ladder-perf>` below.

.. _write-ladder-perf:

The write ladder (how often the media are written, and how big one write is)
----------------------------------------------------------------------------

**The short version.** The storage ladder counts bytes that *lie* in
the flash, the hour-cache footprint counts *capacity*, the bus ladder
counts bytes the display *moves* — none of them counts how OFTEN the
media are written. This ladder counts scheduled flash writes per day
per board and the blast radius of a single write across the four
media the firmware owns (TF card, NVS, spiffs, coredump partition).
Nothing was measured on a card or in NVS: every number is parsed from
the live sources or arithmetic on parsed numbers, and the generator's
parsers die when a source drifts.

- **The hourly producer is the churn: 48 media operations a day.**
  ``UI_MOON_HOURLY_ENABLE`` / ``UI_MOON_HOURLY_TFCACHE`` /
  ``UI_MOON_TFCACHE`` are all ``default y``
  (``components/ui/Kconfig:137/:169/:271``), so the steady state is one
  ``tfcache_store`` write-through per new hour
  (``components/ui/moon_ui.c:1614`` — the ONLY store call site, and
  the parser asserts it sits inside its
  ``#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE``
  guard) plus ~24 eviction unlinks a day once the 709-file window is
  full: ``cache_floor`` (``moon_tfcache.c:693``) walks one hour per
  hour, so one file per hour falls below it, and the producers call
  ``tfcache_evict`` every hour (``moon_ui.c:1587/:1615``; the store's
  own ``> 24``-hour gate, ``moon_tfcache.c:684`` — "one scan per day
  of new hours" — just guarantees the scan even when the producers
  idle). ``meta.txt`` rewrites ride the new-moon crossing — **one per
  synodic month** — because ``tfcache_set_new_moon_floor``
  early-returns on an unchanged floor, its own comment: "the meta
  rewrite only ever fires on the new-moon crossing"; the second
  ``meta_write`` site fires once per fresh/wiped folder.
- **The most frequent write is the cheapest — and the RTC decides who
  pays it.** The 15-minute NVS epoch snapshot
  (``CLOCK_NVS_SAVE_INTERVAL_SEC`` 900, ``clock_sntp.c:52`` — the
  code's comment: "one commit per quarter hour") fires from the minute
  tick (``clock_interrupt.c:171``, its ONLY caller): **96 commits/day**
  of an 8-byte ``int64`` (``nvs_set_i64`` + ``nvs_commit``), plus one
  forced save per SNTP sync — but only on the RTC-less boards. The
  whole ``clock_nvs_*`` family sits under ``#if
  !CONFIG_CLOCK_USE_PCF85063A`` (``clock_sntp.c:45``) and the calling
  minute-tick branch lives inside the same guard's RTC-less block in
  ``clock_interrupt.c`` (:124 to the ``#else`` at :200) — the S3's
  built ``sdkconfig.h`` defines the RTC, so **the S3's daily NVS bill
  is zero, compiled out**, while the P4 (``sdkconfig.defaults.esp32p4``
  pins ``=n``) and the knob (``knob/sdkconfig`` leaves it unset) each
  pay the 96.
- **The knob's day carries no scheduled write but its clock.**
  ``moon_tfcache.c`` is a ``UI_BOARD_SRCS`` member the ``KNOB_BOARD``
  branch empties (``components/ui/CMakeLists.txt:31-33``) — the TF legs
  are *not compiled*, the bus ladder's no-such-leg precedent.
  ``alarm.txt``'s writer (``clock_alarm_scheduler_write``,
  ``clock_alarm_scheduler.c:504``) has **zero call sites repo-wide**:
  the file belongs to the spiffs image, not the runtime.
  ``settings.txt`` — 2 comment + 6 key lines, 240 B computed from the
  asserted ``fprintf`` template — is written only by three
  settings-screen actions (sleep ``screens.c:1406``, timezone
  ``:3066``, alarm sound ``:3148``) and the TEST settings demo
  (``main.cpp:275``); ``wifi_credentials.txt`` only by the
  provisioning wheel's commit (``screens.c:2301``, its single caller,
  87 B with a sample SSID). The knob's 64 K coredump partition
  (``knob/partitions.csv:18``) sees its first bytes on a crash — the
  coredump row the storage ladder drew italic *no bytes until a crash*
  is a write target whose schedule is a flat zero.

The three facts the other ladders cannot show: the *most numerous*
write is the 8-byte NVS value — 96/day of operations per board at a
total payload under 1 KB/day, while 24× rarer the hourly stores write
up to 5.5 MiB/day on the S3 and 14.5 MiB/day on the P4 at the encoder
bound (the real q90 JPEGs far smaller, instrumented nowhere); the
*same shared component* bills two boards 96 commits a day and the
third zero, decided by one ``#if`` that three sdkconfig layers answer
differently; and the knob — the board with the smallest screen —
writes flash on a tap and on a crash, and on nothing else, because
its largest media subsystem is excluded at CMake level rather than
disabled at runtime. The human-paced writers (three screen actions,
one provisioning commit, one TEST demo) are call-site counts, not
frequencies, and deliberately contribute zero to the day bar.

.. figure:: ../_static/figs/perf_writes.png
   :alt: the write ladder — scheduled writes per day per board, and bytes per single write
   :figclass: align-center

   The churn axis. **Left:** scheduled flash writes per day, stacked
   by leg — the hourly store (24), eviction unlinks (~24 at window
   steady state), meta at the new-moon crossing (~1/synodic ≈ 0.03),
   the 15-min NVS epoch commit (96, RTC-less boards only): S3 48, P4
   144, knob 96. S3 blue, P4 vermillion, knob violet; the two zero
   legs are annotated where their bars are absent (the RTC ``#if``,
   the ``KNOB_BOARD`` source exclusion). **Right:** bytes per ONE
   write, log — the 8-byte NVS value, the 87 B credentials write, the
   240 B settings rewrite, the ~82 B meta rewrite (both boards'
   asserted templates), the 64 K coredump partition bound, and the
   encoder's own file bound ``S²+S²/2+1024`` — 241,024 B at 400 px,
   634,774 B at 650 (hatched: the storage ladder's twin, never a
   measured JPEG). alarm.txt draws a zero stub: its writer exists at
   ``clock_alarm_scheduler.c:504`` with no caller. The generator is
   ``docs/tools/gen_perf_writes.py`` — caps, the fprintf templates,
   every call-site count (1 store, 3+1 settings writers, 1 credentials
   writer, 0 alarm writers, 1 ``maybe_save`` caller), the Kconfig
   defaults, the ``CLOCK_USE_PCF85063A`` state across all three
   sdkconfig layers and the partition rows are **parsed from the live
   sources** at figure time; nothing touches a card, an NVS partition
   or a spiffs image.

What this ladder does NOT count, and who does: the bytes the display
moves per refresh are the :ref:`bus ladder <bus-ladder-perf>`, the
resident flash images are :ref:`the storage ladder
<storage-ladder-perf>`, the 709-file card month as *capacity* is the
:ref:`hour-cache footprint <cache-footprint-perf>` above, and the
spiffs files' contents and ownership are inventoried in
:doc:`the SPIFFS store <../reference/spiffs-store>`.

.. _census-ladder-perf:

The build census (how many parts make each image, and how many are silent)
--------------------------------------------------------------------------

**The short version.** The binary chronology counts bytes and the
storage ladder counts bytes lying still; the seam sketch
(:doc:`the build system <../reference/build-system>`,
``build-seam.dot``) draws the *shape* of the three projects. Nobody
had yet counted the **parts** that shape prices — how many components
each board's image is assembled from, and how many of them compile to
nothing. Three panels, every number parsed at figure time from the
directory listings, the two ``CMakeLists.txt`` files, the three
dependency locks, the font tiers and the two clock build trees:
**left**, one stacked bar per board of the components under project
control (the same steel **shared-4** — ``clock``/``wifi``/``moon``/``ui``
— under all three bars, then the app, the board-side pair, the
vendored BSP, the knob-native six, the shims, and the registry set
each lock resolves: 29 / 31 / 20 parts; the IDF core set is
target-specific and deliberately not counted); **middle**, the seam's
prices in lines of code — the four ``UI_BOARD_SRCS`` translations the
knob build never compiles (3,219 + 815 + 792 + 352 = **5,178**),
against the knob's three answers to the board-specific names
(16 + 28 + **0** lines); **right**, who names what — the
``board``/``imu``/``esp_codec_dev`` matrix read out of the four
shared ``REQUIRES`` literals (clock names ``board``; ui names all
three; wifi and moon name none), plus the ``FONT_DIR`` branch's one
tier per build. The family's discipline holds: the parsers assert the
``EXCLUDE_COMPONENTS`` literal predates ``project()``, that
``EXTRA_COMPONENT_DIRS`` lists exactly the four dirs and never the
parent, that the KNOB branch still empties the four TUs, that the
three locks still disagree per target (hosted never on S3, the co5300
BSP never on P4, the knob's six name-for-name against
``knob/managed_components/`` on disk), and that the codec shim is
still *a CMakeLists file and nothing else* — the figure is the
docs-side twin of the seam's rules and refuses to draw when one is
removed.

.. figure:: ../_static/figs/perf_census.png
   :alt: three panels — per-board stacked bars of components under project control (S3 29 with its in-tree BSP, P4 31 with the BSP excluded and 24 managed, knob 20 with six native components and three shims), the four knob-dropped UI translations by LOC against the shims' 44 lines and the zero-source codec shim, and the REQUIRES naming matrix for board/imu/esp_codec_dev with the one-tier-per-build FONT_DIR note
   :figclass: align-center

   **Left:** one bar per board, segments by family (legend below).
   The shared-4 is identical under every bar — the census's first
   fact is that the seam only ever moves the *ends*. The S3 carries
   the vendored AMOLED BSP in-tree (its managed name shadowed, the
   25-row patch living there); the P4's bar is grown by
   ``EXCLUDE_COMPONENTS`` instead — one name removed (:20), and the
   XC BSP + ``esp_hosted`` + ``esp_wifi_remote`` return as managed
   entries (the bar grows 21 → 24); the knob adds neither side — no
   ``board``, no ``imu``, no BSP — and answers their names with three
   silent shims. **Middle:** amber = a TU the knob build never
   compiles (the sum 5,178 is in the axis label); each bar is
   annotated with what answers it on the knob side. The
   ``esp_codec_dev`` row is the honest *no such leg* drawn as a
   **zero** bar (the bus ladder's precedent): the shim is one
   ``CMakeLists.txt`` and nothing else, so the name exists, the
   early pass is satisfied, and zero lines compile. **Right:** filled
   = the component's ``REQUIRES`` literal names that register — four
   hits total, exactly the four answers the three projects must each
   provide; below it, the three font tiers (7 ``.c`` files each,
   asserted the *same file names* across all three) of which
   ``FONT_DIR`` compiles exactly one per build.

Three counting facts the byte view cannot show:

- **The knob answers three names it never compiles with 44 lines and
  one CMakeLists — while 5,178 lines drop out on the other side of
  the seam.** The shim side is 16 (board) + 28 (imu) + 0 (codec);
  the drop side is the four ``UI_BOARD_SRCS`` files, each of which
  the knob re-answers *elsewhere* (``knob_moon_render.c`` for the
  whole 3,219-line UI, a ``"Silent"`` stub for the audio, nothing at
  all for the TF cache and the demo — no SD socket, no demos). The
  seam costs 44 lines to keep and re-prices 5,178 into new files.
- **The P4's single excluded name is repaid by a managed set grown
  to 24.** The vendored S3 BSP is one ``EXCLUDE_COMPONENTS`` entry
  (:20), and what arrives in its place is a whole radio stack: the
  XC BSP plus ``esp_hosted`` plus ``esp_wifi_remote`` (the remote
  C6) are lock entries, not tree entries — the target-gating is
  asserted both ways (hosted never on S3, co5300 never on P4).
- **The clock's whole named "board" is 28 lines, and the purest
  price of the early pass compiles 0.** The name ``board`` that
  ``clock`` and ``ui`` list in ``REQUIRES`` is answered by a
  28-line forwarding shim over the BSP; ``esp_codec_dev`` is the
  limiting case — a component that must *exist* for the requirements
  pass (build.cmake sees neither project variables nor ``CONFIG_``
  symbols) and exists as a build file only. A name, not code, is the
  seam's smallest currency.

The census is the seam sketch's ledger twin: ``build-seam.dot``
(:doc:`the build system <../reference/build-system>`) says *where*
the names resolve, ``build-flash.dot``
(:doc:`the boards <../devices/index>`) says *what the command
carries*, and this counts *how many parts* each image assembles from
— with the shims as the only segments that compile to silence.

.. _sharing-ladder-perf:

The sharing ladder (how many images compile each source)
---------------------------------------------------------

**The short version.** The build census counts the *parts* each image
assembles from; this ladder **transposes the table** and asks the
complementary question — of the family's twelve link images, how many
compile each source? The twelve: three device firmwares plus the host
tools built per target — ``moon_sim`` ×3, ``moon_bench`` ×3, the uisim
×3 (the gate and the bench are one ``add_executable`` pair configured
once per target dir, so every moon source is compiled three times over
on the host alone). **Left**, one bar per source (or family of
sources): the 16 portable-core TUs — five math plus eleven vendored
ERFA — are the only repo-owned files that touch the dotted
"every image" line at 12/12; the vendored LVGL's 412 TUs (cited,
hatched) stop at 6 (firmwares + uisims, the sim/bench never link it);
the four ``UI_BOARD_SRCS`` top out at **2**. **Right**, the same
model read per image — repo-owned TUs per link image, recomputed from
the rows and asserted: S3 **47**, P4 **47**, knob **57**, each sim
**19**, each bench **19**, each uisim **31** (+412 vendored). Every
count is parsed at figure time from ``components/moon/CMakeLists.txt``,
``host_sim/CMakeLists.txt``, ``host_sim/uisim/build.sh``, the four
shared component ``CMakeLists``, and the knob's component listing —
and the parser asserts the three core source lists are **name-for-name
equal** (the "shared verbatim" invariant made mechanical: if a host
list drifts from the component list, the figure refuses to draw), that
the two probes are tracked-but-never-linked, and that the font tiers
are 7 files each with identical names.

.. figure:: ../_static/figs/perf_share.png
   :alt: two panels — left, 22 horizontal bars of "images per part" against a dotted line at twelve (the sixteen portable-core TUs at 12/12, vendored LVGL hatched at 6, screens/styles/images at 6, the host backend at 9, per-target generated asset tables at 3, clock 11 TUs at 3, the four UI_BOARD_SRCS at 2, one font tier of 7 at 2, knob-native fifteen at 1, the two throwaway probes dashed at 0); right, "parts per image" — twelve bars: S3 47, P4 47, knob 57, each sim 19, each bench 19, each uisim 31 with a plus-412-vendored annotation
   :figclass: align-center

   **Left:** the file side — who compiles this, with the "every
   image" witness line at 12. Blue is the portable core (the
   founding invariant, drawn as the only bar that reaches the line);
   steel the shared UI; slate the clock-side device TUs; amber the
   four the knob drops; violet the knob's own fifteen; teal the host
   tools' own files; the grey hatched row is the cited 412-TU LVGL
   walk (``drivers/`` excluded, counted from the uisim's own find);
   the dashed ghost at zero is the throwaway pair. **Right:** the
   image side — the same rows summed per link image, never
   hand-summed: three firmwares, three gates, three benches, three
   screenshot rigs.

Three counting facts the census's part-view cannot show:

- **The core is the family's only everywhere-code, and the figure is
  its guard.** 16 of the ~47 repo-owned TUs in a device image belong
  to ``components/moon/core`` — five math files and eleven public-
  domain ERFA files — and they are the ONLY repo-owned files compiled
  into all twelve images. The parser reads the core list out of three
  places (the component's ``SRCS``, ``host_sim/CMakeLists.txt``'s
  ``MOON_CORE_SRCS``, the uisim's ``build.sh``) and dies unless the
  three are name-for-name equal, so the "shared verbatim" promise the
  project instructions make is now also a *figure-time* promise.
- **The knob builds the largest repo-owned image while re-using the
  least of itself.** 57 TUs beat the clocks' 47 — the shared clock's
  whole 11-TU body arrives through ``EXTRA_COMPONENT_DIRS``, plus the
  knob's fifteen own files, plus the inherited core and UI. Yet the
  violet row tops out at **1**: nothing knob-owned compiles twice,
  and the four ``UI_BOARD_SRCS`` the census priced at 5,178 lines top
  out at **2** images (S3 + P4). It is the configuration ladder's
  finding mirrored: the smallest screen carries the biggest menu *and*
  the biggest one-shot image.
- **Screenshots are the most expensive host artefact — and the dead
  code's bill is exactly zero.** Each uisim panel compiles 31
  repo-owned TUs *plus* the 412-TU vendored LVGL (cited), so the
  docs' three panel sets cost 1,236 vendored compiles, while
  ``moon_sim``/``moon_bench`` — the image gate's own numbers — link
  no LVGL at all. And the two tracked throwaway probes
  (``dark_test.c``, ``newmoon_test.c``, plus a stale gitignored
  ``pole_test`` ELF that is all the third ever left) draw the dashed
  ghost bar at **0**: tracked by git, called in code, linked by
  nobody.

The sharing ladder is the census's transpose and the surface sketch's
ledger: :ref:`the surface in one picture <c-api-surface>` names who
*calls* each core entry; this prices how many times each source is
*compiled*. See also :doc:`the build system <../reference/build-system>`
for where the lists live and :doc:`the knob display
<../implementation/knob-display>` for the fifteen one-shot files.

.. _config-ladder-perf:

The configuration ladder (how many choices each layer hands over)
-------------------------------------------------------------------

**The short version.** The build census counted the *parts* each
image assembles from and which of them compile to silence; it never
priced the layer above the parts — the **options**. This ladder counts
the knobs: every ``config`` entry the three projects' ``menuconfig``
menus define (left), who actually reads each one (middle — the
census's silence, re-counted for the menu), and the whole layer the
*user* owns: the six keys on ``settings.txt`` (right). A
configuration option is the product's cheapest component and its
most numerous — and it can be silent four different ways.

**Left: what each menu hands over.** The shared 50 (UI 40 + CLOCK 9 +
MOON 1, every name parsed from the five first-party Kconfigs) sit
under every bar; on top of them the S3 carries the clock's BOARD menu
(5) *and* the vendored AMOLED BSP's 12 options (hatched — upstream's
file, counted not curated) for **67**; the P4 excludes that BSP by
name and lands at **55**; the knob answers with 50 shared + 30
knob-native across its six shim menus for **80**. The counting is
per-file and name-for-name: the generator SystemExits if a single
option is added, renamed or removed in any of the ten Kconfig files.

**Middle: who reads it where.** Blue is read by code every build
compiles; amber is read *only* by the four ``UI_BOARD_SRCS`` + the
clock's ``main.cpp`` — the files the knob build never compiles — and
is therefore silent *on the knob*; steel names a CMakeLists guard
(the ``MOON_ALBEDO_W`` fit check, the ``BOARD_S3`` mismatch referee),
dark-grey hatch a key read by no C at all (the three ``int``
esp_hosted manifest guards, whose only consumer is the registry's
if-clause parser), pale-grey dots read by nobody anywhere. The
ledger's numbers: **27 of the 40 UI entries and 1 of the 9 CLOCK
entries compile to silence on the knob** — 28 of the 50 the knob
inherits are clock-private. Five entries are dead letters
repo-wide: the two ``CLOCK_TIME_FORMAT_*`` (the 12/24 h choice is
runtime — no C ever reads them), ``UI_IMU_ROLL_OFFSET_DEG`` (the
obsolete spin-era rest angle, still stamped ``=0`` into all three
live sdkconfigs), ``BSP_I2C_FAST_MODE``, and the knob shim's two
*unselected legs* of ``choice BOARD`` (the choice must carry all
three members; only ``BOARD_KNOB`` has a knob-side reader). Two
further entries never even reach a menu: ``UI_DEMO_FULL_TOUR`` is
hidden behind ``depends on UI_DEMO_MODE`` — off in all three
configures — and the generator only accepts that absence when it can
prove the gate off everywhere. Only **eleven** UI entries are read
by shared code — and one of them is the most surprising:
``UI_SCREEN_SWITCH_FADE``, which the knob's own ``knob_moon_render.c``
gates its fade-to-black on.

**Right: the user's whole layer.** Not dozens of knobs but **six
keys** on ``settings.txt``. Four are live: two screen-written
timeouts (``screens.c:1403``/:1401, both clamped to 60..1800 s by
the store's own ``SETTINGS_TIMEOUT_MIN/MAX_SEC``), the timezone
(``screens.c:3065``) and the alarm sound (``screens.c:3147``, range
twin asserted against ``ALARM_SOUND_COUNT`` 4). Two are ghosts —
``boot_anim_enable`` and ``dusk_anim_enable``, parsed and re-saved
*only so a stale card must not break parsing* (the store's own
words), written by no screen, read by no getter; the single writer
repo-wide is the ``TEST settings demo`` in ``main.cpp``. Their seed
dots are grey because the ``#ifndef`` beside them names a macro no
Kconfig defines anymore.

.. figure:: ../_static/figs/perf_config.png
   :alt: three panels — stacked menuconfig-entry bars per board (S3 67 with the hatched vendored BSP, P4 55, knob 80 with its six shim menus), a per-menu ledger of who reads each entry (alive, amber knob-silent, configure-only, registry-only, vendored, dead letters), and a six-key matrix of settings.txt (writer, seed origin, range, still-read) with the two ghost keys drawn dashed-grey
   :figclass: align-center

   **Left:** one bar per board (legend below the figure); the shared
   50 under every bar, the S3's hatched BSP tail drawn only there,
   the knob's six violet shim segments at the end. **Middle:** one
   row per menu, coloured by who reads it — the amber 27 is the UI
   menu's knob-inert majority, the pale 2s the dead letters, the
   dark-grey hatch the three registry guard keys. **Right:** a
   filled dot = the column's claim holds; the two ghost rows hang
   grey with writer and reader open. Every number is parsed at
   figure time — the generator refuses to draw when a Kconfig entry
   vanishes from a ``kconfig-*.inc`` fragment, when a documented
   option loses or gains a reader against the classification, when
   an entry appears in no live ``sdkconfig`` without a provably-off
   ``depends on`` gate, when the twelve BSP options leave the
   fragments' documented-nowhere state, when a guard key stops being
   ``int``, or when the six keys of ``settings_store.c`` stop
   matching its save block.

Three counting facts no other ladder can show:

- **The menu is inverted against the product.** The smallest screen
  (the knob, 360 px) carries the *biggest* menu — 80 entries against
  the S3's 67 and the P4's 55 — because the seam forces it to
  re-declare the board ``choice`` and grow six shim menus of its own,
  while **28 of the 50 entries it inherits are clock-private**: a
  third of the knob's menu compiles to silence in the knob build.
- **Silence has four prices, not one.** 28 entries are read only by
  the *other* build's files (amber); 5 are dead letters no file
  anywhere reads; 2 are configure-time only (a CMake guard is their
  sole reader); 3 are read by no C at all and exist to answer the
  esp_hosted registry's ``if:`` clauses. A Kconfig entry can be
  simultaneously documented, configured, stamped into ``sdkconfig``
  — and inert four different ways.
- **The user's configurability is six keys wide — and two of them
  are kept alive by the save path alone.** Everything else about the
  device is compiled in; the four live keys all have a screen writer
  and a validated range, while the two LEGACY ghosts persist only
  because dropping them would break parsing of a stale card — the
  runtime layer's own *no such leg* rows.

The generator is ``docs/tools/gen_perf_config.py``; its parsers read
the ten Kconfig files, the ``kconfig-*.inc`` fragments (including the
``KNOB_BATTERY_DIV_NUM`` / ``_DEN`` shared-prefix idiom), the
``UI_BOARD_SRCS`` set out of ``components/ui/CMakeLists.txt``, the
``CONFIG_*`` tokens of every ``.c``/``.h``/``.cpp`` under
``components/``, ``main/`` and ``knob/``, the seeds, clamps and
ghosts of ``settings_store.c``, the four writer sites in
``screens.c``, and the three live ``sdkconfig`` files. The
configuration sibling of :ref:`the build census <census-ladder-perf>`
— that counts parts, this counts choices.

.. _freshness-ladder-perf:

The freshness ladder (how old every shown number is)
----------------------------------------------------

**The short version.** The response ladder plotted how soon the user
is *noticed* — the input side of the latency family. This is its
mirror: how **old** every number on the panel may be when the user
looks at it. Eleven rows on one log millisecond axis, every timer
parsed from the live sources at figure time: the 1000 ms
seconds-tick timer and the 1300 ms clock-interrupt poll that owns
the digits (each annotated with the render beat, parsed and asserted
equal in all three generated ``sdkconfig.h`` files), the wall-minute
reload that lands the alarm row's 24 h-boundary prefix flip
(``actions.c:959``), the knob's 60 s gauge timer
("the cell moves on the minute scale, not the second", the code's
own comment), the 900 s NVS snapshot gate of the two boards without
an RTC (forced on every SNTP fix), the SNTP correction interval
(Kconfig-paced), the moon disk's hour-centre pin — the parsed
``hour * 3600 + 1800`` offset, which the code itself calls "at most
30 min stale at the show" —, the hour cache's same pin on
*deterministic* content ("byte-identical across boots and boards"),
and the sky tint's calendar-day scan ("the one untinted bake
self-heals"). Two rows carry **no bar**: the Wi-Fi state has no
timer at all (three on-demand kickoffs), and the albedo/DEM blob is
a pure function keyed by calcver — pipeline age, never stale.

The figure's content is the asymmetry between *refreshing* and
*going stale*. Right panel: the same rows as checks per day
(86400 s / cadence — one division, nothing new measured), ~156,000
checks a day of which the ~153,000 fastest are the ephemeris being
re-derived, fresh by construction. The only rows that can actually
**age** are the ADC (1,440/day), the radio (on demand) and the NVS
snapshot (96/day) — which is exactly why the P4 *marks* its one
stale row: the unsynced clock sits at ``LV_OPA_50`` until a fix
lands (:doc:`the chain of trust <../implementation/clock-sleep>`).
The minute, hour and day bars are wall-calendar definitions, stated
as such on the rows, not firmware constants. Hardware drift between
SNTP corrections is deliberately **not plotted** — it is the
32k-crystal bench question of :doc:`the P4 device page
<../devices/p4>`, and inventing a drift rate would be the one way to
make this figure lie. The generator is ``docs/tools/gen_perf_fresh.py``;
the host-side proof that the ephemeris rows *depict correctly* is
:doc:`the image gate <image-gate>`, and the device half that proves
the pipelines actually land these values is
:ref:`the capture grammar <capture-grammar-verify>`.

.. figure:: ../_static/figs/perf_fresh.png
   :alt: every shown value's worst staleness on one log axis, and the same rows as checks per day
   :figclass: align-center

   The freshness ladder. **Left:** nine timed rows on the log
   millisecond axis — blue the shared tick family (arc, digits,
   alarm prefix), amber the knob's ADC, teal the time source (NVS
   snapshot, SNTP cadence), violet the moon family (disk pin, hour
   cache, sky tint); hatched = Kconfig-paced, and the two italic
   rows are the honest *no timer at all*. **Right:** the same rows
   as checks per day — one division, nothing new measured: the panel
   re-derives the ephemeris ~153,000 times a day and polls the three
   things that can age another ~3,000.

.. _sharpness-funnel-perf:

The resolution funnel (how finely each stage can speak)
--------------------------------------------------------

**The short version.** Every other ladder here counts a resource a
refresh *consumes* — time, bytes, RAM, watts. This one counts the
resource the picture is *made of*: how finely each stage is allowed to
speak. The bake samples the equirect albedo and the relief gradient
cache **nearest-neighbour** — one tap per output pixel, no filtering —
and that is only allowed when the source is dense enough that a
mis-chosen neighbour moves the picture by less than a pixel. The bake's
own header comment has always justified NN with a density number ("the
per-board equirect texture is >= 1 texel per screen px on the disk, so
NN is sub-pixel-accurate"); this figure is that claim, audited — and
honestly restated: the density that holds is the disk **average**, the
disk *centre* (the worst place, because orthographic projection stretches
texels widest there) dips below 1 for two of the six source legs, and
what actually guarantees sub-pixel accuracy is the half-texel **pick
error**, which never leaves the pixel on any leg on any board.

The funnel narrows left to right — panel, disk, source columns — and
the narrow point is where the NN tap lives. At the disk centre one
radian spans exactly *R* screen px (the bake's base direction is
``r = u*right + v*up + w*E``, ``w = sqrt(1 - r²)``), so the centre
density of a *W*-column source is ``W / (2·pi·R)`` texels per screen
pixel — the **orthographic worst case**, pi/2 ≈ 1.57× *below* the naive
diameter-average ``W / (4R)`` the comment's ">= 1" rides on. Every
value is parsed at figure time with SystemExit-failing parsers:
``MOON_ALB_W``/``MOON_ALB_H``/``MOON_DEM_W``/``MOON_DEM_H`` from all
three generated ``moon_consts.h`` (and asserted equal to the widths the
bake comment itself quotes), the ``MOON_PX`` pair from
``moon_ui.c:174-178`` with the ``MOON_R = MOON_PX/2`` shape assert, the
knob's shared-home disk radius parsed out of
``knob_ui_set_disk_geom(150.0f, …)`` (``knob_moon_render.c:178`` —
150, not recalled), the panel sizes from ``BSP_LCD_H_RES``, the P4's
generated ``CONFIG_BSP_LCD_TYPE_720_720_4_INCH`` and ``KNOB_LCD_H_RES``,
and the two NN rounding lines asserted present in ``moon_bake.c``
(``int col = (int)(colf + 0.5f)`` and the ``(int)(row_frac * alb_h +
0.5f)`` row twin) — the pick error bound *is* that half-texel rounding,
so if the sampler ever changes the generator refuses to plot.

The verdict the six bars tell: at the centre, **S3 albedo 0.69** (the
comment's 1.08 is the Ø-average), S3 relief **1.63**; P4 albedo and
relief both **1.003** (the claim holds to 0.3 %); knob albedo **0.92**,
knob relief **1.09**. Three legs clear the bar at the centre, two miss
it, one holds by a hair. But the pick error — half a texel at centre
scale, ``pi·R/W`` px — is **0.73/0.31/0.50/0.50/0.55/0.46 px: all six
sub-pixel**. That is the honest form of the comment's guarantee (the
code comment now says exactly this, "ON AVERAGE" qualifier and
pick-error sentence included, citing this generator), and it is also
why the gate re-checks "the NN/bilinear and table/atan2 differences
against the Python model" after any change to the sampler — the
validated model is the backstop when a density number is argued rather
than plotted.

.. figure:: ../_static/figs/perf_sharpness.png
   :alt: source widths per board, and source texels per screen pixel at the disk centre against the 1.0 bar
   :figclass: align-center

   The resolution funnel. **Left:** parsed widths per board — panel,
   disk diameter, albedo columns (= 360°), relief DEM columns: the
   funnel narrows source → screen on every board. **Right:** the six
   source legs as texels per screen pixel at the disk **centre** (the
   orthographic worst case); teal clears the amber 1.0 bar the bake
   comment claims, red does not, and every bar carries its half-texel
   NN pick error — all six sub-pixel, which is what the bake's
   "sub-pixel-accurate" actually rides on.

The generator is ``docs/tools/gen_perf_sharp.py``. The sampling itself
(the displaced-normal Lambert, the +180° texture offset) is
:doc:`the lighting model <../science/lighting>`; the vectors feeding
the tap are :doc:`the geometry chain <../science/geometry>`; the asset
widths are reference-tabled in :doc:`the moon constants
<../reference/moon-consts>`; the bake *time* these taps cost is the
work ladder at the head of this page; and the *count* of pixels they
fill per published frame is :ref:`the pixel ladder <pixel-ladder-perf>`
above.

.. _taps-ladder-perf:

The tap ladder (how much human labour each screen costs)
--------------------------------------------------------

**The short version.** Every other ladder on this page prices the
machine — milliseconds, bytes, watts, priorities. This one prices the
other half of every interaction: **the finger**. How many taps from
the awake home screen does each screen of the ``ScreensEnum`` cost?
The answer is a three-level ladder — hub 1, ring 2, subtrees 3,
nothing deeper — and four screens the finger cannot reach at all,
because the machine loads them. The right panel prices the product's
only keyboard: a password is **one press-and-release per character**
plus rounding — you pay for the secret, not for the keyboard.

The graph is not hand-drawn; the generator **parses the navigation
edges out of ``screens.c``** — the home tap
(``event_handler_cb_home_tap``, registered on exactly the two home
roots and opening the hub only while ``!clock_is_idle()`` — the wake
tap is a deliberate second tap), the ``ring[]`` table with its
``#if defined(CONFIG_BOARD_P4)`` clock entry (6 icons on the S3 and
the knob, 7 on the P4), the alarm screen's two ``user_data`` entries,
the roller row that opens the wheel, the wheel's exit, and all nine
bottom-exit buttons — under an **accounting rule**: any literal
``loadScreen(SCREEN_ID_*)`` in ``screens.c`` outside this set is a
SystemExit, so navigation nobody has explained cannot silently
appear. The four machine-only views (alarm, transient sync,
light-sleep, deep-sleep) are then italic **"no tap leg"** rows with
their ``actions.c`` drivers cited — the bus ladder's *no such leg*
precedent — because you cannot tap *into* the alarm view; it taps its
way out (touch dismisses). The per-panel bars are cross-checked
against the **drift-gated reachability matrix** of :doc:`the UI index
<../ui/index>` (16/15/13, with the exact absentee set asserted: the
P4-only ``settings-time``; the knob's two sleep views, which it
replaces with a real dimmed backlight and a real ``esp_deep_sleep_start``).
The knob's "tap" is a dial-click — who *reads* the finger is
``docs/_graphs/input-chain.dot``.

What the bars say: **depth never exceeds 3** — nothing in the product
hides behind a fourth tap. Everything the finger can reach is the hub
(1), a ring icon (2), or one of three third-level leaves: the alarm
screen's two entries (``settings-pomodoro``, ``settings-alarm-time``)
and the wheel (``settings-wifi-password``). One parsed fact the plot
does not draw but the parser asserts turns depth into a bill:
**every screen's bottom-exit button goes to its BFS parent** (the
parsed exit table — pomodoro and alarm-time to the alarm screen, the
wheel to the scan screen, everything else to the hub, the hub's
central disk to home) — so up costs exactly what down cost, and a
round trip to any screen is precisely ``2·depth`` taps: 2 to the hub,
4 to a settings screen, 6 to the wheel. ``settings-time`` draws its
bar for the P4 alone; the italic notes on its row are the matrix's
own ``—`` cells.

**Right:** the wheel's price as a function of the password, derived
from the registers parsed verbatim out of ``s_wheel_sets[]`` (letters
and capitals 26 slots, digits and symbols 25 each — by ``strlen``,
never hand-counted). Three curves: lowercase-only ``N+1`` (the first
register the wheel draws is letters, per the ``s_wheel_mode``
comment), one register hop ``N+2``, alternating letters/symbols
``1.5N+1.5``. The register premium is *half a tap per character* —
at a 12-character key the three paths cost 13/14/19 taps: **the
per-character term dominates; the registers are rounding**. The cap
pair is parsed, not recalled: ``WHEEL_PW_MAX`` 63 (the commit guard)
under ``MAX_PASSWORD_LEN`` 64 — the firmware's own reject of lengths
0 and ≥ 64 — so the floor is one character and no 8-character
minimum exists anywhere.

.. figure:: ../_static/figs/perf_taps.png
   :alt: taps from the awake home to every screen, per board, and wheel taps versus password length
   :figclass: align-center

   The tap ladder. **Left:** BFS tap-depth per screen, one bar per
   board (S3 blue, P4 cyan, knob amber); machine-driven views carry
   italic ``actions.c`` driver citations instead of bars, and absent
   screens carry the matrix's own reasons. **Right:** taps to enter
   and commit a password of *N* characters — press-and-release per
   character, register hop, commit key; the wheel stops at 63 and
   ``wifi.c`` rejects ≥ 64, both parsed.

The generator is ``docs/tools/gen_perf_taps.py``. What one
press-and-release *is* — the exact-box hit-test, the sticky sector,
the commit-only-in-band rule — is drawn in ``docs/_graphs/wheel-commit.dot``
on :doc:`the round keyboard <../keyboard>`; the matrix the bars are
checked against is :doc:`the UI index <../ui/index>`; the knob's
dial-click path is :doc:`the knob display
<../implementation/knob-display>`.

.. _assembly-ladder-perf:

The assembly ladder (how many objects every screen assembles — and what a visit pays)
--------------------------------------------------------------------------------------

**The short version.** Every ladder above prices *work*: the tap
ladder prices the finger's labour, the bus and pixel ladders price
movement, the static footprint prices bytes, the work ladder prices
milliseconds. None of them has priced the **population** — the LVGL
objects that exist at boot, before a finger has touched anything.
This ladder counts it out of ``screens.c``: sixteen detached roots
and **255 object instances** (256 on the P4), assembled exactly once
by ``create_screens()`` (``screens.c:3894``) inside ``ui_init``
(``ui.c:77``) *before* the first ``loadScreen`` — and then prices the
build-once model that makes visiting them free. The code's own
comment is the contract, at ``screens.c:1368``: *"The widgets are
created once (loadScreen never destroys them), so the pointers live
here."* The awake home **shows 17** objects; the other 238 are the
hidden city. A screen visit costs **zero creations and zero
destructions** — ``loadScreen`` (``ui.c:51``) swaps roots, and the
sub-screens pay only an entry REFRESH.

The graph is not hand-drawn; the generator **parses the census out of
the live sources** — and its parsers double as docs-side twins of the
UI's structural rules, SystemExit-failing when a screen, a ticker or
a registry entry appears or vanishes:

* the 16 ``SCREEN_ID_*`` ids in enum order (``screens.h``); exactly
  one ``create_screen_*`` per id, each rooted at exactly **one
  detached** ``lv_obj_create(0)`` (layout contract 1) — bodies are
  brace-matched, not regex-sliced;
* ``create_screens()`` calls exactly those 16 creators in enum order,
  and ``ui_init`` runs it *before* ``loadScreen(SCREEN_ID_MODEM_SLEEP)``
  — every object exists before the first frame;
* ``loadScreen`` contains **no creator call at all** — the build-once
  claim is asserted, not quoted: add a ``*_create(`` inside it and
  the figure refuses to draw;
* exactly 16 ``tick_screen_*`` definitions plus ``tick_screen_funcs[]``
  (``screens.c:3872``) in enum order — ``ui_tick`` (``ui.c:87``)
  dispatches **only the current screen**, so the fifteen off-screen
  roots tick zero times;
* helper bodies are resolved **transitively** (the exit button is
  container + glyph, the hub icon is one label, the roller helper is
  one roller), and ``create_user_widget_clock`` — the ONE clock
  widget, called exactly once, from the airplane creator (layout
  contract 2, ``ui_clock_attach_to`` reparents it onto every moon
  root, ``screens.c:3826``) — counts toward the home;
* object creation inside a loop is priced by the parsed bound —
  ``WHEEL_N_SECTORS`` 12 (``screens.c:1863``), literal bounds, and
  the hub's ``sizeof(ring)`` loop at the parsed 6/7 ring table — and
  **any loop bound the generator cannot model is a SystemExit**: a
  loop nobody modelled cannot ship a bar;
* the structural tables re-derive the tap ladder's own: exactly 9
  ``settings_sub_screen_prepare`` exits, exactly 10 cases in
  ``settings_screen_refresh_for`` (``screens.c:730`` — the 10
  sub-screens, no more), exactly **4** ``moon_screen_prepare`` call
  sites (``screens.c:41``, the airplane/wifi/light/deep roots that
  share the ONE moon image via ``moon_ui_create_image``,
  ``moon_ui.c:2627``), and **zero** creations in the knob's P2 dial
  block — the dial cursor only restyles existing objects.

What the bars say: **the wheel is the bill**. One screen — entered
once per provisioning — assembles **150 of the 255 objects (59 %)**:
42 arcs + 49 containers + 59 labels, dominated by the 12-sector loop
(36 box arcs, 24 box dividers, 12 sector dividers) and the 26 letters
drawn twice (52 in/out labels). The wheel is also *not* a roller —
the round keyboard is hand-assembled from arcs and boxes; the
family's 14 ``lv_roller`` instances all live on the list screens.
Everything else is small: the home 17 (13 of them the shared clock
widget), the hub 9 (6 of them ring labels), the lightest screen the
alarm view at 3. The awake user of a home screen sees 17 of the 255 —
**under seven percent of the city is ever on screen at once** — and
that is exactly what the build-once model buys: paying for the whole
city once at boot beats building and tearing down a screen per visit,
because the visit then pays *nothing*. **Right:** the three
panels carry identical bills because there is exactly one
``screens.c``; the only per-board delta the parser finds is the hub's
7th ``ring[]`` label (the P4's clock entry, the same
``#if defined(CONFIG_BOARD_P4)`` the tap ladder walks), making the
P4's city 256. The ledger box states the lifecycle the parsers
proved: 0 created / 0 destroyed per visit, entry refresh on the 10
sub-screens, tick dispatch to the current screen only, four roots
sharing one moon image.

.. figure:: ../_static/figs/perf_assembly.png
   :alt: per-screen object budgets as stacked class bars, and the identical per-board totals of 255/256/255
   :figclass: align-center

   The assembly ladder. **Left:** every screen's object budget, one
   stacked bar each (containers slate, labels blue, arcs vermillion,
   rollers violet, the shared moon image amber), sorted; loop-bound
   multipliers are parsed at figure time, so the wheel's 150 is
   arithmetic over the parsed loop, not a hand count. **Right:** the
   per-board totals — identical shared bars in each board's identity
   colour, the hub-ring labels the only delta (6/7/6), and the
   build-once ledger beside them.

The generator is ``docs/tools/gen_perf_assembly.py``. What this
ladder does *not* count: the bytes each object costs is the **static
footprint** above, the taps to reach any bar are :ref:`the tap ladder
<taps-ladder-perf>`, and what the LVGL pool charges when a *bake*
out-funds the population is the render-path story of :doc:`the
architecture <../implementation/architecture>`; the population's
navigation — who may visit whom — is the reachability matrix of
:doc:`the UI index <../ui/index>`.

.. _glyph-ladder-perf:

The glyph ladder (how many glyphs every panel carries — and what the rasters cost)
-----------------------------------------------------------------------------------

**The short version.** The assembly ladder counted the objects; this
one counts the **ink**. Every word the UI speaks comes from **seven
faces** baked **three times** — ``components/ui/fonts/`` (96/48/32 px,
the S3), ``fonts_p4/`` (148/74/50, the P4), ``fonts_knob/`` (74/37/25,
the knob) — 21 ``lv_font_conv`` files with identical ``lv_font_t``
symbol names, so the tier is a CMake directory choice and no consumer
ever mentions a panel. This ladder prices what the 21 files carry:
**235 glyphs and 81 KiB of raw 4 bpp raster on the S3, 236 and 213
KiB on the P4, 236 and 44 KiB on the knob** — the same copy, three
densities — and what the firmware statically asks them to paint. The
bill is not recalled: every number is parsed from the 21 files and
the two asker files (``screens.c`` + ``styles.c`` — a repo-wide scan
for ``&ui_font_`` finds no other file) with SystemExit-failing
parsers:

* **identity across tiers**: all 21 headers parse to ``Bpp 4`` +
  ``--no-compress`` with ``--size`` matching the ``Size:`` line, and
  six of the seven faces share their Opts charset **byte-equal**
  across tiers (only ``--size`` differs) — a charset drifting between
  tiers kills the figure;
* the px triples (96/48/32 · 148/74/50 · 74/37/25) are asserted
  **against the board pages** — ``devices/p4.rst`` and
  ``devices/knob.rst`` must still carry the literals the files bake;
* **carried sets** come from the files' own ``/* U+XXXX */``
  annotations: digits ``0-9 : -`` (12), digits ``+ .`` (13), the two
  SSID faces the full printable 0x20–0x7E (95 each), the weekday face
  **derived from the two weekday tables themselves** (``s_day_abbr``
  ``actions.c:986`` + ``ALARM_DAY_ABBR`` ``screens.c:1033`` plus
  ``-``) and asserted EQUAL to the 12 baked codepoints both ways,
  and the icon faces from the decoded UTF-8 of the ``GLYPH_*``
  defines (escape bytes vs their own ``U+XXXX`` comments);
* **asked ⊆ carried**: the ring's per-board glyph sets, the xmark and
  the stopwatch draw inside the solid/regular sets, and every
  character of the wheel's four ``s_wheel_sets`` registers (88
  distinct) sits inside the 32 SSID face's 95 — an escaping register
  char is a SystemExit (the charset bug the stopwatch define still
  mourns: *"was U+F9F2, absent -> a box"*, ``screens.c:87``);
* **ask counts**: ``screens.c`` 43 + ``styles.c`` 4 references, per
  face exactly 12/6/17/3/2/5/5 across the seven faces (the digits
  faces are the workhorses, the weekday face is two labels), and the
  repo-wide scan refuses to draw if a third asker appears — the knob
  and the clocks' main ask **nothing**; they ride ``screens.c``.

What the bars say: **the P4 pays for pixels, the knob for ink** — the
P4's tier costs **2.6×** the S3's where the pure density square
(720/466)² would predict 2.39×, and the knob pays **54 %** of the
S3's bill where (360/466)² predicts 60 %: 4 bpp rasters track the
square of density but not by the letter (per-glyph box rounding at
bake time), and the P4/knob regular icon set additionally carries
one glyph the S3 never asked for. The same-face pairs read as
constant-projection: the 48 SSID face costs 34.0 / 78.4 / 20.4 KiB
at 48 / 74 / 37 px. **Right:** carried-vs-asked per face — the
weekday face is **12 = 12** (the alphabet of "Mo-Fr", nothing
carried that the tables can't spell), the solid icons **6 = 6**, and
the 32 SSID face holds its 88 register chars inside 95; the digits
and 48 SSID faces carry no static stub because their content is
runtime-formatted (italic, honest). The one strike the ladder draws
in red: the **knob's regular icon set carries the clock U+F017 and
never draws it** — the ring's clock entry is
``#if defined(CONFIG_BOARD_P4)`` code, but the knob's tier was baked
from the P4's icon Opts, so it inherited the glyph (the parsed
per-board asked sets are S3 2 · P4 3 · knob 2 against carried 2 · 3 ·
3).

.. figure:: ../_static/figs/perf_glyphs.png
   :alt: Left, grouped raster-byte bars for the seven faces across the three tiers; right, carried-vs-statically-asked glyph counts per face and per-board totals 235/236/236 glyphs at 81/213/44 KiB
   :figclass: align-center

   The glyph ladder. **Left:** every bake's raster bytes (4 bpp,
   ``--no-compress``) on a log axis — one three-bar group per face
   (S3 blue, P4 vermillion, knob violet), px on the wire; the same
   face at three densities is the comparison the board pages assert
   in prose. **Right:** carried glyphs (steel dot) against the
   statically-asked sets (open circles: weekday tables, wheel
   registers, icon asks), runtime-formatted faces marked with an
   italic stub, and the per-board totals at half scale with true
   counts in the bars. The knob's inherited-and-never-drawn clock
   glyph is the only red annotation.

The generator is ``docs/tools/gen_perf_glyphs.py`` — its parsers are
the figure-time twin of :ref:`the type sketch <type-family-ui>`. What
this ladder does *not* price: the objects these faces paint
(:ref:`the assembly ladder <assembly-ladder-perf>`), the RAM the
fonts cost at runtime (the static footprint above), and the factory
partition the rasters land inside — that budget is
:doc:`the binary page <binary>` (whose chronology already named the
keyboard's widened font as a size event). The per-board font
statements themselves stay with the board pages: :doc:`S3
<../devices/s3>`, :doc:`P4 <../devices/p4>`, :doc:`knob
<../devices/knob>`.

.. _silicon-ladder-perf:

The silicon ladder (how often each chip is asked)
-------------------------------------------------

**The short version.** The family's other axes count bytes (bus,
storage, writes, footprint) or waits (pacing, patience, response,
freshness) — none of them counts **interrogations**: how many times a
day each board's firmware asks a chip for something, while the hand
is on (the sleep story is owned by the pacing and patience ladders
and the dark hours they price). Every bar is parsed at figure time:
the shared beat ``CONFIG_LV_DEF_REFR_PERIOD`` asserted **equal across
all three** generated headers (it is also the LVGL indev read period,
``lv_indev.c:105`` — so every touch chip answers 2,618,181 reads a
day), the tilt monitor's 1 s ask against the QMI8658C's parsed
1000 Hz ODR, the clocks' ``RTC_POLL_INTERVAL_MS`` 1300 feeding the
expander-INT + PCF-CTRL2 double read, the encoder's nominal
``ENC_POLL_MS`` 3 clamped to one tick at the knob's parsed 100 Hz,
the hour-centre epoch ``hour * 3600 + 1800``, the 60 s battery gauge,
and the day scan at the ``moon_event`` worker.

The ladder's two comparisons are gaps, not rates. **The 1000×:** the
IMU streams 86,400,000 samples a day (the CTRL2 "1000 Hz = 3"
setting, a hatched chip-side ghost — never billed to a board) while
the firmware asks for 86,400; one sample in a thousand is read, and
the departure model that wakes the device lives entirely inside that
one-in-a-thousand. **The 3×:** the dial's 3 ms nominal poll cannot
exist at a 100 Hz tick — ``pdMS_TO_TICKS(3)`` is zero, the code's own
``poll < 1 ? 1 : poll`` clamp lands it at 10 ms, and the 28,800,000
nominal becomes an effective 8,640,000 (the response ladder already
priced what the user pays for that clamp; here it is priced as
polls). The right panel is the presence matrix: filled dots are asked
at the printed rate, open dots are **zero legs with a parsed
reason** — the P4's IMU probe failing ("moon tilt disabled", with the
P4 board page never mentioning the chip while the S3 page does), the
RTC leg compiled out by the three ``#if !CONFIG_CLOCK_USE_PCF85063A``
legs and the undefined P4-header macro, the knob's ``imu_stub.c``
answering ``false``, no RTC driver in the knob's own sources, the
hour cache whose whole module is the ``KNOB_BOARD`` side of
``UI_BOARD_SRCS`` (four files, emptied — "no SD socket"), and the
radio re-sync's airplane-screen gate the knob never stands on. The
same 1.3 s metronome appears three times with three different
truths: 66,461 two-read I2C interrogations on the S3, a compiled-out
timer callback on the P4, a software minute-tick with no chip behind
it on the knob. Not drawn: the alarm ring player (event-driven, and
:ref:`the ring ladder <loudness-ladder-perf>` owns when), the
``esp_timer`` daemon's own housekeeping, and every sleep-window
quiet — the bars are *awake-attention* rates. Every figure number
dies with its source: the CST9217 driver is lock-only (asserted in
``dependencies.lock``, no address ever drawn), while GT911 0x5D/0x14
and PCF85063A 0x51 are parsed from the on-disk headers, the
``UI_BOARD_SRCS`` quartet is parsed name-for-name from the component
CMakeLists, and the knob-zero claims are walked over the knob's own
sources (the vendored ``managed_components`` package it never links
is excluded by rule). The generator is
``docs/tools/gen_perf_silicon.py``; who holds each of these hands is
:ref:`the crew sketch <task-roster-arch>` (its amber arrows *are*
these legs), what one interrogation costs on the panel bus is
:ref:`the bus ladder <bus-ladder-perf>`, and what the card asks
*write* is :ref:`the write ladder <write-ladder-perf>`.

.. figure:: ../_static/figs/perf_silicon.png
   :alt: interrogations per day per chip leg per board on a log axis with hatched chip-side ghosts, and the per-board presence matrix of filled, open and absent legs
   :figclass: align-center

   The silicon ladder. **Left:** every periodic interrogation leg as a
   group, one bar per board in its identity colour (S3 blue, P4 teal,
   knob amber) on one log count axis — with the two hatched ghosts
   that are never billed (the IMU's 86,400,000/day stream behind the
   86,400 asks, the dial's un-clamped 28,800,000 nominal behind the
   clamped 8,640,000), and every zero leg drawn as an open dotted bar
   with its parsed reason in italics. **Right:** the presence matrix,
   leg × board — filled dots carry the count, open dots mark the
   parsed zeros, ``×`` marks the legs the board does not build at all.

.. _certainty-ladder-perf:

The certainty ladder (what each of the thirteen costs the finger, and buys against error)
------------------------------------------------------------------------------------------

**The short version.** Every ladder above prices the machine that
shipped — the tap ladder priced the wheel that won the study. None has
priced the **twelve counterfactuals**: what the methods the round
keyboard lab rejected or deferred would have cost the finger and
bought against error, on the corpus's published numbers. The ladder
makes the study's own trade visible — **certainty over speed** — as
one ratio: the wheel's wall-clock entry bill (the report's own
back-of-envelope, 16 characters in 30–60 s ⇒ **16–32 CPM**) pays
roughly **6.5×** against the corpus's fastest published class (watch
trace, 29–33 WPM ⇒ 145–165 CPM) and roughly **4.0×** against watch
QWERTY tap (18–20 WPM ⇒ 90–100 CPM), and buys the one property no
measured method has — a double-entry that is structurally impossible
(release-only commit). Every bar is parsed at figure time from three
tracked sources: the dated record
(``docs/research/circular-keyboard-alternatives.md`` — the 18–20/14–15
pair, the trace 29–33, the Crownboard 2.3 %/0.53 % pair, the
37,370-volunteer 36.2 WPM/2.3 % phone anchor, the CHI 2025 dwell
ceiling, the in-car "> 94 % for all methods", the 0.7–1.5 s motion
pair and its wall-clock pair, the A–M table whose verdict cells colour
the bars); the operable record (the lab's ``CARDS``/``OFF`` tables,
ranked and classed, their Speed rows re-checked against the report's
own numbers, the widget timings ``const T = 700`` / ``< 400`` /
``>= 400`` / ``/ 1000`` re-read, the ring geometry
``LARC = 360 / 26`` re-derived into the 27.7°/41.5° spans); and the
shipped wheel (``WHEEL_SHARE``, the twelve groups name-for-name, the
25/25 registers, and the in-tree 400 ms dwell
``CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME`` seeding ``lv_indev.c``). The
fidelity notes the docs pages owe the frozen mocks are asserted
**together** with the firmware's pairing comment — the lab card and
widget still draw ⊗/trash while ``screens.c`` records the 2026-11-08
move to check-commit/xmark-discard, and the ladder refuses to draw
while the RST notes and the firmware comment disagree about it.

What the bars say. The wheel's bar is a wall-clock band with two
hatched ghosts that are never summed into it: its own **motion-only
floor** (0.7–1.5 s per commit before aim and settle ⇒ 40–86 CPM — what
real glass *could* lift it toward, which is exactly the pending
device check) and the phone anchor (36.2 WPM ⇒ 181 CPM, the 2D
ceiling when a panel can host a QWERTY — the finding-7 counter-
evidence, which this 35 mm disk cannot). An open circle marks the
report's own **"4–8 CPM" slip**: it implies 7.5–15 s per commit and
follows from neither of the report's own pairs (both pages now carry
the corrected arithmetic, and the parser dies if the gloss ever
becomes consistent upstream). The three SHIPs each trade a different
piece of the certainty: multi-tap keeps the 30° target but drops the
live preview (40–60 CPM, strictly between the wheel's bill and the
tap band); dwell keeps the preview and pays the CHI 2025 **ceiling**
(100 CPM — drawn inside the watch-tap band, asserted); auto-scan keeps
the certainty but serialises on the scan **floor** (60 CPM) and carries
the corpus's best measured errors (0.53–2.3 %, Crownboard). The right
panel prices the error side: the wheel's diamond is a **structural
zero** (double-entry impossible; no real-glass benchmark exists —
the honest cell is the italic, not a number), QWERTY carries its
14–15 % WER band, and every method the corpus never measured is an
open circle with its parsed reason — including the IMU twist ("a
twist *is* a roll change") and the T9 decode ("passwords defeat the
word list"). The in-car study's "> 94 %" accuracy note is the panel's
footer, not a row: accuracy was never the differentiator — the dial's
case is glance and distraction. And the two context ghosts (phone 2.3
%, handwriting 25 % WER) are never summed into a row's evidence.

.. figure:: ../_static/figs/perf_certainty.png
   :alt: the thirteen keyboard methods on two log axes — characters per minute per method on the left with hatched ghosts for the motion floor and the phone anchor, published error percentages on the right with the wheel's structural zero and open circles for every parsed no-number row
   :figclass: align-center

   The certainty ladder. **Left:** characters per minute per method on
   one log axis, bar colour = the recorded verdict (KEEP blue, SHIP
   green, HOLD amber, REJECT red, EXPERIMENTAL violet, v2 BOARD teal,
   INFEASIBLE grey), range bars for measured bands, an edge tick for
   the scan floor and the dwell ceiling, open dotted stubs with italic
   reasons for the zero rows, and three ghosts that are never summed
   in — the wheel's 40–86 CPM motion floor, the phone's 181 CPM, and
   the report's 4–8 CPM slip as an open circle. **Right:** the same
   thirteen on published accuracy evidence (log %) — filled dots and
   bars for measured figures, the wheel's diamond for the structural
   zero, open circles with parsed reasons where the corpus never
   measured, grey ``×`` for the off-hardware cards.

The generator is ``docs/tools/gen_perf_certainty.py``. What this
ladder does NOT count: the shipped wheel's per-tap bill and the 63/64
cap pair (:ref:`the tap ladder <taps-ladder-perf>`), what the wheel's
150 objects cost at boot (:ref:`the assembly ladder
<assembly-ladder-perf>`), who vouched for whom and which constraint
struck whom down (:ref:`the thirteen in one picture
<thirteen-sketch-lab>`), and the wheel's own commit machine
(:doc:`the round keyboard <../keyboard>`). The literature rows it
converts are the HCI shelf of :doc:`the sources page
<../reference/sources>`.

.. _guard-ladder-perf:

The guard ladder (how many locks stand between the hands)
----------------------------------------------------------

**The short version.** Every earlier ladder counts a hand doing
something: the silicon ladder counts interrogations, the bus ladder
counts bytes crossing a wire, the patience ladder counts allowed
waits. None of them has counted the **contention machinery** — the
objects that decide which hand goes first. This ladder counts them
out of the live sources: the app owns exactly **three mutexes, two
event groups, one task-notify pair and four SEQ_CST fences — and zero
queues** (``xQueueCreate`` never fires outside the vendored BSP). The
wait-site bars tell the sharper story: seventeen image-key takes all
say ``portMAX_DELAY`` — a publish block is a memcpy plus a gather,
never a bus, so there is nothing to time out on — while the panel key
carries the **entire timeout policy of the app**: the 2 s flush-wait
gate, the 500 ms quiesce, the drain verify's audited forever-hold,
and the watchdog's single 3 s bounded probe. Against the healthy
~40 ms drain the 3 s probe leaves **75× headroom**; even the
full-anomaly ~620 ms hold leaves 4.8×.

The graph is not hand-drawn; the generator **parses the whole census
out of the live sources** — and its parsers are the gate, SystemExit-
failing when a guard appears, vanishes, or changes its mind:

* exactly **3** mutex-creation sites (``moon_ui.c:2553-2554`` and the
  knob's ``main.c:366``) and ``xQueueCreate`` **zero** in app code —
  the zero is an assertion, not an absence of evidence;
* all **17** ``s_rgb_mtx`` takes are ``portMAX_DELAY``, and the
  ``s_panel_mtx`` take-policy **multiset** matches exactly
  ``[6x portMAX_DELAY, 2 s witness, 500 ms quiesce, 3 s probe]`` — a
  new take or a retimed one stops the figure;
* the **one-way lock order** is asserted per site: between every
  panel-key take and its give no image-key take appears — the single
  both-keys function ``moon_ui_screen_switched`` is checked for the
  sequential order ``b_panel_t < b_panel_g < b_rgb_t`` instead;
* the drain witness is pinned to the S3: ``moon_panel_drain_verify``
  is one take inside ``#if !defined(CONFIG_BOARD_P4)`` with the P4 leg
  ``(void)site;`` — the presence matrix bills the S3 **9** panel takes
  and the P4 **8**, the same "same shared component bills zero,
  compiled out" shape the write ladder draws for the RTC; the 9 drain
  call sites and their site strings are enumerated;
* the watchdog is pinned to its literals — the spawn line
  ``"panel_wdg", 4096, NULL, 3, &s_panel_watchdog, 0``, the
  ``xSemaphoreGetMutexHolder`` holder-naming, the "PANEL IO STUCK"
  restart line, and the 2000/3000 ms defines;
* the knob's mux is pinned to **one** ``xSemaphoreTake`` in
  ``knob_ui.c`` behind the holder+depth wrapper ("NOT recursively
  takable", the non-owner guard), with its **11 entries** recounted
  across four files (6 internal, the park loop, the alarm wake, the
  dial, the two seam legs), and its seam is asserted **mutex-free** —
  the ``moon_ui_init`` shim carries no lock;
* the two event groups are pinned to their waits (``WaitBits`` bounded
  ``pdMS_TO_TICKS(timeout_ms)`` with callers passing 10000 from
  ``clock.c`` + ``actions.c``, 20000/2000 on the knob; 8000 ms join);
  four fences in two files; the notify pair at ``moon_ui.c:1106``/
  ``:1132``;
* the compiled-out facts come from the build, not the prose: four
  ``UI_BOARD_SRCS`` entries emptied by ``if(KNOB_BOARD)``, the S3
  header carrying ``CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH 1`` where the P4
  header lacks the symbol, and the orbit gate unset in all three
  ``sdkconfig.h`` files (the ghost leg stays a ghost).

What the bars say: **the clocks hold two keys, the knob holds one,
and nobody holds a queue**. The image key is the wide bar — seventeen
sites, all forever — because the family's invariant (one atomic
whole-panel ``lv_refr_now``) *requires* a publish block that cannot
half-fail. The panel key is where every bounded wait in the firmware
lives, and all of them exist for one reason: the lost-GPSPI-DMA-
completion wedge, which the drain witness converts into an audited
hold (75× headroom) and the watchdog into a holder-named restart.
**Right:** the presence matrix is the compiled-truth view — the two
clock keys are ``x`` on the knob because ``UI_BOARD_SRCS`` empties
``moon_ui.c`` there; the knob's mux is ``x`` on the clocks because
their seam takes the BSP's panel path, not the LVGL task's own mutex;
the adapter mutex is one app take on each clock (``main.cpp:579``,
boot build, ``-1``) and absent on the knob; and the three open circles
on the ``xQueueCreate`` row are the figure's way of drawing "the
family hands off on fences + flags".

.. figure:: ../_static/figs/perf_locks.png
   :alt: per-guard wait-site bars (solid forever, hatched bounded) and the guard-presence matrix across S3, P4 and knob
   :figclass: align-center

   The guard ladder. **Left:** one bar per synchronization object,
   solid violet = ``portMAX_DELAY`` sites, hatched red = bounded
   timeout, each note parsed from the call site; the red pair of notes
   prices the watchdog's 75× headroom and the knob's 11-entries-on-1-
   take wrapper. **Right:** the presence matrix — filled dot = built
   and taken with the count above, open circle = zero with its parsed
   reason, ``x`` = the guard is not built on this board (the knobs'
   two ``x`` marks are ``moon_ui.c`` excluded wholesale; the P4's
   panel-key count drops to 8 because the drain take is ``#if``'d
   out).

The generator is ``docs/tools/gen_perf_locks.py``. What this ladder
does *not* count: how often the guards are asked is the **silicon
ladder** (and the 33 ms indev beat prices the hands that queue behind
them), the waits they *buy* are :ref:`the patience ladder
<patience-ladder-perf>`, the priorities that decide who wakes first
are :ref:`the scheduler ladder <scheduler-ladder-perf>`, the relay
that sequences the two keys is :ref:`the guards sketch
<guards-arch>` with its twin :ref:`the crew sketch <task-roster-arch>`,
and the wedge these guards convert into bounded states is the witness
family of :doc:`the display pipeline
<../implementation/display>`.

The performance decisions, ranked
---------------------------------

The measured costs justify the spin/sweep era's performance decisions
(both subsystems retired 2026-11-13 — the list stays as the record of
what the numbers bought; the relief cache and the dusk's
constant-projection economics outlive them), in the order they
mattered:

1. **The relief gradient cache** (:doc:`the C core <../implementation/core-c>`) — the single largest
   win: ~3.3 s → ~1.3 s (S3, 466 px) by turning six dependent
   PSRAM loads per pixel into one packed 2-byte load. Worth ~2 s
   per frame on the S3; on the host (where loads are free) the same
   cache is worth the 31 % the A/B plot measures.
2. **The once-baked spin** (:doc:`rotation and the LUT <../implementation/rotate-lut>` and :doc:`the animation pipelines <../implementation/animations>`, retired
   2026-11-13) — the 8 ms gather against the 1.3 s bake is what made
   the burn-in rotation free; without it, the spin was a 1.3 s
   re-bake per hand motion.
3. **The constant-projection dusk** (:doc:`the dark side <../science/darkside>` and :doc:`the animation pipelines <../implementation/animations>`) — the one-time
   1.6 s map build against the per-frame 1.3 s bake; the dusk's
   43 s runs off ~50 ms re-projects.
4. **The reduced-res sweep lever** (:doc:`the animation pipelines <../implementation/animations>`, retired with the sweep
   2026-11-13) — the 240 px intermediate
   (~0.28 s) against the native bake: the sweep's 1343
   intermediate frames were ~56 % cheaper on the per-frame math.
   The shipped default since 8954e88 was the NATIVE bake (S3 400 px,
   P4 650 px — every intermediate frame pixel-identical to the
   static/spin bakes), a quality decision that cost boot time:
   measured ~36.5 min wall on the bench S3 (2026-11-08; per-frame
   bake 1211–1354 ms @ 400 px against the earlier ~0.72 s note —
   the 864-albedo PSRAM-load floor). The TF cache paid this cost
   once: the cold card baked every intermediate (at hourly
   granularity, ~19 min S3, measured 1164 s), the warm card replayed
   it in ~2 min —
   but on a *card-less* boot the native lever applied at full
   price, which is why it mattered. ``UI_MOON_ANIM_BAKE_PX`` was
   the single Kconfig value to drop it (240 px → arithmetically
   ~11 min).
5. **The 25-row strips / 23,300 B cap** (:doc:`the display pipeline <../implementation/display>`) — not a
   *speed* decision (it is the NO_MEM fix) but a *throughput* one:
   the single 400×400 window write at 30–40 ms is the spin's panel
   budget, and the cap is what makes the 320 KB stream stage in
   ~31 KiB blocks that fit the internal heap.

The honest limit: the S3's bake is ~0.7–1.3 s, and the product
*accepts* it — the sweep was a ~37 min card-less boot story at the
native default, ~19 min on a cold TF card and ~2 min on a warm
one (frame count
quadrupled on 2026-11-07: users watched the old 2.8 min sweep stutter,
so the boot grew longer and smooth; the TF cache, 2026-11-08, made
the length a once-per-calculation cost) before its 2026-11-13
retirement; the spin (likewise retired) was
once-baked; the dusk is map-re-projected. The alternative (a
faster silicon, a smaller disk, a coarser relief) would buy
speed and lose *physical correctness at this resolution* — the
gate's numbers (:doc:`the image gate <image-gate>`) are the price of the resolution, and
the performance architecture's job is to make the correctness
affordable, which is what it does.

The next page is the other budget — the *flash* budget, the
binary-size chronology that keeps the app inside its partition
(:doc:`the binary-size chronology <binary>`).
