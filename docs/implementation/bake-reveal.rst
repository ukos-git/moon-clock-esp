Bake reveal — displaying the computation while it runs
=======================================================

The user's brief (2026-11-14): *"think of iterative display during the
calculation of the current moon phase. The current state to display a
black background during the calculation is not a good user experience,
especially if calculation takes very long. Integrate the display of the
current calculation with the calculation performance itself."*

The answer is not a spinner or a progress bar — it is making the bake
**the metronome of its own display**: the unbaked area is black, each
completed strip of the bake becomes one atomic displayed state, so the
moon draws itself north to south at the actual bake rate. When the
bake is fast the reveal is fast; when the panel, the radio or the card
starves the pipeline, the reveal stalls with it. The displayed pixels
and the computation share one clock because they are the same
computation.

This page carries the analysis (why the black window existed and why
it was hard to remove), the design on both boards, the exit-path
matrix, the cost analysis, and the measured bench numbers from the
2026-11-14 dual-board run that validated it.

The black windows this removes
------------------------------

Two flows left the panel dead black while the current phase computed:

1. **The hourly refresh** (``UI_MOON_HOURLY_ENABLE``, default y): the
   panel faded to black on the 0x51 ramp, the core-1 producer fetched
   or baked the hour-centre frame (**~1.3 s S3 / ~2.4 s P4** — the
   Kconfig help itself admitted the fade "only lengthens the dark
   window"), the finished frame landed as one atomic flush *while
   still black*, then the brightness ramped back. Dark dead-time ≈
   fade-out + full bake + fade-in.
2. **The boot replay's final "now" leg** (``UI_MOON_BOOT_REPLAY``):
   after playing the stored hour frames, the panel froze on the newest
   stored frame (or stayed black with no cache — the brief's own
   "stays black until the current moon phase is calculated") for the
   whole current-phase bake. The **knob** had the worst case: no SD
   socket, so *every* boot is the black-until-first-frame window
   (measured 991 ms on the current binary).

The cure already existed as a house pattern: the **orbit pipeline**
bakes row-by-row with ``moon_bake_rows()`` (its header comment says the
row API exists "so it can feed the progressive strip display"),
publishes a rows-completed watermark, and the 50 ms tick displays
newly completed strips. ``MOON_ORBIT_BATCH = MOON_NSTRIPS`` later
collapsed that consumer back to one atomic flush (one visible state per
batch, the QSPI lesson of 2026-11), so the machinery existed but no
longer displayed progressively. The bake reveal generalizes that shape
to "reveal over black" with its own flags — the orbit leg stays as it
is.

Why "black until done" was the rule — the invariant history
.............................................................

The hard invariant says every moon-pipeline frame hits the panel as
ONE atomic whole-panel ``lv_refr_now``; the only sanctioned exception
was the hardware brightness ramp (which writes no pixels). A
strip-by-strip reveal of a *computing* frame looks like a violation —
it is exactly what the 2026-11 spin-era lessons warned against (the
spin's per-strip QSPI re-latch produced "regular disturbing lines";
the P4's 3-FB flip showed stale poses; a half-drawn frame on the
AMOLED burns in). Three decisions pin the reveal down as compliant
rather than a regression:

* **Atomicity-per-state.** The rule forbids a frame torn MID-ROW by
  the async drain, not a designed sequence of consistent states. Each
  displayed state — the black canvas, each completed strip band, the
  healed old frame — is ONE atomic whole-panel-band ``lv_refr_now``
  under ``s_rgb_mtx`` with its drain witness, exactly like the hourly
  atomic show. Between states the panel shows a complete,
  deliberately-chosen image; it never shows the async drain's partial
  work. The invariant text in :doc:`architecture` names the reveal as
  the second sanctioned flow (user decision 2026-11-14).
* **No mixed old/new frames.** The user's own call when offered the
  choice: unbaked rows are **black**, not the previous frame
  (the spin-seam lesson: a half-old/half-new composite read as
  corruption). So the reveal is over a cleared canvas, and the old
  frame is never partially overwritten on screen — it is only ever
  restored whole (the heal).
* **Not anti-burn-in machinery.** The AMOLED burn-in rule (user
  2026-11-14: a faint burned-in moon is accepted, a *feature*)
  forbids reintroducing spin/pixel-shift-style machinery without a
  fresh decision. The reveal is the opposite: it *shortens* static
  exposure to half-frames (each band is on screen only until the bake
  completes or the final atomic show replaces the square), and it is
  a fresh user decision — this feature.

Design — the shared clock (S3 + P4, ``components/ui/moon_ui.c``)
................................................................

.. code-block:: c
   :caption: components/ui/moon_ui.c (the reveal's whole cross-core contract)

   enum { REVEAL_NONE = 0, REVEAL_HOURLY, REVEAL_REPLAY_FINAL };
   static volatile int s_reveal_owner;      /* whose bake is being revealed */
   static volatile uint32_t s_reveal_pub;   /* rows fully baked into the bounce */
   static volatile uint32_t s_reveal_shown; /* rows the tick has displayed */
   static volatile bool s_reveal_clear;     /* the black canvas is not shown yet */
   static volatile bool s_reveal_dirty;     /* s_rgb holds reveal pixels: an abort must heal */
   static volatile bool s_reveal_stop;      /* finish/pause asked the producer to stop */

* **Producers** — ``moon_hourly_task``'s cold-bake leg and
  ``moon_boot_replay_task``'s final "now" leg — replace the single
  ``moon_bake()`` with ``moon_reveal_bake()``: the orbit's row loop,
  ``moon_bake_rows`` per ``MOON_REVEAL_ROWS``-row chunk into the same
  ``s_hourly_gray`` PSRAM bounce, the rows watermark bumped after every
  chunk **under ``s_rgb_mtx``** (the orbit rule: the tick's row reads
  and the watermark never race), ``vTaskDelay(1)`` per chunk for the
  IDLE1/TWDT. Geometry, classify, event LUT, TF write-through, the
  publish memcpy + LUT and the commit-on-show bookkeeping are
  unchanged — the finished bounce is exactly what ``moon_bake`` would
  have produced, which is why the image gate stays byte-identical
  (``components/moon/core`` is untouched).
* **The tick** (``moon_bake_reveal_service``, right after
  ``moon_hourly_service`` in ``moon_tick_cb``) converts one state per
  50 ms tick: first the black canvas (``memset`` the square's
  ``s_rgb`` + full invalidate), then one band per tick — rows
  ``[shown, pub)`` gray→RGB565 through ``moon_gray565`` into ``s_rgb``
  and ``lv_obj_invalidate_area`` of exactly that band on every moon
  image — each landed as ONE ``lv_refr_now`` under ``s_rgb_mtx`` with
  ``moon_panel_drain_verify``. A band flush at the chosen heights is
  ≤ one QSPI strip's payload (S3: 400×20×2 = 16 KB < the 23,300 B
  strip cap), so one band is one strip transaction.
* **Band height** — ``MOON_REVEAL_ROWS``: 20 rows on the S3
  (400 px / 20 = 20 states), 26 on the P4 (650 px / 26 = 25 states);
  a ``#error`` guard pins the divisibility. There is no artificial
  FPS cap: at the ~2 s S3 bake the producer lands ~10 rows/50 ms tick,
  so the 50 ms tick and the bake rate co-pace (~20 visible states);
  on a bursty producer the tick simply shows larger catch-up bands
  (one atomic state per tick regardless).
* **The producer NEVER blocks on the display.** The chunk loop
  free-runs; a single bounded wait (1500 ms) at the very end lets the
  tick land the tail before the normal publish + atomic show (a parked
  or gated tick never wedges the producer — the done legs show
  atomically regardless, and the heal paths cover every aborted
  state). An early per-chunk wait variant was rejected on the bench:
  during a traffic hold the gated tick starved it chunk after chunk
  and serialized the bake behind the consumer (exactly what
  integrating display and computation must NOT do).
* **``s_frame`` (the 8-bit truth) is never touched during a
  reveal** — only the bounce and ``s_rgb`` diverge, and
  ``s_reveal_dirty`` flags that. Any abort heals by re-converting
  ``s_frame``→``s_rgb`` under ``s_rgb_mtx`` + ONE atomic flush
  (``moon_reveal_restore``, LVGL-task callers only): the old frame
  returns whole over the half-drawn square. A late producer after a
  blown budget still lands its own atomic show (no second fade-in).
* **TF fetch hits never arm the reveal** — the fetch path keeps
  today's atomic show (the black window on a hit is ~one bake-less
  second: fade-out + ~200 ms + fade-in).

