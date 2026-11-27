# Deep Research: Progressive / Iterative Moon-Phase Startup Computation (ESP32)

Date: 2026-11-18 · Depth: exhaustive · Method: Firecrawl search + scrape (16
search angles, 30 sources incl. primary papers), grounded in the local pipeline
(`components/moon/core`, `docs/implementation/bake-reveal.rst`)

## Executive Summary

The question: the startup "now" bake (≈1.3 s on the S3 400 px disk, ≈2.4 s on
the P4 650 px, ≈991 ms hidden on the knob) currently reveals itself as
completed strips **over black** (the bake reveal). Three intuitions were on the
table: (a) show the last calculated image (the TF hour cache) instead of black
while a new frame computes; (b) show a quick cheap calculation immediately and
refine it stepwise; (c) compute every 16th pixel, upscale and show, then every
8th, then 4th — "a good method for iterative update of the calculation result."

The literature answers all three, and they converge on one design. The
progressive-rendering line (progressive photon mapping, HiPR's
hierarchical-progressive rendering) established the principle: _change the
order in which computed information reaches the screen, not the algorithm or
the final result_; the first pass is fast and low-detail, subsequent passes
refine, and the final result is identical to a from-scratch render. The
image-coding line supplies the concrete 16→8→4 mechanism the design brief
sketched: JPEG 2000's **resolution refinement in RLCP (resolution-first)
progression** is exactly "coarse grid first, then finer grids," and the
**Laplacian pyramid** formalizes it as "coarse base + detail layers." The
web-UX line (LQIP/"blur up") supplies the perception precedent: a small _blurry
version of the actual content_ reads as "loading," not "corruption" — provided
it is recognizable. And the incremental-re-rendering line (residual path
integrals, region-priority scheduling) plus e-ink partial-refresh practice
supply the embedded-scale evidence that "refine only the parts that change, on
a constrained device" is a proven pattern.

Two findings matter most for this project:

1. **The moon bake is a per-pixel _pure function_.** Because the relief
   gradient is precomputed per DEM cell (`moon_relief_build` → one packed
   16-bit load per pixel), the output of pixel `(x, y)` depends only on that
   pixel's body direction, the epoch's sun direction, its albedo texel, and its
   gradient cell — never on any other output pixel. Consequences: pixels may be
   computed **in any order and any subset**, every computed pixel is _exactly_
   its final value (no approximation), a coarse grid is a valid self-contained
   frame, and a coarse-to-fine pass that computes each pixel once costs
   **exactly one full bake** (≈1.0×, not 1.3×). This is what makes the 16→8→4
   idea _exact_ here, where it would be an approximation in most renderers.
2. **Multigrid is the wrong mental model, Laplacian pyramid the right one.**
   Multigrid's coarse-to-fine is a _convergence accelerator_ for iterative
   solvers (the coarse solution seeds the fine solve). The moon bake is a
   one-shot evaluation with no iteration to accelerate. What applies is the
   _display_ structure: a coarse-to-fine multi-resolution presentation of a
   deterministic result — precisely JPEG 2000's resolution refinement and the
   Laplacian pyramid's "base + detail layers." (See Contrarian Views.)

The recommended design (Detailed Analysis) is therefore: **keep the last good
frame on screen (no black canvas), compute the new frame on nested power-of-two
grids (16→8→4→2→1), and display each completed grid — exact pixels in place,
gaps bilinear-filled from the same grid — as one atomic whole-panel state.**
The first displayable new frame lands in roughly 5–10 ms instead of the ~1 s
black window; the sequence reads as the moon _resolving into focus_; the final
state is bit-identical to today's `moon_bake` output, so the image gate is
untouched.

## Key Findings

