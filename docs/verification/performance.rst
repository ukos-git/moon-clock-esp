Performance, measured
=====================

This page is the performance comparison the project owes: the
same moon bake, timed on **four engines** — the C core on the host,
the C core on each board (the Waveshare ESP32-S3-Touch-AMOLED-1.75
and the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC), the Python prototype
(numpy, float64, vectorized), and the JavaScript port (V8, scalar) —
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

The four engines
----------------

- **C core (host)** — float32, scalar, ~ns load latency: the
  algorithm's compute cost.
- **C core (S3)** — float32 + FPU, Xtensa, octal PSRAM at ~100s ns
  random load: the shipping product (400 px disk).
- **C core (P4)** — float32 + RISC-V vector, X16 PSRAM (instruction +
  data shared): the second product (650 px disk).
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
  reason the sweep bakes 240 px and lets the *draw* do the
  up-scaling.

The measured log-log slopes in the figures below are the empirical
signature of these classes: ~2.0 against linear size (≡ linear in
pixels) for every per-pixel step, and 0.0 for the one-time builds.

The bake, across sizes and engines
----------------------------------

.. figure:: ../_static/figs/perf_engines.png
   :alt: Bake time vs disk size for all five engines, log-log
   :figclass: align-center

   Single-frame bake time vs disk size, re-measured 2026-11-07 (median
   of 5). The two host C builds sit together at the bottom (their
   marker *shape* tells S3-asset from P4-asset); the device points
   (hollow markers) are the accepted-capture wall times from
   :doc:`on-device verification <device-verify>`, plotted on the same
   millisecond axis. The dashed gray line is the ``size²`` reference
   through the host curve.

.. figure:: ../_static/figs/perf_vs_pixels.png
   :alt: Bake time vs pixel count, linear, through the origin
   :figclass: align-center

   The same data against *input count* (pixels = size²) on linear
   axes: every engine is a straight line through the origin — the
   O(N)-in-pixels claim, shown rather than asserted. The host C fit
   is 13.2 ns per pixel.

The plot's story, in four facts:

1. **The C core is the fastest engine at the device's sizes — by a
   wide margin.** At 400 px the host C bake is 2.34 ms; the Python
   reference is 54.3 ms (23×); the JS port is 10.4 ms (4.4×). The C
   core is not a *re-implementation* of the Python model for speed —
   it is the *same model* written for the constraint (float32, a
   gradient cache, no per-pixel allocation), and it runs the model
   faster on a 2025 desktop than the vectorized reference does.
2. **The device cost is memory, not math.** The S3's 400 px bake is
   ~1.2–1.35 s (measured 2026-11-08, 864 albedo; an earlier capture
   read ~0.72 s) against the host's 2.34 ms — ~500×. The FPU work is
   identical (the host C bake *is* the FPU work, at 2.34 ms); the
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
   (V8, scalar, float64) is 4.4× slower than the host C at 400 px —
   the scalar port pays the float64 + no-cache cost without a FPU
   head start — and the Python reference (SIMD, float64, but the
   full-resolution per-pixel gradient *without* the cache) is 23×.
   The cache is worth ~6× of the C core's speed against the
   reference model, on the same silicon. (The JS port is *faster
   than the Python reference* at every size — the browser sim runs
   the *cached* gradient, the prototype does not.)

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
- **The shade-map build is size-independent** (~2.5 ms on the
  S3-asset build, ~13 ms on the P4-asset build, at every bake size):
  it builds the *asset* map (864×432 S3 / 2048×1024 P4 — the 5.2×
  ratio is 5.6× the pixels), and the display size only affects the
  re-project. That is the dusk's economics in one curve — a one-time
  ~2.5 ms (host) / ~1.6 s (S3) build buys a per-frame re-project that
  scales with display size (0.3–3.0 ms host, ~50 ms S3) instead of a
  per-frame *bake* (which would be 0.7–1.3 s on the S3).
- **The rotate is ~1/13th of the bake** (0.18 ms vs 2.34 ms at 400
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
   product size: **relief off saves 37 %, the display path costs
   +27 %** — and the shape of the trade-off holds across the size
   range, which is why the sweep can trade resolution for frames.

The display path's +27 % is the backlit night side (a large fraction
of the disk at a crescent) running the ``moon_backlit_night`` branch
per dark pixel, plus the soft-terminator smoothstep at the terminator.
That 27 % is the *art pass's* performance cost (:doc:`the artistic pass <../science/art-pass>`), paid by the S3 at
~0.19 s per 400 px frame — a price the sweep's 240 px bake (0.28 s
wall) and the spin's once-baked model make affordable. The relief
cache's 37 % is the cheap side of that trade: the one-time ~1.3 s
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

The *pipeline-level* lesson is the ratio that makes the product: the
sweep and dusk are **display-paced, not bake-paced** (the per-frame
display cost — ~0.23 s on the S3 — is *less* than the bake, so the
bake task paces and the display keeps up with backpressure), and the
spin is **gather-paced, not bake-paced** (the 45 ms frame against a
1.3 s bake is the ~30× — 150× counting just the 8 ms gather — that
makes a *free* spin off a once-baked frame possible). A pipeline
whose display cost exceeded its bake cost would show the backpressure
failing (the ``shown in`` deltas tracking the bake) — which is the
verification signature :doc:`on-device verification <device-verify>` checks.

