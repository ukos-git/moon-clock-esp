Chapter 19 — Performance, measured
==================================

This chapter is the performance comparison the project owes: the
same moon bake, timed on **four engines** — the C core on the host,
the C core on the S3, the C core on the P4, the Python prototype
(numpy, float64, vectorized), and the JavaScript port (V8, scalar) —
across **every disk size** the product renders, plus the per-pipeline
breakdowns (geometry, relief build, rotation, shade map, re-project)
and the on-device CPU costs. Every number is measured, every command
is in the text, and the numbers that come from the device are quoted
from accepted captures (Chapter 18) — the device is not touched for
this chapter.

The benchmark harness
---------------------

The host numbers come from ``host_sim/bench.c`` (a tracked bench
harness that links the *exact* device core — the same
``moon_astro.c``/``moon_bake.c``/``moon_render.c``/``moon_fixp.c`` +
vendored ERFA the device compiles, at ``-O2``):

.. code-block:: bash
   :caption: Building and running the bench

   cd host_sim
   cmake -B build -S . && cmake --build build --target moon_bench -j
   ./build/moon_bench --utc 2026-04-15T12:00 --iters 5 \
       --sizes 200 240 320 400 466 618 650

   # the A/B variants:
   ./build/moon_bench --utc 2026-04-15T12:00 --sizes 400 --no-relief
   ./build/moon_bench --utc 2026-04-15T12:00 --sizes 400 --darkzero --neutral

The bench times each step as the device runs it: ``moon_geometry``
(the ERFA ephemeris, once per bake), ``moon_relief`` (the one-time
gradient cache build), ``moon_bake`` (the per-pixel model),
``moon_rotate`` (the spin's Q15.15 gather), ``moon_shade_map_build``
(the dusk's one-time map), and ``moon_reproject`` (the dusk's
per-frame map gather). Averages of 5 iterations; the relief build is
first-run-only (the device builds it once at boot).

Host machine: AMD Ryzen 9 9950X3D (Zen 5, 16 cores, AVX-512),
Linux 7.0. The host numbers are *compute* numbers — the host's RAM
has no 32 KiB DMA cap, no staging copy, and ~ns-scale load latency,
so the host C bake measures the algorithm, not the memory system.
That is exactly the split the chapter needs: the host says *how much
work* a bake is; the device says *what the work costs* on the silicon
it ships on.

The four engines
----------------

.. list-table::
   :header-rows: 1
   :widths: 20 26 28 26

   * - Engine
     - Precision / SIMD
     - Memory
     - Role
   * - C core (host)
     - float32, scalar
     - ~ns load latency
     - the algorithm's compute cost
   * - C core (S3)
     - float32, scalar (FPU), Xtensa
     - PSRAM, ~100s ns random load
     - the shipping product (400 px disk)
   * - C core (P4)
     - float32 + RISC-V vector, X16 PSRAM
     - XIP PSRAM (instruction+data shared)
     - the second product (650 px disk)
   * - Python (numpy)
     - float64, vectorized (SIMD)
     - ~ns load latency
     - the validated reference model
   * - JS (V8)
     - float64, scalar
     - ~ns load latency
     - the browser simulator (Chapter 9)

The Python and JS comparators run the *port* paths (the validated
prototype's ``moon.renderer`` via ``animation.render_frame``; the
book's ``moonlab.js`` ``MoonLab.bake`` at the display settings,
K=0.20, soft=0.03, backlit, relief on), sized to match the C bake's
square disk.

The bake, across sizes and engines
----------------------------------

.. code-block:: text
   :caption: Single-frame bake time, by engine (2026-11-02)

   size    C host     C S3*      C P4*      Python      JS (V8)
   200     0.81 ms    —          —          14.1 ms     2.66 ms
   240     1.08 ms    ~0.28 s    —          20.6 ms     3.70 ms
   320     1.60 ms    ~0.48 s    —          39.0 ms     6.55 ms
   400     2.23 ms    ~0.72 s    —          67.2 ms     10.3 ms
   466     3.03 ms    ~1.3 s     —          92.0 ms     13.8 ms
   618     5.28 ms    —          0.60 s     168.7 ms    23.3 ms
   650     5.93 ms    —          0.96 s     183.5 ms    25.5 ms

   * S3: the 400/466 px numbers are the on-device measurements from the
     accepted 336-frame sweep capture (2026-11-01) — the bake task's
     per-frame wall at the sweep's 240 px intermediate (0.28 s) and the
     full-res final (0.72 s @ 400 px disk; ~1.3 s at the 466 px panel
     bake, the pre-backlit-era full-res number, relief-off 0.95 s).
     P4: the final full-res bake from the M3/M4 acceptance capture
     (599 ms @ 618 px, 959 ms @ 650 px).

The table's story, in four facts:

1. **The C core is the fastest engine at the device's sizes — by a
   wide margin.** At 400 px the host C bake is 2.23 ms; the Python
   reference is 67.2 ms (30×); the JS port is 10.3 ms (4.6×). The C
   core is not a *re-implementation* of the Python model for speed —
   it is the *same model* written for the constraint (float32, a
   gradient cache, no per-pixel allocation), and it runs the model
   faster on a 2025 desktop than the vectorized reference does.
2. **The device cost is memory, not math.** The S3's 400 px bake is
   ~0.72 s against the host's 2.23 ms — ~300×. The FPU work is
   identical (the host C bake *is* the FPU work, at 2.23 ms); the
   300× is the PSRAM: each pixel issues an albedo byte + a 2-byte
   gradient load from octal PSRAM at ~100s of ns of random-access
   latency, and the unroll (Chapter 11) only overlaps, never
   removes, the latency. The **relief-off floor proves it**: at 466
   px the relief-off bake is 0.95 s — the albedo load alone is the
   floor, and the gradient cache's 0.35 s of extra PSRAM traffic is
   the *cheaper* option against the naive 6-load-per-pixel
   alternative (~3.3 s, Chapter 6). The bake is
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
   full-resolution per-pixel gradient *without* the cache) is 30×.
   The cache is worth ~6× of the C core's speed against the
   reference model, on the same silicon. (The JS port is *faster
   than the Python reference* at every size — the browser sim runs
   the *cached* gradient, the prototype does not.)