1. **The first frame should be a fast _low-detail approximation of the new
   content_, and later frames refine it; the final frame equals the
   from-scratch result.** This is the standing principle of progressive
   rendering. Progressive photon mapping: "a multi-pass algorithm where the
   first pass … [is] followed by any number of … passes. Each … pass results in
   an increasingly accurate … solution that can be visualized in order to
   provide progressive feedback"
   [Hachisuka/Ogaki/Jensen, PPM](http://graphics.ucsd.edu/~henrik/papers/progressive_photon_mapping/);
   NVIDIA's iRay docs state it as a product contract: "the first frame has a
   lower quality than usual, but it is available very fast. Subsequent render
   calls then improve the result obtained so far more and more"
   [iRay](https://raytracing-docs.nvidia.com/iray/api_reference/iray/mi_neuray_example_progressive_rendering.html).
   The moon bake satisfies the "final frame = from-scratch" clause for free
   because it is deterministic per-pixel (Finding below, §2).

2. **"Every 16th pixel, upscale, then every 8th" — the starting sketch — is a
   named, standardized display order: JPEG 2000 resolution refinement, RLCP
   (resolution-first) progression.** A J2K codestream decomposes the image into
   a 2:1 subband pyramid and emits packets per (precinct, resolution level,
   layer); the **RLCP progression order orders packets by resolution level
   first**, so a decoder displays the coarsest level, then adds detail level by
   level — the canonical "progressive sharpening" stream
   [UAL lecture notes](https://w3.ual.es/~vruiz/Docencia/Apuntes/Coding/Image/04-JPEG2000/index.html),
   [Wikipedia: JPEG 2000](https://en.wikipedia.org/wiki/JPEG_2000),
   [McMaster intro](https://www.ece.mcmaster.ca/~shirani/multi08/jpeg2000part1.pdf).
   The transposition to a _compute_ pipeline (emit the grids as they are
   computed instead of decoding a stream) is unremarkable — it is the same
   pyramid, same 2:1 factor, same display order. (JPEG 2000's _quality_
   refinement — bitplane refinement of the same grid — is the analog of "more
   samples per pixel" and does not apply to a deterministic bake; resolution
   refinement is the applicable axis.)

3. **The Laplacian pyramid is the mathematical formalization of 16→8→4: a
   coarse base image plus a cascade of detail layers, each level 4× the pixel
   count of the previous, summing to ≈1.33× if re-derived per level or exactly
   1.0× if each detail is computed once and reused**
   [Wikipedia: Pyramid (image processing)](<https://en.wikipedia.org/wiki/Pyramid_(image_processing)>).
   Because the bake's per-pixel function is pure (Finding 4 below), the
   "computed once and reused" form holds exactly: a stride-s grid computed at
   level s is a strict subset of the stride-(s/2) grid, so a level-ordered pass
   that shades only the _new_ grid points at each level shades every pixel
   exactly once — total shading work = one full bake.

4. **The moon bake is embarrassingly per-pixel — the enabler.** `moon_bake.c`
   header: the relief slope "is PRECOMPUTED ONCE per cell by
   `moon_relief_build()` into ONE uint16 array … the bake then reads ONE 16-bit
   cache load per pixel … instead of a 4-neighbor DEM scatter"; the per-pixel
   model is `albedo(lon_tex, lat_body) · L` + earthshine + tone LUT, all
   functions of the pixel's own body direction and the fixed epoch sun. No
   output pixel reads a neighbor. (The backlit night side and the
   `MOON_TERM_SOFT` terminator blend are also per-pixel functions of that
   pixel's `lns` — no spatial filtering.) Hence any subset/any order is exact,
   a coarser grid is a valid frame, and the coarse-to-fine display is _not_ a
   Monte-Carlo "converging estimate" (there is no variance) but a
   _deterministic multi-resolution refinement_ — stronger than what the
   progressive-rendering papers achieve (their intermediate frames are noisy
   estimates).

5. **Showing a blurry version of the _actual_ content during load is an
   established, accepted UX pattern (LQIP / "blur up") — with one caution: the
   placeholder must stay recognizable.** Mux's survey: a WebP at "16×16 …
   quality around 70%" was "sufficient detail" for the placeholder; the
   technique exists precisely because "a blurry image placeholder" beats
   nothing or a spinner for perceived load
   [Mux](https://www.mux.com/blog/blurry-image-placeholders-on-the-web),
   [Cloudinary](https://cloudinary.com/blog/low_quality_image_placeholders_lqip_explained).
   The caution from the technique's practitioners: minimize size, not quality —
   "aiming for a sensible minimum quality threshold rather than making the
   placeholder as tiny as possible at any cost"
   [csswizardry](https://csswizardry.com/2023/09/the-ultimate-lqip-lcp-technique/).
   For the moon: the 1/16 grid (25×25 on the S3) carries the phase shape and
   terminator position _exactly_ (it is exact shading, not an encoded
   placeholder) — well above that bar.

6. **"Show the last frame while refining" is the last-known-good pattern, and
   its embedded precedent is e-ink partial/fast refresh.** E-ink panels update
   only the pixels that change (fast update) or a defined window (partial
   update) — 0.3 s vs 2–4 s full refresh — with _ghosting_ (residual old
   content) as the known price
   [Pervasive Displays](https://www.pervasivedisplays.com/how-e-paper-works/fast-update-refresh/),
   [MakerGuides/ESP32](https://www.makerguides.com/partial-refresh-e-paper-display-esp32/).
   On the AMOLED the analog of ghosting is a _temporal blend_ (crossfade),
   which is exactly the project's sanctioned 0x51 fade-ramp flow — so the "old
   frame lingers under the new one" cost the e-ink literature measures has an
   already-sanctioned, controllable form here. The graphics line calls the same
   pattern _incremental re-rendering_: keep converged pixels on screen,
   recompute only what changed
   [residual path integrals](https://arxiv.org/html/2406.16302v1)
   [Hann, incremental path tracing](https://www.cg.tuwien.ac.at/research/publications/2022/HANN_2022_IPT/).

7. **Scheduling _what to compute first_ is a first-class research topic (HiPR),
   and its central result transfers: prioritize the perceptually dominant
   change.** HiPR (SIGGRAPH 2026) treats interactive rendering as scheduling:
   identify where an edit is visible, follow its influence, "render them in
   descending order … the image can temporarily contain a mixture of newly
   updated pixels and information from before the edit, but … the final result
   remains [the] fully converged [image]"
   [arXiv:2606.26612](https://arxiv.org/html/2606.26612v1),
   [80.lv interview](https://80.lv/articles/how-hipr-makes-path-tracing-feel-more-responsive-by-rendering-what-matters-first).
   For the moon, the perceptually dominant change between adjacent frames is
   the **terminator band** (the lit-region boundary and its mountain shadows) —
   a small annulus of the disk. A later refinement stage can compute that band
   at full resolution first (HiPR-style importance order) and fill the
   mare/highland rest at coarse grids — "render what matters first."

8. **No lunar prior art displays progressively — the display side is open
   ground, the shading side is settled.** NASA JPL's
   [Lunasynth](https://github.com/nasa-jpl/lunasynth), DLES terrain crafting,
   and the 3D lunar-terrain generation projects all render DEM+albedo+sun (the
   same model as `moon_bake.c`) for dataset/visualization use, offline or on
   GPUs; none addresses progressive _display_ of a deterministic lunar shade on
   a constrained MCU. Nothing in the lunar literature contradicts the proposed
   approach, and the settled DEM+albedo+displaced-normal model is exactly the
   one already validated against NASA's reference frames by the image gate.

9. **MCU-scale image refinement is proven, but the learned-upscale route is the
   wrong tool here.** On-device super-resolution on a Cortex-M33 with 640 KB
   SRAM (INT8, C-generated) is demonstrated [arXiv:2610.09098]
   (https://arxiv.org/abs/2610.09098). The moon case needs no learned prior:
   the gap-fill is a bilinear interpolation of _already-exact_ shading values
   (a few FLOPs per pixel, no model, no training, no PSRAM for weights) — the
   DL-SR result validates only that "upscale on a small MCU" fits the device
   class.

## Detailed Analysis

### 1. The four families, mapped onto the pipeline

| Family                                                                                             | What it refines                                                   | Moon-bake analog                                                         | Applicability                                                                                                       |
| -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| Progressive stochastic rendering (PPM, adaptive progressive refinement, HiPR scheduling)           | _Samples_ per pixel; intermediate frames are converging estimates | No samples — the bake is deterministic                                   | Principle only: order-of-display, final = from-scratch, "fast low-quality first frame is a contract"                |
| Embedded progressive coding (JPEG 2000 RLCP, layered/embedded DCT)                                 | _Resolution level_ then _quality_ of a fixed image                | **Resolution levels of the bake** (16→8→4→2→1 grids)                     | Direct: the 16th/8th-pixel mechanism, 2:1 factor, "displayable at every prefix" property                            |
| Multi-resolution math (multigrid, Laplacian pyramid)                                               | Coarse solution seeds the fine solve; or base + detail layers     | Not a solver — but the **grid-nesting + detail-layer display structure** | Laplacian form applies exactly (pure per-pixel function ⇒ layers are exact); multigrid's convergence claim does not |
| Last-known-good / incremental display (LQIP blur-up, e-ink partial refresh, residual re-rendering) | _Regions_ or the whole image that changed                         | Keep the previous hour's frame; refine to the new one                    | Direct for "show the last image from the TF card"; ghosting↔crossfade mapping is sanctioned by the 0x51 ramp       |

### 2. Why the coarse grids are exact (and the total cost is one bake)

Per pixel `p` at position `(x, y)`, the bake computes
`out(p) = TONE_LUT[round(albedo(tex(p)) · L(p))] + es(p)` where every operand
is a function of `p`'s own body direction, the epoch sun `S`, and the
precomputed per-cell gradient — see the `moon_bake.c` header ("reads ONE 16-bit
cache load per pixel … instead of a 4-neighbor DEM scatter"). No spatial
filtering, no neighbor reads (the soft-terminator and backlit-night terms are
per-pixel `lns` functions). Therefore:

- `out(p)` is identical whether `p` is shaded in the current row loop or in any
  other pass, at any time, on any subset of pixels — the final frame of a
  coarse-to-fine pass is **bit-identical to `moon_bake`** (same per-pixel
  arithmetic), so the gate (`check_gate.py`) validates it unchanged.
- A stride-16 grid (25×25 on the S3) is a _complete_ 25×25 rendering of the
  same scene: exact terminator position (it comes from `d = r·S`, independent
  of relief detail), exact albedo at each sampled texel — only the between-grid
  detail is missing.
- Grid nesting: stride-16 points ⊂ stride-8 points ⊂ … ⊂ all pixels. A
  level-ordered pass that shades only the _new_ points at each level shades
  each pixel exactly once. On the S3: 625 + 1 875 + 7 500 + 30 000 + 120 000 =
  160 000 evaluations = exactly the full bake (≈1.3 s total, unchanged). A
  simpler v1 that re-shades each level's full grid costs ≈1.25× — 325 ms extra
  on the S3, still inside the 5 s hourly budget; the difference-set enumeration
  is the refinement if the bench shows the extra matters.

Displaying a level: the computed set at level s is a dense (W/s)×(H/s) grid;
the displayed frame is that grid with exact pixels in place and gaps
bilinear-filled from the same grid — a self-consistent whole image, never a
spatial mix of two frames. Per-level display cost = one upscale pass + the
existing gray→RGB565 LUT conversion + one atomic whole-panel `lv_refr_now` —
the same class of work the final atomic show already pays, once per level
instead of once per strip (≈4–5 levels vs the reveal's ≈41 bands — _fewer_
drain-witness events, i.e. cheaper than today's reveal display tax, which on
the knob cost the bake ~1.07 s of swap-task preemption).

Time-to-first-new-pixel (proportional to the measured full bakes):

| Level (stride) | S3 400 px     | P4 650 px     | Knob 360 px                |
| -------------- | ------------- | ------------- | -------------------------- |
| 1/16 grid      | ~5 ms (25×25) | ~9 ms (41×41) | n/a (360 not ÷16; use 1/8) |
| 1/8 grid       | ~20 ms        | ~35 ms        | ~15 ms (45×45)             |
| 1/4 grid       | ~80 ms        | ~140 ms       | ~60 ms (90×90)             |
| 1/2 grid       | ~330 ms       | ~600 ms       | ~250 ms (180×180)          |
| 1 (full)       | ~1.3 s        | ~2.4 s        | ~0.99 s                    |

The knob's 360 = 8·45 admits only 1/8, 1/4, 1/2 as clean strides — the pyramid
is size-agnostic by construction (each level is an independent small grid), so
it simply starts at 1/8 there.

### 3. The display policy: last-known-good base + resolving sequence

Current startup ("now" leg): replay the stored hour frames → **black canvas at
+355 ms** → 40 strip bands over ~3.2 s → atomic final show. The black canvas
_actively wipes the newest stored frame off the panel_ before the bake's bands
arrive — that wipe is the UX problem.

Proposed policy (each displayed state one atomic whole-panel flush under
`s_rgb_mtx`, i.e. compliant with the invariant and its atomicity-per-state
exception — a designed sequence of consistent states, exactly the class the
bake reveal established):

1. **Never wipe.** Keep the last complete frame on screen (the newest stored TF
   hour frame after the replay; `s_frame` otherwise). The knob, with no SD
   socket, keeps whatever its disk last showed — its every-boot window is the
   only board where a truly-new first image exists.
2. **Level frames arrive on schedule.** As each grid level completes (producer
   free-runs as today; watermark per level instead of per strip), the tick
   converts one level into one displayed state: exact grid pixels + bilinear
   gaps, whole-panel, atomic. The visible sequence is the new moon _resolving_:
   phase shape and terminator position correct from the first level (they are
   exact at 1/16), crater raggedness and mountain shadows sharpening in over
   the final ~1 s.
3. **Optional 2–4 frame crossfade at the first level** (old frame → first level
   frame) to avoid any "pop": each blend frame is a complete, per-pixel-defined
   image (`α·old + (1−α)·new`), the same sanctioned class as the 0x51 ramp; the
   e-ink literature's ghosting cost is precisely this transient, bounded and
   controllable. A hard swap is the simpler fallback — old and first-level
   frames are close (hours apart), so the pop is small.
4. **Final frame**: the completed grid _is_ the full frame; the final atomic
   show lands as today (contract + witness). `s_frame` is published as today;
   nothing in the sequence touches it, so every existing heal path
   (restore-the-old-frame-whole, re-hide on the knob) is unchanged and still
   correct — an abort at level k restores the last _complete_ frame, never a
   half-resolution composite.
5. **Hourly TF hits get the same benefit for free**: with the last frame on
   screen, a hit's atomic show needs no fade-out/fade-in pair at all when the
   fetched frame is close to the displayed one — the hit's current ~2-ramp
   black window collapses to one atomic show.

### 4. What the three starting intuitions become

- **"Show last calculated image from TF card"** → policy step 1 (+ 5): the last
  frame is the base and the failure fallback; the black canvas is deleted, not
  softened.
- **"Quick calculations to show anything quickly, refine stepwise"** → the
  level-ordered bake: a level is a "quick calculation" (5–80 ms), its display
  is a designed state, the next level refines.
- **"Every 16th pixel, upscale, then every 8th"** → JPEG 2000's RLCP resolution
  refinement / Laplacian detail layers, executed as a compute order over the
  pure per-pixel function. Exact, 1.0× total cost, bit-identical final frame.

### 5. Staged plan

- **Stage 0 — delete the black canvas (small diff, big UX win).** Keep the last
  complete frame on screen during the "now" bake; final atomic show unchanged;
  TF hits skip the fade pair. Kconfig flag alongside `UI_MOON_BAKE_REVEAL`. No
  new computation at all. Benches: first-visible unchanged, but the panel shows
  a correct moon (one hour old) instead of black for the whole bake; on a TF
  hit, black window ≈ 0.
- **Stage 1 — coarse-to-fine level display (the pyramid).** Level-ordered bake
  into the existing bounce (grid points at their final positions; gaps
  bilinear-filled in the display conversion), one watermark bump + one atomic
  whole-panel state per level; pacing and gates identical to the reveal's (the
  producer still never blocks on display). Reuses `moon_bake_rows` geometry;
  the new surface is a small grid-strided variant + one ~40-line bilinear
  gap-fill. Host-sim can render the level strip (1/16→full) as PNGs for the
  perceptual bench before any hardware — same discipline as the reveal's bench
  captures.
- **Stage 2 — importance order (HiPR-style, only if the bench wants it).** The
  terminator annulus (the band around `lns = 0` ± `MOON_TERM_SOFT`, already
  computed for the soft blend) at full resolution first, then the coarse levels
  for the rest. Cost: an extra full-resolution pass over a thin annulus (a few
  % of the disk) before the pyramid — the "most important pixels first"
  upgrade, and it kills the one perceptual weakness of Stage 1 (a soft 1/16
  terminator) if it shows up in the bench.

Gate impact: none of the stages change the final frame or `s_frame` semantics;
the image gate, the drain witness, and the heal matrix carry over. New Kconfig
keys (e.g. `UI_MOON_COARSE2FINE`, `KNOB_UI_COARSE2FINE`) follow the n-undefined
rule.

## Contrarian Views And Risks

- **The "no mixed old/new frames" product rule constrains the design — and is
  satisfied only by _whole-state_ discipline.** The spin-seam lesson was that a
  _spatial_ half-old/half-new composite (row 0–200 old, 200–400 new) reads as
  corruption. The proposed policy never creates one: every displayed state is
  either the whole old frame, the whole new-frame-at-level-k (exact + filled
  gaps — one image), or a whole-frame temporal blend. The moment an
  implementation starts _compositing_ level pixels onto the old frame
  row-by-row on screen, it is back in the spin-seam failure mode; the invariant
  text should name the level states as the reveal's successor states.
- **Multigrid does not apply, and borrowing its name would mislead.** Its
  coarse-to-fine is a solver acceleration (smooth-error transfer,
  restriction/prolongation, O(1) convergence for PDE systems)
  [MIT 18.086 notes](https://math.mit.edu/classes/18.086/2006/am63.pdf). There
  is no solve here — one evaluation per pixel, no iteration, no error to damp.
  The transferable artifact is only the grid-nesting structure. (A related
  trap: "compute coarser grids first _to speed up_ the fine computation" — with
  a pure per-pixel function there is nothing to speed up; the coarse levels
  earn their keep purely as _display_ states.)
- **Coarse levels can look _worse_ than black if the gap-fill is naive.**
  Nearest-neighbor fill of a 1/16 grid shows blocky 16-px maria; bilinear fill
  smears the terminator by up to ~16 px at level 0. Both are "a recognizable
  moon" (the LQIP bar), but the smear is the most likely "looks wrong"
  candidate. Mitigations: bilinear fill (not nearest), the optional Stage-2
  terminator-first pass, and a minimum-level floor (skip displaying levels
  whose gap-fill error exceeds a bench threshold — the csswizardry "sensible
  minimum quality" caution).
- **Perceptual "settling" vs corruption.** Progressive stochastic rendering
  lives with intermediate frames that _are wrong estimates_ (noise); the moon
  case is better (deterministic refinement) but the transition old→level-0 is a
  whole-image change. If it reads as a flicker, the 2–4 frame crossfade fixes
  it at the cost of a few extra full flushes (each ~one QSPI strip payload on
  the S3; trivially affordable inside the 5 s budget).
- **E-ink ghosting is the warning label for "old content under new."**
  Pervasive Displays recommends fast (whole-screen diff) update over windowed
  partial refresh partly for residual-image quality. The AMOLED analog — a
  crossfade — is bounded by construction (a few frames), but the knob's
  swap-task preemption tax (1.07 s measured for 36 band flushes) shows the
  display work is real; the pyramid's ~4–5 whole flushes should cost far less,
  but the knob's ~1 s extra bake time means the _final_ frame lands later with
  any reveal on — the trade (first pixels ~10 ms vs final ~1 s later) is the
  same one accepted for the strip reveal, at a much smaller cost.
- **The 8.2 s ephemeris and 15–20 s day-scan CPU hogs are separate startup
  latencies.** Coarse-to-fine only shortens the _display_ of the bake; the
  `new_moon_epoch` scan (memoized per synodic month) and the event day scan
  (backgrounded to a worker) sit in front of or beside the bake. If "slow
  startup" persists after Stage 1, the next lever is those, not the bake.
- **HiPR's own limit note applies**: its scheduling shines for _localized_
  edits; "large lighting changes … affect much of the image, leaving less work
  to prioritize" [80.lv]. Between adjacent hours the sun moves a few degrees —
  the terminator region is a large fraction of the disk at thin phases
  (crescents: most of the disk is dark-side shading, barely changed). The
  importance order (Stage 2) is most valuable near quarter phases, least at
  new/full — a known, bounded weakness, not a refutation.
- **Nothing in the lunar rendering prior art validates progressive _display_ of
  lunar DEM shading** [Lunasynth](https://github.com/nasa-jpl/lunasynth) — the
  perceptual acceptance of a "resolving moon" is an untested bet, mitigated by
  the host-sim level-strip bench before hardware.

## Open Questions

- Hard swap vs crossfade at the first level (needs the level-strip bench: does
  old→1/16 read as a pop, a flicker, or a natural "the moon updates"?).
- Level floor: is 1/16 worth displaying, or does the 1/8 grid (20 ms on the S3)
  already beat it perceptually for 8× the cost? (The cost argument for skipping
  1/16 is weak — it is free — the question is purely perceptual.)
- Gap-fill kernel: bilinear from the same grid (proposed) vs a 3×3 blur of the
  grid points (a touch more "photo-like" softness, matching the accepted
  product aesthetic "NASA adds a gaussian blur at the boundary".
- TF-hit fade skip: how close must a fetched hour frame be to the displayed one
  to skip the fade pair (a small per-pixel diff budget, or simply "always skip
  when a frame is displayed")?
- Interaction with the day-tick re-bake and the dusk: both already ride the
  row/strip APIs; do they gain a level-display path too, or is boot + the "now"
  leg the right scope for v1?
- P4 (650 = 2·5·13·10): only 1/2 divides cleanly — its pyramid is 325→650 (two
  levels, first state ~600 ms). Is a 2-level pyramid worth the shared code path
  on the P4, or should the P4 take Stage 0 only?

## Sources

Scraped and used (Firecrawl, 2026-11-18):

- [arXiv:2606.26612 — HiPR: Hierarchical Progressive Rendering for Immediate Feedback](https://arxiv.org/html/2606.26612v1)
  — primary paper: region-hierarchy scheduling, tile priority by
  light-transport influence, stale pixels kept while others catch up, final =
  unbiased result.
- [80.lv — HiPR interview (Padilla, Sep 2026)](https://80.lv/articles/how-hipr-makes-path-tracing-feel-more-responsive-by-rendering-what-matters-first)
  — mechanism in plain terms; limitations (large lighting changes, camera
  motion); "order in which information reaches the screen."
- [gamedev.net — HiPR news](https://gamedev.net/news/4122-hipr-hierarchical-progressive-rendering-for-immediate-feedback/)
  and
  [3dxdev — HiPR breakdown](https://3dxdev.com/hipr-repaints-only-the-pixels-your-edit-changed-not-the-whole-path-traced-frame/)
  — corroboration; three-stage algorithm summary.
- [UCSD — Progressive Photon Mapping (Hachisuka, Ogaki, Jensen), page + slides](http://graphics.ucsd.edu/~henrik/papers/progressive_photon_mapping/)
  ([PDF](https://pdfs.semanticscholar.org/2530/0ba97d03cc21ba6296616ff57ae747dee1ef.pdf))
  — multi-pass progressive global illumination; each pass visualizable;
  convergence with finite memory.
- [NVIDIA iRay — progressive rendering example](https://raytracing-docs.nvidia.com/iray/api_reference/iray/mi_neuray_example_progressive_rendering.html)
  — product-level contract: fast low-quality first frame, monotone improvement.
- [UAL — JPEG 2000 lecture notes](https://w3.ual.es/~vruiz/Docencia/Apuntes/Coding/Image/04-JPEG2000/index.html)
  — progression orders LRCP/RLCP/PCRL/CPRL; RLCP = resolution-first; 2:1
  subband decomposition.
- [Wikipedia — JPEG 2000](https://en.wikipedia.org/wiki/JPEG_2000) — coding
  passes, layers/progression, refinement types.
- [McMaster — JPEG2000: An Introduction Part I (PDF)](https://www.ece.mcmaster.ca/~shirani/multi08/jpeg2000part1.pdf)
  — decoder-side progressive display as the canonical use.
- [USC — Layered DCT still image compression (PDF)](https://mcl.usc.edu/wp-content/uploads/2014/01/1997-04-Layered-DCT-still-image-compression.pdf)
  and
  [Microsoft Research — Embedded DCT approach to progressive image compression](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/icip96_embdct.pdf)
  (listing only; PDF fetch bot-walled) — layered/progressive refinement
  predates J2K.
- [Wikipedia — Pyramid (image processing)](<https://en.wikipedia.org/wiki/Pyramid_(image_processing)>)
  — lowpass vs bandpass (Laplacian) pyramids; base + detail structure.
- [MIT 18.086 — Multigrid methods (Strang notes, PDF)](https://math.mit.edu/classes/18.086/2006/am63.pdf)
  and
  [tbetcke HPC notes — Multigrid](https://tbetcke.github.io/hpc_lecture_notes/multigrid.html),
  [Wikipedia — Multigrid method](https://en.wikipedia.org/wiki/Multigrid_method)
  — coarse-to-fine as solver convergence; why it does not transfer.
- [arXiv:2406.16302 — Residual path integrals for re-rendering](https://arxiv.org/html/2406.16302v1)
  — render only the difference between states; faster than from-scratch.
- [TU Wien — Hann, Incremental Updates of Path-Traced Scenes during Editing (2022)](https://www.cg.tuwien.ac.at/research/publications/2022/HANN_2022_IPT/)
  — region-priority incremental rendering in 3D editing viewports.
- [Mux — Blurry image placeholders on the web](https://www.mux.com/blog/blurry-image-placeholders-on-the-web)
  — LQIP/BLIP comparison; 16×16 WebP "sufficient detail"; technique trade-offs.
- [Cloudinary — LQIP explained](https://cloudinary.com/blog/low_quality_image_placeholders_lqip_explained)
  — placeholder mechanics, use cases.
- [csswizardry — The ultimate LQIP/LCP technique](https://csswizardry.com/2023/09/the-ultimate-lqip-lcp-technique/)
  — minimum-quality threshold over minimum size.
- [Pervasive Displays — E-Ink fast & partial refresh](https://www.pervasivedisplays.com/how-e-paper-works/fast-update-refresh/)
  — fast vs partial update; ghosting table; fast update recommended.
- [MakerGuides — Partial refresh of e-paper (ESP32)](https://www.makerguides.com/partial-refresh-e-paper-display-esp32/)
  — MCU-scale partial refresh: 0.3 s vs 2–4 s full; flicker avoidance.
- [Viwoods — E-Ink full vs partial refresh](https://viwoods-eu.com/blogs/paper-tablet/e-ink-refresh-full-vs-partial-refresh)
  — trade-off summary.
- [arXiv:2610.09098 — On-device SR for a SPAD array on an MCU](https://arxiv.org/abs/2610.09098)
  — INT8 SR on a Cortex-M33 (640 KB SRAM): upscale-on-MCU feasibility.
- [GitHub — NASA JPL Lunasynth](https://github.com/nasa-jpl/lunasynth) and
  [GitHub — 3D Lunar Terrain Generation](https://github.com/Abeey04/3D-Lunar-Terrain-Generation)
  — standard DEM+albedo+sun lunar rendering; no progressive-display prior art.
- [Inferensys — Coarse-to-fine sampling](https://inferensys.com/glossary/neural-radiance-fields-and-spatial-computing/real-time-neural-rendering/coarse-to-fine-sampling)
  — coarse pass as importance proposal for the fine pass (the scheduling
  reading of coarse-to-fine).
- [DeepWiki — progressive rendering & accumulation (WebGPU raytracer)](https://deepwiki.com/gnikoloff/webgpu-raytracer/5.4-progressive-rendering-and-accumulation)
  — accumulation-buffer practice for intermediate-frame display.

Not fetched (bot-walled, corroborated by the above): ACM — _Antialiased ray
tracing by adaptive progressive refinement_ (10.1145/74334.74362); ResearchGate
mirror of the same; Microsoft Research embedded-DCT PDF.

Local grounding (not web sources): `components/moon/core/moon_bake.c`
(per-pixel model, gradient precompute, row/chunk APIs),
`docs/implementation/bake-reveal.rst` (current reveal states, pacing rules,
measured bench numbers, exit-path matrix), `components/ui/moon_ui.c` (TF hour
cache, replay, reveal flags).

## Rerun Inputs

workflow: firecrawl-deep-research topic: progressive / iterative refinement of
an image computation result (coarse-to-fine multi-resolution computation,
progressive rendering, JPEG2000-style resolution refinement, last-known-good
cache while refining) applied to per-pixel lunar DEM+albedo shading on a
dual-core ESP32 with an atomic-flush display invariant depth: exhaustive
output: markdown (docs/research/progressive-moon-startup.md) angles:
progressive rendering (PPM/HiPR) · progressive coding (J2K RLCP, layered DCT) ·
coarse-to-fine math (multigrid, Laplacian pyramid) · incremental re-rendering
(residual integrals, region priority) · display UX (LQIP/BLIP, e-ink partial
refresh) · embedded MCU prior art (on-device SR) · lunar rendering prior art
(Lunasynth et al.) · contrarian/limits (flicker, ghosting, minimum-quality bar,
solver-vs-display distinction) notes: search via `firecrawl search --json` (no
--no-highlights: 400); scrape via
`firecrawl scrape -f markdown --only-main-content`; ResearchGate and some
publisher PDFs are bot-walled — route around via mirror/lecture copies.
