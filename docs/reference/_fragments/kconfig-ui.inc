.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Sleep and screen switching
---------------------------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_DEEP_SLEEP_COUNTDOWN_SEC``
     - ``60``
     - Countdown from light sleep to deep sleep, 60–1800 s. Armed by
       the moon tick on the light-sleep dusk's *last frame* (the sleep
       depth follows the animation, never a fixed timer at the sleep
       entry) and triggers the deep-sleep transition on expiry.
   * - ``UI_LIGHT_SLEEP_TICK_MS``
     - ``100``
     - LVGL timer tick period in light sleep, 50–1000 ms (vs 10 ms
       active). Slower tick = less CPU and power.
   * - ``UI_LIGHT_SLEEP_DIM_PCT``
     - ``25``
     - The panel's *own* hardware brightness (CO5300 0x51 / the P4's
       backlight PWM) while the light-sleep screen is shown, 0–100 %.
       The entry ramps down to this level over
       ``UI_SCREEN_SWITCH_FADE`` ms; the wake / an alarm ring ramps it
       back to 100. The dusk keeps spinning at this level. The ramp
       follows the fade's panel-io rule (panel mutex per 0x51 step,
       ``s_fade_busy`` holds the spin's writes), so a brightness write
       is never queued behind a strip DMA that could stall the bus.
   * - ``UI_CLOCK_ALPHA_PCT``
     - ``20``
     - The shared clock widget's LVGL opacity in light sleep, 0–100 %.
       A plain panel has no true alpha, so the clock's own opacity is
       the alpha; the panel dim (``UI_LIGHT_SLEEP_DIM_PCT``) carries
       the readability, this option dims the clock further relative to
       the moon. Light-sleep only — deep sleep hides the clock.
   * - ``UI_SCREEN_SWITCH_FADE``
     - ``240``
     - Screen-switch fade-to-black time in ms, 0–2000 (0 = the plain
       instant atomic switch). A switch dims the panel's hardware
       brightness to black, loads the new screen, flushes it as ONE
       atomic whole-panel flush while the panel is still black, then
       ramps back — the transition writes no pixels. Total cost is
       roughly 2× this time plus one hidden flush. The exception: the
       both-moon switch (airplane / wifi / the sleep screens share one
       moon image) stays atomic. The sanctioned second exception to the
       whole-panel-atomic invariant, next to the burn-in spin's direct
       write (:doc:`the display pipeline <../implementation/display>`).

