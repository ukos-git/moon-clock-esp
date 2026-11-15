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
picks one bilinear tap per output pixel), and the baked disk is then
up-scaled onto the panel. ``N`` below is the bake's pixel count
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
- **up-scale to the panel** — the baked ``size``-px disk drawn onto
  the ``panel``-px round display is one bilinear sample per *display*
  pixel: **O(display²)** in the LVGL/canvas draw and the flush strips
  (:doc:`the display pipeline <../implementation/display>`) — the
  reason the (pre-8954e88, retired 2026-11-13) boot sweep baked 240
  px and let the *draw* do the up-scaling.

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
   reference is 47.6 ms (21×); the JS port is 10.3 ms (4.6×). The C
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
