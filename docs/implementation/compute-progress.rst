Compute progress: the phase disc and the arc
============================================

.. brief:
   Reader: a firmware developer working on the moon pipeline or the home screen.
   They already know: C, FreeRTOS tasks, LVGL objects, the atomic-frame rule.
   After this page they can: say what the panel shows while a bake runs, and change the progress seam without breaking the frame rule.
   Page type: explanation.

While the clock computes a new moon frame (a *bake*, about 1.3 s on the S3), the panel never shows a half-finished image.
It keeps the last complete frame on the panel, or a cheap flat phase disc when there is none, and the seconds arc on the home and Wi-Fi screens turns into a progress bar.
When the bake finishes, the new frame lands in one atomic whole-panel flush.
This page explains the three pieces that make this work, where the disc appears, and what it costs.

Every image that reaches the panel (the disc, the old frame, the finished bake) is one atomic ``lv_refr_now`` under ``s_rgb_mtx``.
The only thing that moves during a bake is the arc, and the arc is an ordinary LVGL object, not a panel write.
That leaves one sanctioned exception to the atomic-frame rule: the screen-switch brightness ramp (register 0x51), which writes no pixels.
The strip-by-strip bake reveal that came before this design is described in :doc:`/project/history`.

The three pieces
----------------

The flat phase disc (core: ``moon_bake_phase_dummy``)
........................................................

:cfile:`components/moon/core/moon_bake.c` holds the disc, declared in :cfile:`components/moon/include/moon.h`.
Like the rest of the portable core, it is shared verbatim with the host simulator and the knob.

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