The moon animation pipeline
----------------------------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_MOON_ANIM_ENABLE``
     - ``y``
     - Play the synodic boot sweep (last NEW MOON → now, at most 29.5
       days) at boot, in ``UI_MOON_ANIM_FRAMES`` frames, baked off
       screen and displayed as atomic whole frames. Always starts
       dark and ends on the current phase; hands the spin back at the
       exact on-screen pose. BOOT only — wakes show the current phase
       static.
   * - ``UI_MOON_ANIM_FRAMES``
     - ``1344``
     - Sweep frame count, 5–4096. Evenly spaced from the last new moon
       to the current phase; the step shrinks as the cycle is younger
       (up to ~0.022 d/frame ≈ 32 min at an old moon at the default).
       4x the pre-2026-11-07 count (336) — at the old density the
       per-frame illumination jump read as a visible stutter.
   * - ``UI_MOON_ANIM_BAKE_PX``
     - ``400`` (S3) / ``650`` (P4)
     - Intermediate-frame bake resolution, 100–650 px, upscased
       nearest-neighbor to the full ``MOON_PX``. **First
       ``default X if BOARD_P4`` pattern in the project** — the
       conditional line must precede the unconditional fallback
       (Kconfig defaults are first-match-wins). Both boards default to
       their own ``MOON_PX`` (pixel-identical to the static/spin
       bakes); measured S3: 200 → ~0.21 s/frame, 240 → ~0.28, 320 →
       ~0.48, 400 → ~0.72. Values above the board's ``MOON_PX`` clamp
       to it. The final frame and the static/spin phase are always full
       resolution.
   * - ``UI_MOON_ANIM_ON_WAKE``
     - ``y``
     - **Obsolete.** The sweep plays at boot only; wakes show the
       current phase static. Kept so existing sdkconfig files keep
       validating.
   * - ``UI_MOON_DUSK_ENABLE``
     - ``y``
     - Light-sleep dusk: the moon spins about its *pole* at the current
       phase (1°/frame, the shortest arc to the fully unilluminated
       view, at most a half turn, 14–95 s at ~0.2 s/frame) and the
       deep-sleep countdown arms on the last frame. Constant
       projection, not a re-bake: the fully-lit body-frame equirect map
       is baked once for the epoch and every intermediate frame
       re-projects it (~50 ms gather); the final dark-side frame is one
       full-``MOON_PX`` bake.
   * - ``UI_MOON_SPIN_ENABLE``
     - ``y``
     - Free spin: the current phase is baked once and rotated
       in-plane (nearest-neighbor gather, ~8 ms/frame) at a rate that
       eases from the active period to the settled period. Each frame
       is written straight to the panel from the render task (core 1)
       via a wrapped ``esp_lcd_panel_draw_bitmap`` (the sanctioned
       second exception to the atomic-flush invariant), with a UI
       keep-mask protecting every non-black UI pixel.
   * - ``UI_MOON_SPIN_PERIOD_S``
     - ``300``
     - Active spin: seconds per full turn while in use, 30–7200.
   * - ``UI_MOON_SPIN_SETTLED_PERIOD_S``
     - ``600``
     - Settled spin: seconds per full turn at rest, 60–7200 — the
       visible burn-in protection.
   * - ``UI_MOON_SPIN_FPS``
     - ``8``
     - Spin frame rate (rendered + panel-written), 2–30. Each frame is
       one ~8 ms rotate of the baked phase + a direct write of the moon
       area; the rate is time-integrated (a dropped frame is a bigger
       step, never a repeat).
   * - ``UI_MOON_SPIN_SETTLE_S``
     - ``10``
     - Seconds of idle before the rate eases from the active to the
       settled period, 2–60 (exponential, time constant ~this/3).

