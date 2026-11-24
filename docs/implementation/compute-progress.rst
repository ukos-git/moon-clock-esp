Compute progress — the phase disc and the arc
=============================================

The user's brief (2026-11-24): *"There is no point currently in giving out
iteratively the result of the current calculation. It is simply not
performant enough to yield calculation-steps. Instead, show the calculation
progress in reasonable update steps using the seconds display arc that is
already showing on the display after startup — use it as a calculation
progress bar. As a dummy image (instead of the black image), use a plain
sphere projection of the moon with its current phase … easiest and quickest
to calculate … this stays as a dummy during calculation of the more advanced
image."* — with the mid-turn refinement: *"if a calculated image is present
you might as well stay on that; only if there would be a dark background,
show the quick-calculated phase image."*

It replaced the strip-by-strip **bake reveal** (user decision 2026-11-14,
this page's predecessor ``bake-reveal.rst`` — its 2026-11-14/15 bench
numbers below remain the accepted bake-cost measurements). The reveal's
entire display side is gone: the band state machine, the one-strip-per-tick
pacing rule, the catch-up bands, the heal path, the ``s_hourly_faded``
fade choreography — and with it four Kconfig keys
(``UI_MOON_BAKE_REVEAL``, ``KNOB_UI_BAKE_REVEAL``,
``UI_MOON_HOURLY_FADE_MS``, ``TEST_BAKE_REVEAL_TRACE``; all three trees,
2026-11-24). What remains of the reveal is its **producer** — the
strip loop with the rows-completed watermark — repurposed: the watermark
now feeds a progress bar instead of a display. And the display side is two
small things: a **quick phase disc** in the dark windows, and the home/wifi
**seconds arc** doubling as the progress bar.

The invariant is unchanged — in fact it got simpler: there is now exactly
ONE sanctioned exception to the one-atomic-whole-panel rule (the
screen-switch 0x51 ramp), where the reveal was the second. Every image that
lands on the panel — the disc, the old frame, the finished bake — is ONE
atomic ``lv_refr_now`` under ``s_rgb_mtx``; the only thing that moves
continuously during a bake is the arc, and the arc is an ordinary LVGL
object, not a panel-write.

The three pieces
----------------

The flat phase disc (core: ``moon_bake_phase_dummy``)
........................................................

``components/moon/core/moon_bake.c`` (declared in
``components/moon/include/moon.h``) carries the dummy — shared verbatim
with the host simulator and the knob like the rest of the core:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c — the whole function is one row loop

   void moon_bake_phase_dummy(const moon_geom_t *g, uint8_t *frame, int size,
                              float radius, float cx, float cy) {
       /* per pixel: u = (x-cx)/r, v = (cy-y)/r, r2 = u*u + v*v
        * r2 > 1          -> 0
        * w = sqrt(1 - r2);  r = u*right + v*up + w*E
        * d = r·S;  lit side = MOON_TONE_LUT[round(255*clip(d,0,1))]
        * dark side = 0  */
   }