The hourly fade choreography (user decision: fade out, then reveal)
...................................................................

The hourly cycle keeps its fade-OUT; the fade-BACK no longer waits for
the whole bake. The owed fade-in (``s_hourly_faded``, the #34 "owed
exactly once" rule) is paid at the **first completed strip** — the
reveal leg calls ``fade_ramp(0, 100)`` between flushes, so the dark
window is the fade itself, not fade + bake. Deliberately, ``s_fade_busy``
is NOT a gate in the reveal service: during the hourly the reveal leg
IS the 0x51 writer that pays the fade-in (and ``fade_ramp`` blocks the
tick while it runs, preserving the one-writer rule). The other exit
paths keep their payer — every path that reaches
``moon_hourly_finish`` with a half-revealed square heals FIRST (via
``s_reveal_stop`` + ``moon_reveal_restore``), and only then ramps, so
the brightness never climbs over a half-moon or a black canvas. The
heal-before-ramp ordering is the #34 never-strand-black rule with the
new state in the picture.

Exit-path matrix (all measured on the 2026-11-14 bench, S3)
.............................................................

.. list-table::
   :header-rows: 1
   :widths: 26 24 26 24

   * - Exit
     - Reveal state
     - Panel
     - Fade-in payer
   * - bake completes (hourly or replay final)
     - all bands, then owner cleared; publish + tick's atomic show
     - completed frame
     - first strip (tick leg); finish finds the flag false
   * - TF fetch hit
     - never armed (no reveal, no black canvas)
     - fetched frame, one atomic show
     - ``moon_hourly_finish`` (no strips exist)
   * - producer fails / no albedo
     - whatever landed; ``finish(0)`` heals via stop + restore
     - old frame (healed whole)
     - ``moon_hourly_finish`` after the heal
   * - 5 s budget blown
     - ``finish(0)`` heals; the late producer finishes its bands, clears owner, publishes, lands its own atomic show (no second fade)
     - old frame immediately; new frame when late producer lands
     - ``moon_hourly_finish`` after the heal
   * - sleep hand-off / alarm screen (``moon_ui_pause(true)``)
     - replay branch: abort + stop + heal BEFORE the tick pauses (the sleep screens must not inherit the black canvas)
     - last complete frame, then the sleep screens
     - ``moon_hourly_finish(0)`` from the hourly drop / replay heal branch
   * - screen left mid-reveal
     - gates park the service; the tick's backstop heals on return (owner already NONE)
     - old frame on return
     - whichever finish ran first; backstop covers none
   * - traffic hold / fade pending / sweep (gated)
     - service returns; watermark keeps advancing; tick drains larger catch-up bands when the gate opens
     - last displayed state until the gate opens
     - first strip (deferred) or finish

The witness labels are themselves the per-owner done signature on the
bench: the hourly leg's own publish rewrites all of ``s_rgb`` and clears
``s_reveal_dirty``, so its atomic show is the last flush and no heal
follows; the replay-final leg leaves the flag set, so the tick's
backstop heal is the final re-convergence — its done line is always
followed (within a tick or two) by exactly one
``drain verify after bake reveal restore`` witness. That line on a
healthy boot means *normal completion*, not an abort.

Design — the knob sibling (``knob/``)
.....................................

The knob has no bounce/tick split — its producer fills ``s_buf[back]``
whole and the swap task copies it out — so the reveal moves INSIDE the
producer: ``knob_moon_bake_display_reveal()`` (the same ``bake_frame``
as the plain bake, phase order only) clears the square to black, then
per row bakes **and** LUT-converts it into ``out`` before advancing the
rows watermark with the ``s_pub`` fence discipline
(``__atomic_thread_fence(__ATOMIC_SEQ_CST)`` + plain volatile store).
The swap task (prio 7, 15 ms poll) edge-detects the reveal, and for
every watermark advance copies the row range into the visible buffer
and lands it as ONE atomic band ``lv_refr_now`` under the reentrant
``s_lvgl_mux`` — unhiding the disk at the first completed rows. It
applies to the **boot first frame only** (the knob's every-boot black
window): day-tick re-bakes and the demo tour keep the
old-frame-then-atomic posture (not a black window — no reveal). A
reveal that dies before publishing re-hides the disk: the honest
"moon unavailable" posture the plain bake already had.

Configuration
.............

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Key
     - Default
     - Effect when off
   * - ``UI_MOON_BAKE_REVEAL`` (S3/P4)
     - y
     - both producers call ``moon_bake``; the hourly is today's
       full-black bake (the feature is built under ``#if``; flags and
       hang-context lines stay compiled so the witness stays whole)
   * - ``KNOB_UI_BAKE_REVEAL``
     - y
     - boot bakes through ``knob_moon_bake_display``; black until the
       first frame lands (the pre-reveal posture)

Both follow the hourly/burn-in rules above; the clock's key depends on
``UI_MOON_HOURLY_ENABLE`` (the reveal rides the hourly choreography and
the replay's final leg — no hourly, no revealed bake).

Cost analysis — display integrated with computation
...................................................

The reveal ADDS display work during what used to be dead time, and the
numbers say what that costs:

* **QSPI/LVGL**: ~20 band flushes per revealed bake (S3), each ≤ one
  25-row strip's payload plus one ``lv_refr_now`` bookkeeping pass.
  This is the cost the pre-reveal posture paid ALL AT ONCE in the
  final atomic show (~19 full-width strips) — the total bytes to the
  panel are comparable (the bands sum to the square); what changes is
  that they are spread over the bake instead of piled at the end. The
  final atomic show is kept (contract + witness), visually a no-op
  over the completed reveal.
* **Drain witness**: the band cadence re-triggers the lost-GPSPI-done
  family measurably more often (co5300's per-draw CASET/RASET/RAMWR
  polling is the trigger, and the bands multiply the flush windows).
  The 2026-11-14 bench observed ~5 in-place heals during a boot-reveal
  leg (~12 % of band states), ALL healed by the designed no-op
  ``tx_param(-1)`` bookkeeping heal — 0 site restarts, 0
  ``PANEL IO STUCK``, the same healthy recovery capture 11 established.
* **The knob pays the visible-for-computation tax the clearest**: the
  same binary family measured **991 ms** bake with the reveal off
  vs **~2.05 s** with it on — the swap task's per-band
  ``lv_refr_now`` (prio 7) preempts the producer (prio 5) and the
  SPI panel flushes interleave with the bake's PSRAM traffic. End to
  end the revealed boot frame lands ~1 s LATER than the hidden one,
  but the first pixels appear ~50 ms in instead of after ~1 s of
  black — the user sees the moon drawing itself for those last
  seconds. That is the integration: the display consumes real bake
  throughput, and the bake visibly paces the display.
* **No added PSRAM**: no new buffers — the reveal writes the existing
  bounce (clock) / back buffer (knob) through the existing
  ``s_rgb``/``s_buf`` paths.

Measured bench numbers (2026-11-14, ``CONFIG_TEST_HOURLY_FORCE_S`` runs + cold/warm boot captures)
...................................................................................................

S3 AMOLED (466 panel, 400 disk, 20-row bands) — card in slot, fresh
per-version cache folder (a new flash caches from start by design):

.. list-table::
   :header-rows: 1
   :widths: 40 28 32

   * - Flow
     - Measured
     - Notes
   * - Boot replay final leg, revealed bake
     - **2009–2615 ms** (400/400 rows, shown 400; three boots)
     - from ~150 ms after bake start the bands flow (first band =
       20 rows ≈ 130 ms bake + one 50 ms tick); 20 band states; the
       empty-folder boot (0 stored frames, fresh per-version folder)
       reveals too — the leg is then the panel's first image at all
   * - Hourly cold bake, revealed
     - **2687 ms** bake internals (owner 1, shown 400), whole cycle
       **3083 ms** — inside the 5 s budget
     - fresh per-version folder (a new flash), first-strip fade payment
       observed: ``bake reveal: first strip up, panel brightness 0
       -> 100 (240 ms)``; the pre-memoization take measured 1975 ms
       internals with the bake itself unchanged
   * - Hourly warm TF hit
     - **185–235 ms** frame land, fade-back ramp +240 ms
     - no reveal armed (correct); whole black window ≈ the two ramps
   * - Hourly cycles (forced bench cadence)
     - 60 s spacing (probe rides the minute day-tick)
     - ``PANEL IO STUCK`` 0×, restores 0× after the fix below
   * - Drain witness during reveal legs
     - ~5 in-place heals per revealed boot leg
     - 0 restarts, 0 stuck (designed heal, see cost analysis)

Knob (360 disk, row-interleaved, boot-only reveal):

.. list-table::
   :header-rows: 1
   :widths: 40 28 32

   * - Flow
     - Measured
     - Notes
   * - Boot bake, reveal off (same binary family)
     - **991 ms**
     - the honest baseline (also corrects the stale K7b-era 790 ms)
   * - Boot bake, revealed
     - **2028–2059 ms** (360/360 rows)
     - first rows visible ~50 ms in; the swap's band flushes cost the
       bake ~1.07 s (see cost analysis)

P4 (720 panel, 650 disk, 26-row bands): **bench deferred** — the 4C
was not on the bench for this pass. The bake baseline stands at ~2.4 s
(the 2048-albedo floor); the reveal's shape and gates are the shared
code, so its numbers are expected to mirror the S3's scaled by the
bake rate. Flag: hourly/reveal bench for the P4 is an open item with
the rest of #44.

The bench's regression catch — the 8.2 s per-cycle new-moon scan
................................................................

The forced-hourly bench existed before this feature but had never been
run (open item #44) — and it immediately caught a real regression from
the boot-replay era (1c90a64): the hourly producer's cache-policy leg
re-ran ``new_moon_epoch`` — the ~155-eval ephemeris scan, **8.2 s on
the S3** — on EVERY cycle, so even a pure TF fetch hit took a constant
8210 ms, blew the 5 s producer budget every time, and showed the
restore-and-late-landing path instead of the designed sub-second
refresh. Fix (same commit): ``new_moon_epoch_cached`` memoizes the
floor for the whole synodic month (the floor only moves at a crossing;
a 30-day reuse bound is safe, and the policy is non-authoritative
anyway — ``tfcache_evict`` enforces the cap ephemeris-free). Post-fix
the hit cycles land at 185–235 ms and stay inside budget; the boot
replay's floor leg shares the same cache, so the first producer pays
the scan once per synodic month.

Cross-references
................

* :doc:`animations` — the boot replay and hourly sections now describe
  the revealed legs; the fade ramp page owns the ramp mechanics.
* :doc:`architecture` — the invariant's two sanctioned flows.
* :doc:`knob-display` — the knob's boot posture with the reveal on.
* :doc:`../verification/performance` — the numbers table.
* ``components/ui/moon_ui.c`` (the reveal section) and
  ``knob/components/knob_moon/knob_moon.c`` (``bake_frame``) are the
  authoritative sources.