IMU and tilt coupling
----------------------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_IMU_ENABLE``
     - ``y``
     - Drive the moon viewport from the QMI8658 IMU. The tilt changes
       which side of the sphere the camera sees (the out-of-plane
       orbit) and counts as user activity (holds the idle timer, wakes
       from sleep, drives the spin's active rate).
   * - ``UI_IMU_ACTIVITY_DEG``
     - ``5``
     - Tilt *activity* threshold, 1–45°: the DEPARTURE from a
       settled-pose EMA (not raw per-sample deltas) that, while awake,
       counts as user activity. Two-consecutive-sample streak gates the
       fire.
   * - ``UI_IMU_WAKE_DEG``
     - ``20``
     - Tilt *wake* threshold, 5–45°: the departure that, while asleep,
       wakes the device. Higher than the activity threshold on purpose
       — table vibration and the ~0.4°/s at-rest drift must not wake.
   * - ``UI_IMU_ANGLE_SIGN``
     - ``1``
     - IMU angle sign (+1/−1), flips the orbit direction. The
       activity/wake monitor measures a departure, so it is
       sign-agnostic.
   * - ``UI_IMU_ROLL_OFFSET_DEG``
     - ``0``
     - **Obsolete.** The free spin is time-driven, not tilt-driven; a
       resting roll offset has no effect. Kept for config validation.
   * - ``UI_CLOCK_ROLL_SIGN``
     - ``1``
     - Screen-orientation correction sign (+1/−1). The screen-plane
       gravity vector picks the whole-screen 90°-step orientation
       (100 ms sampling → int4 quantization → 1 s running mean →
       quadrant decision via integer dot/cross); if the UI rotates the
       wrong way when tilted, set −1.
   * - ``UI_MOON_ORBIT_ENABLE``
     - ``n``
     - Sphere model: peek at the dark side via out-of-plane tilt.
       Requires a re-bake with a pre-rotated camera basis
       (~1.7–2.4 s with the relief DEM) displayed as one atomic frame.
       Disabled by default: the S3 cannot serve the re-bake at the
       tilt cadence, so on the S3 the tilt drives the *clock*
       orientation instead.
   * - ``UI_MOON_ORBIT_MAX_DEG``
     - ``30``
     - Max orbit (peek) angle, 0–60°. Beyond it the orbit is clamped;
       0 disables the orbit.
   * - ``UI_MOON_ORBIT_STEP_DEG``
     - ``1``
     - Orbit quantization step (re-bake trigger), 1–10°.
   * - ``UI_MOON_ORBIT_DEADZONE_DEG``
     - ``2``
     - Quantized orbit angles inside ±this band map to the default
       viewport, 0–15° (table jitter ~1.5°/s mean cannot walk the
       viewport).
   * - ``UI_MOON_ORBIT_LPF_ALPHA``
     - ``30``
     - Orbit low-pass coefficient in percent (5–95) at the 20 Hz UI
       tick.

Demo mode
----------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_DEMO_MODE``
     - ``n``
     - Showcase mode: the clock widget shows MM:SS (the seconds roll
       through the per-digit odometer) and the five moon screens
       auto-rotate every ``UI_DEMO_SCREEN_SEC`` seconds until the next
       reset. Holds the idle timer, skips the hourly WiFi sync — a
       recording run is never cut by a sleep transition.
   * - ``UI_DEMO_SCREEN_SEC``
     - ``15``
     - Seconds per screen in the demo rotation, 5–600 (75 s per full
       cycle at the default).

Test hooks
-----------
All default off (``0`` = off, except the two bools default ``n``).

.. list-table::
   :header-rows: 1
   :widths: 26 12 62

   * - Key
     - Range
     - Meaning
   * - ``TEST_AUTO_WAKE_SEC``
     - 0–3600
     - Auto-wake from sleep after N seconds while asleep (no touch) —
       exercises the wake-from-sleep transitions repeatedly.
   * - ``TEST_SETTINGS_DEMO_SEC``
     - 0–3600
     - Scripted settings run N seconds after boot: hub → pomodoro →
       alarm time → light-sleep → airplane, logging every transition
       and reading the alarm/settings files back. Headless acceptance
       path for the whole settings stack.
   * - ``TEST_SPIN_BANDS``
     - 0–100
     - Spin seam diagnostic: writes one gray level per strip instead of
       the moon (1 = normal 400-wide geometry, 2 = full-panel-width
       strips — the A/B that isolates the 400-wide window as the
       seam-line cause).
   * - ``TEST_WIFI_SCAN_SEC``
     - 0–3600
     - Open the WiFi settings screen N seconds after boot — headless
       verification of the scan path (on the P4: the esp_hosted RPC
       scan through the remote C6, including its 5 GHz APs).
   * - ``TEST_CPU_STATS_SEC``
     - 0–3600
     - Log per-task CPU % once per second for N seconds (enables the
       FreeRTOS run-time stats tick); also loads the settings hub once
       at N+1 s.
   * - ``TEST_ROLL_TILT_SEC``
     - 0–3600
     - Feed the real 90°-step orientation pipeline synthetic
       screen-plane gravity for ~25 s (rest → +90 → 180 → −90 → rest)
       — no physical tilt needed.
   * - ``TEST_ALARM_RING_SEC``
     - 0–3600
     - Fire the real alarm path N seconds after the boot sweep: ring,
       stop (the touch-dismiss path), re-fire, ring again, then the
       real dismiss. On the P4 proves the ES8311
       open/stream/ramp/fade/close/re-open cycle.
   * - ``TEST_HANG_WATCH``
     - bool
     - Dump every task's backtrace + abort when the LVGL task's
       200 ms heartbeat is stale for >15 s (an LVGL hang is otherwise
       silent — a task parked on a ``portMAX_DELAY`` lock keeps the
       idle task alive, so the task WDT never fires).
   * - ``TEST_BAKE_NO_RELIEF``
     - bool
     - Bake without the relief gradient cache (``L = clip(d, 0, 1)``)
       to measure the relief path's share of the bake cost. Visually
       wrong (flat terminator) — profiling aid only.