The per-step breakdown (host C)
-------------------------------

.. code-block:: text
   :caption: moon_bench, 2026-04-15T12:00, -O2, 5-iter averages

   utc=2026-04-15T12:00  geo=0.0 ms/call  relief=on  darkzero=0
   size= 200  bake=    0.81 ms  rotate=  0.06 ms  mapbuild=    3.11 ms  reproj=  0.35 ms
   size= 240  bake=    1.08 ms  rotate=  0.07 ms  mapbuild=    2.74 ms  reproj=  0.50 ms
   size= 320  bake=    1.60 ms  rotate=  0.11 ms  mapbuild=    2.45 ms  reproj=  0.71 ms
   size= 400  bake=    2.23 ms  rotate=  0.18 ms  mapbuild=    2.47 ms  reproj=  1.23 ms
   size= 466  bake=    3.03 ms  rotate=  0.25 ms  mapbuild=    2.49 ms  reproj=  1.50 ms
   size= 618  bake=    5.28 ms  rotate=  0.43 ms  mapbuild=    2.46 ms  reproj=  2.65 ms
   size= 650  bake=    5.93 ms  rotate=  0.49 ms  mapbuild=    2.46 ms  reproj=  2.93 ms
   relief build (once): 19.8 ms (2097152 px DEM)

Three structure-of-cost facts:

- **Geometry is free** (``0.0 ms`` = < 0.05 ms, one ERFA series per
  frame). The ephemeris is a rounding error against the bake — the
  cheap-model decision (Chapter 5) is a non-issue at this resolution.
- **The shade-map build is size-independent** (~2.5 ms regardless of
  the display size): it builds the 864×432 body-frame map, and the
  display size only affects the re-project. That is the dusk's
  economics in one row — a one-time ~2.5 ms (host) / ~1.6 s (S3)
  build buys a per-frame re-project that scales with display size
  (0.35–2.93 ms host, ~50 ms S3) instead of a per-frame *bake*
  (which would be 0.7–1.3 s on the S3).
- **The rotate is 1/12th of the bake** (0.18 ms vs 2.23 ms at 400
  px). The Q15.15 gather + fused LUT (Chapter 12) is why the spin
  can display at 8 fps off a *once-baked* frame: the per-frame cost
  is the gather (~8 ms on the S3, 40× the host, again the PSRAM),
  not a re-bake.

The A/B variants (host C, 400 px)
---------------------------------

.. code-block:: text
   :caption: bake A/B, 400 px, same epoch

   variant                 bake
   relief on (default)     2.23 ms
   relief off              1.85 ms   (-17%: the gradient cache's 2-byte load)
   display path (dark_zero=1, backlit, soft)  2.77 ms   (+24%: the night-side branch)

The display path costs 24% more than the gate's path at the same
size — the backlit night side (a large fraction of the disk at a
crescent) runs the ``moon_backlit_night`` branch per dark pixel, and
the soft-terminator smoothstep runs at the terminator. That 24% is
the *art pass's* performance cost (Chapter 8), paid by the S3 at
~0.17 s per 400 px frame — a price the sweep's 240 px bake (0.28 s
wall) and the spin's once-baked model make affordable.

The device pipelines (accepted captures)
----------------------------------------