:cfunc:`moon_bake_phase_dummy` uses the same sphere geometry as the real bake's ``bake_px``.
It has the same ``right``/``up`` basis, the same ``E`` (the Moon's unit normal) and the same ``S`` (the Sun direction).
Everything expensive is gone: no albedo tap, no relief gradient, no event tint, and no texture loads from PSRAM (the external pseudo-static RAM).
Each pixel inside the disk costs one ``sqrtf`` and two dot products.
That is tens of ms on the S3, against about 1.3 s for the full bake (about 1 s on the knob).

A full moon comes out as a uniform bright disc with a Lambert falloff towards the limb.
A crescent is the lit wedge the phase dictates.
The dark side is plain 0, with no earthshine and no backlit surface, the same ``dark_zero=1`` posture the display bakes use.
The disc shows the current phase at the correct size and position with uniform albedo, so the panel reads as "the moon, computing" instead of black.

The watermark and the arc
.........................

The producer is the strip loop :cfunc:`moon_bake_progress` in :cfile:`components/ui/moon_ui.c`.
It calls ``moon_bake_rows`` once per ``MOON_COMPUTE_ROWS``-row chunk into the PSRAM bounce buffer.
The chunk is 10 rows on the S3 (**40 watermark steps**) and 13 rows on the P4 (**50**); a ``#error`` pins the divisibility (:clabel:`components/ui/moon_ui.c:1350`).
After every chunk the producer raises the rows-completed watermark (``s_compute_pub``) under ``s_rgb_mtx``.
It calls ``vTaskDelay(1)`` per strip so the IDLE1 task can feed the task watchdog (TWDT).
The owner flag (``s_compute_owner``: ``COMPUTE_HOURLY`` or ``COMPUTE_REPLAY_FINAL``) is cleared last, under the same mutex.
Nothing on the display side converts the watermark; the screens read it through the seam:

.. code-block:: c
   :caption: components/ui/moon_ui.h — the seam (the knob's seam answers the same pair; the uisim stubs it)

   bool    moon_ui_compute_active(void);   /* a compute bake is in flight */
   int32_t moon_ui_compute_progress(void); /* 0..100 (pub*100/MOON_PX) */

The tick of the home and Wi-Fi screens, :cfunc:`arc_seconds_tick` in :cfile:`components/ui/screens.c`, drives the existing seconds arc from the seam.
The arcs are ``objects.arc_clock_seconds_modem`` and ``_wifi``, with a range of 0 to 60.
While a compute is active the arc value is ``progress * 60 / 100``.
The watermark advances per strip, so the arc sweeps in 40 steps on the S3 and 50 on the P4 over the final bake.
On the first tick after the compute completes, the arc shows the real seconds again.

The arc update is a plain LVGL invalidate, with no whole-panel flush and no drain witness.
It rides the normal LVGL refresh, which is why it can move per strip while the frame rule stays intact.
The readers need no lock.
The watermark only rises within one bake, and the owner flips back to ``COMPUTE_NONE`` under ``s_rgb_mtx`` after the last strip.
A tick therefore sees either (active, rising) or (inactive, ignored), never a torn pair.

The hourly refresh keeps the old frame up
.........................................

The once-per-hour refresh sends no brightness (0x51) traffic.
The probe's gates are in :cfunc:`moon_hourly_probe`; when they pass, the tick's start leg spawns the producer.
The old frame stays on the panel for the whole bake.
The finished frame then lands as one atomic whole-panel flush in the bright panel (the done leg of :cfunc:`moon_hourly_finish`).
On a TF (microSD) cache hit, the cached frame also lands atomically over the old frame, with no fade and no black.
The arc sweeps during a cold bake; on a cache hit there is no compute, so the arc keeps showing seconds.

Every exit path is simple because the old frame is always a consistent whole-panel state:

* A blown deadline or a failed producer leaves the old frame up, so there is nothing to restore.
* A sleep entry mid-bake sets ``s_compute_stop`` (in :cfunc:`moon_ui_pause`), and the producer deletes itself before the publish. The partial bounce is never shown, and the wake re-bakes on its next cycle.
* The replay's final leg watches ``s_replay_abort`` the same way.

There is no heal path, because the partial rows of an in-flight bake are never displayed.

Where the disc shows, and where it does not
-------------------------------------------

The disc appears only where the panel would otherwise be dark, that is, when there is no committed frame to stay on:

.. list-table::
   :header-rows: 1
   :widths: 26 34 40

   * - Window
     - On the panel while it computes
     - Notes
   * - Clock boot, first frame (replay or sync path)
     - the quick phase disc (:cfunc:`moon_boot_disc`, called on both the replay path and the sync path)
     - baked into ``s_frame`` under ``s_rgb_mtx`` + ``moon_convert_to_rgb``,
       shown by the ordinary tick as one atomic flush; moves no
       ``s_baked_day``/``s_baked_hour`` bookkeeping (set from the real
       epoch); the first replay frame or the finished bake replaces it
       atomically; the arc sweeps the final bake
   * - Clock boot, no time
     - stays black: no epoch, no phase, no disc
     - the day-tick re-bakes once the clock syncs
   * - Hourly cold bake / replay final bake
     - the old frame (or the newest stored replay frame) stays up; the
       disc does not appear
     - a computed image is present, so the panel stays on it; the arc sweeps the watermark
   * - Knob boot, saved frame on glass (``KNOB_UI_LAST_FRAME``)
     - the saved frame (≤ 2 days old, crc intact) stays up
     - the fresh bake swaps over it, old frame first and then one atomic swap; the arc sweeps
   * - Knob boot, no saved frame (first boot after a fresh flash)
     - the quick phase disc (:cfunc:`disc_publish`, :cfile:`components/knob_ui/knob_ui.c`)
     - published through the ordinary ``s_pub``/swap machinery (the swap
       unhides the disk atomically), ``wait_ack()`` before the real bake
       reuses the other slot; the arc sweeps the per-row watermark; the
       disc is never persisted, so ``/spiffs/moon_frame.bin`` stays the
       last real bake

The watermark contract
----------------------

One rule carries the whole design: **the producer writes the PSRAM bounce strip by strip, and never writes a display buffer strip by strip.**
Everything the panel can show (the disc, the old frame, the saved frame) is a complete frame that lands through the existing atomic publish and show paths.
The only strip-wise artifact is the watermark, a counter the screens read for the arc.
That has three consequences:

* No half-old, half-new composite is possible, because there is nothing to composite.
* No heal path is needed. An abort (sleep entry, replay abort, blown budget) leaves a complete frame on the panel by construction; the producer does not publish.
* ``s_frame`` (the 8-bit truth) is written whole. The image gate's byte-identity proof still holds: the finished bounce is exactly what ``moon_bake`` would produce (the same ``moon_bake_rows`` calls, the same ``dark_zero=1, dk_sup=0.0f``). The disc is an additive function that the gate's frames never call.

Cost
----

The disc is one row loop with no texture loads: tens of ms on the S3, a fraction of a second on the knob.
It is the only extra computation in the pipeline.
It runs on the boot task (clocks) or the producer (knob), and the real frame replaces it atomically.

The arc costs one ``lv_arc_set_value`` per tick while a compute is active.
That is an ordinary LVGL invalidate, the same cost the seconds tick already pays.
There is no 0x51 traffic, no extra flush and no churn on the drain witness.
A probe that follows a multi-strip flush loses its own completion (see :doc:`display`), but that does not apply here.
The bake is still one atomic whole-panel show at the end, so the clock pays the ~53 ms witness once per completed flush, not once per strip.

The hourly cycle has no fade legs (the earlier design spent 2 × 240 ms of 0x51 traffic on them).
The dark window on the hourly refresh is zero, because the old frame is up the whole cycle.

The knob's boot bake runs at its ~991 ms plain baseline plus the disc's tens of ms.
With a per-band display, the knob measured 2028–2059 ms, because the swap task's per-band ``lv_refr_now`` (priority 7) preempted the producer (priority 5).

No memory is added.
The disc bakes into the existing ``s_frame`` (clocks) or back buffer (knob), and the watermark uses three flags: owner, pub and stop.

Configuration
-------------

There are no Kconfig keys for this behaviour.
It is unconditional on every board, with no option to fall back to black windows or a strip reveal.
The keys that controlled the earlier reveal and hourly fade were deleted; :doc:`/project/history` lists them.

Measured bake cost
------------------

The bench numbers below were taken with the earlier reveal firmware.
They remain the accepted bake-cost measurements, because the producer (the strip loop with the watermark) is the same.
On the clocks its internals do not depend on the display.
The knob's numbers include the band-flush tax of the reveal display; :doc:`on-device verification <../verification/device-verify>` re-measures them on the current firmware.

.. list-table::
   :header-rows: 1
   :widths: 40 28 32

   * - Flow (reveal-era bench)
     - Measured
     - Today
   * - S3 boot final bake, strip loop
     - **2009–2615 ms** (400/400 rows), 40 one-strip watermark states
       at ~80 ms cadence (re-measured)
     - same producer; the watermark drives the arc, not a band display;
       the disc is up from boot (~tens of ms) instead of black
   * - S3 hourly cold bake
     - **2687 ms** internals, whole cycle **3083 ms** (inside the 5 s
       budget)
     - same internals; the cycle owes no fade legs (no 0x51
       traffic), so the whole cycle is the bake + one atomic show
   * - S3 hourly warm TF hit
     - **185–235 ms** frame land (+ two 240 ms fade legs)
     - the hit lands atomically over the old frame in the bright panel;
       no fade
   * - P4 boot final bake
     - **2662 ms** (650/650 rows, 50 one-strip states)
     - same producer, 50 watermark steps
   * - Knob boot bake
     - 991 ms hidden baseline / **2028–2059 ms** revealed (the band tax)
     - the ~991 ms baseline + the disc (tens of ms); the arc
       sweeps the per-row watermark over 360 rows

The P4 (4C) bench of this design (hourly refresh with no fade, plus the arc) is still open, with the rest of the bench list.

Where to go next
----------------

* :doc:`architecture`: the atomic-frame invariant and its one sanctioned exception.
* :doc:`animations`: the boot replay and the hourly refresh, with the disc and the arc.
* :doc:`knob-display`: the knob's boot posture (the disc publish and the saved-frame path) and its seam.

Other pages this one depends on: :doc:`display` (the drain witness, and why the design avoids a band display) and :doc:`../verification/performance` (the ladders; the pacing figure's tick row is the watermark's tick, and the hourly ladder owes no fade legs).
The authoritative sources are :cfile:`components/ui/moon_ui.c` (the compute section, the seam and :cfunc:`moon_boot_disc`), :cfile:`components/ui/screens.c` (:cfunc:`arc_seconds_tick`), :cfile:`components/moon/core/moon_bake.c` (:cfunc:`moon_bake_phase_dummy`), and the knob's ``knob_ui.c`` / ``knob_moon.c``.