The device CPU (one-core shares)
--------------------------------

.. figure:: ../_static/figs/perf_cpu.png
   :alt: Horizontal bars of measured one-core CPU shares
   :figclass: align-center

   On-device CPU shares from the ``CONFIG_FREERTOS_HZ`` probe captures
   (the task table in :doc:`the architecture <../implementation/architecture>`). The
   hub-idle bar carries its measured 7–9 % range.

The budget's shape is the architecture's shape: the *bake* is on a
dedicated core (core 1, the spin/bake task) and the *UI* is on
core 0 (LVGL + touch + clock) — a bake that starves the UI is a
design bug, not a timing accident. The two-core split is what lets
the sweep run for ~37 min (S3, native 400 px intermediate bake,
2026-11-08 capture; ~11 min if ``UI_MOON_ANIM_BAKE_PX`` is dropped to
240) — that is the *card-less* path; with the TF cache mounted the
same sweep replays from the card in ~5 min warm (:doc:`the
animation pipelines <../implementation/animations>`). Either way
the touch pipeline never sees a bake. The hub idle sits at 7–9 % because the LVGL adapter's
``task_min_delay_ms`` floor parks the loop at ~93 Hz — the pre-fix
3500 Hz spin at 73–91 % was the ``vTaskDelay(0)`` yield, recorded in
:doc:`the architecture <../implementation/architecture>`.

The performance decisions, ranked
---------------------------------

The measured costs justify the project's performance decisions, in
the order they matter:

1. **The relief gradient cache** (:doc:`the C core <../implementation/core-c>`) — the single largest
   win: ~3.3 s → ~1.3 s (S3, 466 px) by turning six dependent
   PSRAM loads per pixel into one packed 2-byte load. Worth ~2 s
   per frame on the S3; on the host (where loads are free) the same
   cache is worth the 37 % the A/B plot measures.
2. **The once-baked spin** (:doc:`rotation and the LUT <../implementation/rotate-lut>` and :doc:`the animation pipelines <../implementation/animations>`) — the 8 ms gather
   against the 1.3 s bake is what makes the burn-in rotation
   free; without it, the spin would be a 1.3 s re-bake per hand
   motion.
3. **The constant-projection dusk** (:doc:`the dark side <../science/darkside>` and :doc:`the animation pipelines <../implementation/animations>`) — the one-time
   1.6 s map build against the per-frame 1.3 s bake; the dusk's
   43 s runs off ~50 ms re-projects.
4. **The reduced-res sweep lever** (:doc:`the animation pipelines <../implementation/animations>`) — the 240 px intermediate
   (~0.28 s) against the native bake: the sweep's 1343
   intermediate frames are ~56 % cheaper on the per-frame math.
   The shipped default since 8954e88 is the NATIVE bake (S3 400 px,
   P4 650 px — every intermediate frame pixel-identical to the
   static/spin bakes), a quality decision that costs boot time:
   measured ~36.5 min wall on the bench S3 (2026-11-08; per-frame
   bake 1211–1354 ms @ 400 px against the earlier ~0.72 s note —
   the 864-albedo PSRAM-load floor). The TF cache pays this cost
   once: the cold card still bakes every intermediate (at hourly
   granularity, ~19 min S3), the warm card replays it in ~5 min —
   but on a *card-less* boot the native lever applies at full
   price, which is why it still matters. ``UI_MOON_ANIM_BAKE_PX`` is
   the single Kconfig value to drop it (240 px → arithmetically
   ~11 min).
5. **The 25-row strips / 23,300 B cap** (:doc:`the display pipeline <../implementation/display>`) — not a
   *speed* decision (it is the NO_MEM fix) but a *throughput* one:
   the single 400×400 window write at 30–40 ms is the spin's panel
   budget, and the cap is what makes the 320 KB stream stage in
   ~31 KiB blocks that fit the internal heap.

The honest limit: the S3's bake is ~0.7–1.3 s, and the product
*accepts* it — the sweep is a ~37 min card-less boot story at the
native default, ~19 min on a cold TF card and ~5 min on a warm
one (frame count
quadrupled on 2026-11-07: users watched the old 2.8 min sweep stutter,
so the boot grew longer and smooth; the TF cache, 2026-11-08, made
the length a once-per-calculation cost), the spin is
once-baked, the dusk is map-re-projected. The alternative (a
faster silicon, a smaller disk, a coarser relief) would buy
speed and lose *physical correctness at this resolution* — the
gate's numbers (:doc:`the image gate <image-gate>`) are the price of the resolution, and
the performance architecture's job is to make the correctness
affordable, which is what it does.

The next page is the other budget — the *flash* budget, the
binary-size chronology that keeps the app inside its partition
(:doc:`the binary-size chronology <binary>`).