The per-pipeline device numbers, from the accepted captures
(Chapter 18's standard: progress + timing + zero failure lines):

.. list-table::
   :header-rows: 1
   :widths: 24 24 24 28

   * - Pipeline
     - S3 (400 px disk)
     - P4 (650 px disk)
     - Budget / note
   * - Sweep bake (240 px intermediate)
     - ~0.28 s/frame
     - (240 px path)
     - 336 frames, ~2.8 min wall (S3); 336/336, 0 WDT (P4)
   * - Sweep final (full res)
     - ~0.72 s @ 400 px
     - 0.60 s @ 618 / 0.96 s @ 650
     - one frame per boot
   * - Sweep display (per frame)
     - ~0.23 s (display+tick, resolution-independent)
     - —
     - the backpressure's fixed cost
   * - Spin frame (gather + composite + write)
     - ~8 ms + ~4 ms + ~30–40 ms = ~45 ms
     - 65-row DSI strips; 0 underrun, 0 drops
     - 8 fps = 125 ms slot; ~80 ms headroom
   * - Dusk shade-map build (one-time)
     - ~1.6 s
     - ~282 ms
     - at dusk entry
   * - Dusk frame (re-project)
     - ~50 ms
     - —
     - 1°/frame, display-paced ~0.2 s/frame
   * - Dusk total
     - —
     - ~43 s
     - 14–95 s over ``|phi*|``
   * - Screen-switch fade
     - 240 ms (12 × 0x51 steps, ~100 µs each)
     - same
     - no pixels written
   * - Relief cache build (one-time, boot)
     - ~1.3 s (the 4-wide unroll; ~4 s sequential)
     - —
     - 4 MB packed array from the 2 MB DEM

The *pipeline-level* lesson is the ratio that makes the product: the
sweep and dusk are **display-paced, not bake-paced** (the per-frame
display cost — ~0.23 s on the S3 — is *less* than the bake, so the
bake task paces and the display keeps up with backpressure), and the
spin is **gather-paced, not bake-paced** (the 8 ms gather + 40 ms
write against a 1.3 s bake is the 150× that makes a *free* spin off a
once-baked frame possible). A pipeline whose display cost exceeded
its bake cost would show the backpressure failing (the ``shown in``
deltas tracking the bake) — which is the verification signature
Chapter 18 checks.

The device CPU (one-core shares)
--------------------------------

The CPU budget, from the on-device measurements (the ``CONFIG_FREERTOS_HZ``
probe, Chapter 10's task table):

.. list-table::
   :header-rows: 1
   :widths: 30 26 44

   * - State
     - CPU (one core)
     - Note
   * - Settings hub, idle (post loop-rate fix)
     - 7–9 %
     - the 100-Hz FreeRTOS tick + the LVGL adapter's 1 ms
       ``task_min_delay_ms`` floor → the loop at 93–95 Hz (the
       pre-fix 3500 Hz spin at 73–91 % was the ``vTaskDelay(0)``
       yield, Chapter 10)
   * - Spin render (post CPU cut)
     - ~48 % of core 1
     - the 8 ms gather + the 4 ms composite; the panel write is DMA
       (CPU idle)
   * - Sweep bake task
     - core 1, one task
     - the bake paces the sweep; the LVGL task (core 0) only displays
   * - Wake (static frame)
     - —
     - one LUT convert + one atomic flush; no spin re-bake

The budget's shape is the architecture's shape: the *bake* is on a
dedicated core (core 1, the spin/bake task) and the *UI* is on
core 0 (LVGL + touch + clock) — a bake that starves the UI is a
design bug, not a timing accident. The two-core split is what lets
the sweep run for 2.8 min without the touch pipeline ever seeing a
bake.

The performance decisions, ranked
---------------------------------

The measured costs justify the project's performance decisions, in
the order they matter:

1. **The relief gradient cache** (Chapter 11) — the single largest
   win: ~3.3 s → ~1.3 s (S3, 466 px) by turning six dependent
   PSRAM loads per pixel into one packed 2-byte load. Worth ~2 s
   per frame on the S3.
2. **The once-baked spin** (Chapters 12, 14) — the 8 ms gather
   against the 1.3 s bake is what makes the burn-in rotation
   free; without it, the spin would be a 1.3 s re-bake per hand
   motion.
3. **The constant-projection dusk** (Chapters 7, 14) — the one-time
   1.6 s map build against the per-frame 1.3 s bake; the dusk's
   43 s runs off ~50 ms re-projects.
4. **The reduced-res sweep** (Chapter 14) — the 240 px intermediate
   (0.28 s) against the 400 px (0.72 s): the sweep's 335
   intermediate frames are ~56 % cheaper, and the final full-res
   frame carries the pixel-exact end.
5. **The 25-row strips / 23,300 B cap** (Chapter 13) — not a
   *speed* decision (it is the NO_MEM fix) but a *throughput* one:
   the single 400×400 window write at 30–40 ms is the spin's panel
   budget, and the cap is what makes the 320 KB stream stage in
   ~31 KiB blocks that fit the internal heap.

The honest limit: the S3's bake is ~0.7–1.3 s, and the product
*accepts* it — the sweep is a 2.8 min boot story, the spin is
once-baked, the dusk is map-re-projected. The alternative (a
faster silicon, a smaller disk, a coarser relief) would buy
speed and lose *physical correctness at this resolution* — the
gate's numbers (Chapter 17) are the price of the resolution, and
the performance architecture's job is to make the correctness
affordable, which is what it does.

The next chapter is the other budget — the *flash* budget, the
binary-size chronology that keeps the app inside its partition
(Chapter 20).