The exact sphere geometry of the real bake's ``bake_px`` — the same
``right``/``up`` basis, the same ``E`` (Moon's unit normal), the same
``S`` (Sun direction) — with everything expensive removed: no albedo tap,
no relief gradient, no event tint, no PSRAM texture loads. The cost is one
``sqrtf`` and two dot products per in-disk pixel: **tens of ms on the S3**
vs ~1.3 s for the full bake, ~1 s on the knob. A full moon is a uniform
bright disc with a Lambert limb falloff; a crescent is the lit wedge the
phase dictates; the dark side is plain 0 (no earthshine, no backlit
surface — the brief's "easiest and quickest", and the same
``dark_zero=1`` posture the display bakes pass). The disc is a *dummy*:
it shows the current phase at the correct size and position with uniform
albedo, so the panel reads as "the moon, computing" instead of black.

The watermark and the arc (the seam)
....................................

The producer keeps the reveal's strip loop (renamed
``moon_bake_progress``, ``moon_ui.c:1402``): ``moon_bake_rows`` per
``MOON_COMPUTE_ROWS``-row chunk (10 rows S3 → **40 watermark steps**, 13
rows P4 → **50**; a ``#error`` pins the divisibility, ``moon_ui.c:1324``)
into the same PSRAM bounce, the rows-completed watermark
(``s_compute_pub``) bumped under ``s_rgb_mtx`` after every chunk
(``moon_ui.c:1420``), ``vTaskDelay(1)`` per strip for the IDLE1/TWDT —
and the **owner** (``s_compute_owner``: ``COMPUTE_HOURLY`` /
``COMPUTE_REPLAY_FINAL``) cleared LAST under the same mutex
(``moon_ui.c:1429``). Nothing display-side converts the watermark anymore;
the screens read it:

.. code-block:: c
   :caption: components/ui/moon_ui.h — the seam (the knob's seam answers the same pair; the uisim stubs it)

   bool    moon_ui_compute_active(void);   /* a compute bake is in flight */
   int32_t moon_ui_compute_progress(void); /* 0..100 (pub*100/MOON_PX) */

The home and wifi screens' tick (``arc_seconds_tick``,
``components/ui/screens.c``) drives the existing seconds arc
(``objects.arc_clock_seconds_modem`` / ``_wifi``, range 0–60) from it:
while a compute is active the arc value is ``progress * 60 / 100`` —
the watermark advances per strip, so the arc sweeps in the same
reasonable steps the watermark produces (40/50 over the S3/P4 final
bake); the moment the compute completes the very next tick restores the
real seconds. The update is a plain LVGL invalidate (no whole-panel
flush, no drain witness) — the arc rides normal LVGL refresh, which is
exactly why it can move per strip while the frame rule stays untouched.
The readers owe no lock: the watermark only moves up within one bake,
and the owner flips back to ``COMPUTE_NONE`` under ``s_rgb_mtx`` after
the last strip, so a tick sees either (active, rising) or (inactive,
ignored) — never a torn pair.

The old-frame-then-atomic hourly (the fade retires)
....................................................

The once-per-hour refresh no longer owns ANY 0x51 traffic
(``UI_MOON_HOURLY_FADE_MS`` and ``s_hourly_faded`` are gone): the probe's
gates are unchanged (``moon_hourly_probe``), but the tick's start leg
simply spawns the producer — **the old frame stays on the panel the whole
bake** (the brief's "if a calculated image is present, stay on it"), and
the finished frame lands as ONE atomic whole-panel flush in the BRIGHT
panel (``moon_hourly_finish``'s done leg, ``moon_ui.c:1789`` onward).
On a TF fetch hit the hit frame likewise lands atomically over the old
frame — no fade, no black. The arc sweeps during the cold bake; on a hit
there is no compute, so the arc shows real seconds. Every exit path is
now trivial because the old frame is always a consistent whole-panel
state: a blown deadline or a failed producer leaves the old frame up
(nothing to restore); a sleep entry mid-bake sets ``s_compute_stop``
(``moon_ui_pause``, ``moon_ui.c:2975``) and the producer deletes itself
*before* the publish (the partial bounce is never shown — the watermark's
bounce-only rule), the wake re-bakes on its next cycle; the replay's
final leg observes ``s_replay_abort`` the same way. There is no heal path
left: the partial rows of an in-flight bake are NEVER displayed.

Where the disc shows (and where it does not)
--------------------------------------------

The disc appears ONLY where the panel would otherwise be dark — i.e.
there is no committed frame to stay on:

.. list-table::
   :header-rows: 1
   :widths: 26 34 40

   * - Window
     - On the panel while it computes
     - Notes
   * - Clock boot, first frame (replay or sync path)
     - the quick phase disc (``moon_boot_disc``, ``moon_ui.c:2823``;
       ``moon_ui.c:2860`` replay path, ``moon_ui.c:2893`` sync path)
     - baked into ``s_frame`` under ``s_rgb_mtx`` + ``moon_convert_to_rgb``,
       shown by the ordinary tick as one atomic flush; moves no
       ``s_baked_day``/``s_baked_hour`` bookkeeping (set from the real
       epoch); the first replay frame or the finished bake replaces it
       atomically; the arc sweeps the final bake
   * - Clock boot, no time
     - stays black — no epoch, no phase, no disc
     - exactly as before; the day-tick re-bakes once the clock syncs
   * - Hourly cold bake / replay final bake
     - the OLD frame (or the newest stored replay frame) stays up; the
       disc does NOT appear
     - computed image present → stay on it; the arc sweeps the watermark
   * - Knob boot, saved frame on glass (``KNOB_UI_LAST_FRAME``)
     - the saved frame (≤ 2 days old, crc intact) stays up
     - the fresh bake swaps over it old-frame-then-atomic; the arc sweeps
   * - Knob boot, NO saved frame (first boot after a fresh flash)
     - the quick phase disc (``disc_publish``, ``knob_ui.c``)
     - published through the ordinary ``s_pub``/swap machinery (the swap
       unhides the disk atomically), ``wait_ack()`` before the real bake
       reuses the other slot; the arc sweeps the per-row watermark; the
       disc is **never persisted** — ``/spiffs/moon_frame.bin`` stays the
       last REAL bake

The watermark contract
----------------------

One rule carries the whole design: **the producer writes the PSRAM bounce
strip-wise; it never writes a display buffer strip-wise.** Everything the
panel can show (the disc, the old frame, the saved frame) is a complete
frame that lands through the existing atomic publish/show paths; the only
strip-wise artifact is the watermark, a counter the screens read for the
arc. Consequences:

* No half-old/half-new composite is possible — there is nothing to
  composite (the reveal's black-canvas premise is gone with it).
* No heal path is possible — an abort (sleep entry, replay abort, blown
  budget) leaves a complete frame on the panel by construction; the
  producer simply does not publish.
* ``s_frame`` (the 8-bit truth) is written whole, exactly as before; the
  image gate's byte-identity proof is untouched — the finished bounce is
  exactly what ``moon_bake`` would have produced (same ``moon_bake_rows``
  calls, same ``dark_zero=1, dk_sup=0.0f``), and the disc is an additive
  function the gate's frames never call.

Cost
----

* **The disc**: one row loop with no texture loads — tens of ms (S3), a
  fraction of one second (knob). It is the *only* new computation in the
  pipeline; it runs on the boot task (clocks) or the producer (knob) and
  is replaced atomically by the real frame.
* **The arc**: one ``lv_arc_set_value`` per tick while a compute is
  active — an ordinary LVGL invalidate, the same cost the seconds tick
  already pays. No 0x51 traffic, no extra flushes, no drain-witness
  churn: the 2026-11-15 lesson (every probe that follows a multi-strip
  flush loses its own completion) is moot for this design, because the
  bake itself is still ONE atomic whole-panel show at the end — the
  ~53 ms witness per *completed* flush, not per strip.
* **The hourly cycle loses both fade legs** (2 × 240 ms of 0x51
  traffic); the dark window on the hourly is now *zero* — the old frame
  is up the whole cycle.
* **The knob's band-flush tax is gone**: the 2026-11-14 bench measured
  the revealed boot bake at 2028–2059 ms vs the 991 ms hidden baseline —
  the swap task's per-band ``lv_refr_now`` (prio 7) preempting the
  producer (prio 5). With the band display deleted, the knob's boot bake
  is back at the ~991 ms plain baseline plus the disc's tens of ms.
* **No added memory**: the disc bakes into the existing ``s_frame``
  (clocks) / back buffer (knob); the watermark reuses the reveal's
  flags (three of them: owner, pub, stop).

Configuration
-------------

No new keys. The behavior is unconditional family-wide; the four
retired keys were deleted from ``components/ui/Kconfig`` and
``knob/components/knob_ui/Kconfig`` (2026-11-24), and the live pins
(``CONFIG_UI_MOON_BAKE_REVEAL``, ``CONFIG_KNOB_UI_BAKE_REVEAL``,
``CONFIG_UI_MOON_HOURLY_FADE_MS``, ``CONFIG_TEST_BAKE_REVEAL_TRACE``)
sedded out of ``sdkconfig`` / ``sdkconfig.p4`` / ``knob/sdkconfig`` per
the standing rule. The "off" postures (black windows / strip reveal) are
both retired — there is no knob to fall back to, by design.

Measured history (the 2026-11-14/15 reveal bench)
-------------------------------------------------

The predecessor page's bench numbers remain the accepted **bake-cost**
measurements of this pipeline's producer legs — the strip loop with the
watermark is the same producer (its internals are display-independent on
the clocks; the knob's numbers include the now-retired band-flush tax and
are re-measured in :doc:`on-device verification <../verification/device-verify>`
on this change's flash):

.. list-table::
   :header-rows: 1
   :widths: 40 28 32

   * - Flow (2026-11-14/15 bench, reveal era)
     - Measured
     - Today
   * - S3 boot final bake, strip loop
     - **2009–2615 ms** (400/400 rows), 40 one-strip watermark states
       at ~80 ms cadence (2026-11-15 re-measure)
     - same producer; the watermark drives the arc, not a band display;
       the disc is up from boot (~tens of ms) instead of black
   * - S3 hourly cold bake
     - **2687 ms** internals, whole cycle **3083 ms** (inside the 5 s
       budget)
     - same internals; the cycle now owes no fade legs (no 0x51
       traffic), so the whole cycle is the bake + one atomic show
   * - S3 hourly warm TF hit
     - **185–235 ms** frame land (+ the two retired 240 ms fade legs)
     - hit lands atomically over the old frame in the bright panel;
       no fade
   * - P4 boot final bake
     - **2662 ms** (650/650 rows, 50 one-strip states)
     - same producer, 50 watermark steps
   * - Knob boot bake
     - 991 ms hidden baseline / **2028–2059 ms** revealed (the band tax)
     - back to the ~991 ms baseline + the disc (tens of ms); the arc
       sweeps the per-row watermark over 360 rows

The P4's 4C bench of THIS change (hourly no-fade + arc) remains an open
item with the rest of the bench list — the 4C was off the bench for the
2026-11-14 pass as well.

Cross-references
................

* :doc:`architecture` — the invariant, now with ONE sanctioned exception.
* :doc:`animations` — the boot replay and hourly sections (disc + arc,
  no fade).
* :doc:`knob-display` — the knob's boot posture (disc publish + the
  saved-frame path) and its seam.
* :doc:`display` — the drain witness (the multi-strip-flush lesson that
  shaped this design's no-band-display rule).
* :doc:`../verification/performance` — the ladders (the pacing figure's
  reveal-tick row is now the watermark's tick; the hourly ladder owes no
  fade legs).
* ``components/ui/moon_ui.c`` (the compute section at :1286, the seam at
  :1443, ``moon_boot_disc`` at :2823), ``components/ui/screens.c``
  (``arc_seconds_tick``), ``components/moon/core/moon_bake.c``
  (``moon_bake_phase_dummy``) and the knob's ``knob_ui.c`` /
  ``knob_moon.c`` are the authoritative sources.
