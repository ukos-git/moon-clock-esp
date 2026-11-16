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
     - Countdown from light sleep to deep sleep, 60–1800 s. Armed on
       the light-sleep entry itself — the sleep screen is a static dim
       frame (the dusk pole-spin was retired 2026-11-13), so the timer
       starts the moment the panel dims, not on an animation's last
       frame — and triggers the deep-sleep transition on expiry.
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
       back to 100. The light-sleep screen itself is static (the dusk
       pole-spin was retired 2026-11-13) — the dim is a held brightness
       level, not an animation. The ramp follows the fade's panel-io
       rule (panel mutex per 0x51 step, one 0x51 ramp at a time under
       ``s_fade_busy``), so a brightness write is never queued behind a
       strip DMA that could stall the bus.
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
       moon image) stays atomic. One of the two flows that ride the
       hardware brightness ramp — the sole sanctioned exception to the
       whole-panel-atomic invariant (the hourly refresh reuses this
       same fade; :doc:`the display pipeline <../implementation/display>`).

The hourly moon refresh & TF hour cache
----------------------------------------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_MOON_HOURLY_ENABLE``
     - ``y``
     - Once-per-hour moon refresh: at each hour boundary the home's
       moon fades to black on the panel's OWN hardware brightness (the
       same CO5300 ``0x51`` command / P4 backlight PWM as the screen
       switch), a core-1 task produces the current-phase frame at the
       **hour centre** (a TF hour-cache fetch when the card can serve
       it, else a full bake with write-through store), the finished
       frame lands as ONE atomic whole-panel flush while the panel is
       still black, then the brightness ramps back. Only the brightness
       animates — the clock, alarm row and seconds keep running.
       Skipped while idle, on a moon-less screen, or while the orbit /
       re-bake pipelines or another ``0x51`` ramp own the buffers; a
       producer that misses its 5 s budget leaves the OLD frame (never
       a stranded black). The fade reuses the screen switch's rule —
       the hardware brightness ramp is one of the two sanctioned
       exceptions to the atomic-flush invariant (the other is the bake
       reveal below).
   * - ``UI_MOON_HOURLY_FADE_MS``
     - ``UI_SCREEN_SWITCH_FADE``
     - Each fade leg of the hourly refresh (down to black, back up),
       40–2000 ms, paced like the screen-switch fade with the same 12
       steps. With ``UI_MOON_BAKE_REVEAL`` on, the fade-back is paid at
       the bake's first completed strip, so raising this lengthens the
       dark window by the fade itself (the bake is revealed underneath,
       not hidden behind it); on a TF fetch hit the produce pass runs
       while the panel sits black between the two ramps.
   * - ``UI_MOON_HOURLY_TFCACHE``
     - ``y``
     - Serve the hourly refresh frames from the TF hour cache: the
       producer asks the card first (a hit is a decode instead of a
       ~1.3–2.4 s bake) and write-through stores every hour it had to
       compute. The card mounts **lazily on the first producer to run**
       — with ``UI_MOON_BOOT_REPLAY`` on that is the boot replay task,
       otherwise the first hourly refresh (boot never touches the SD
       bus) — and every card failure falls back silently to the bake
       path. The hourly producer also runs the cache-policy scan each
       hour (new-moon floor + synodic cap). Depends on
       ``UI_MOON_HOURLY_ENABLE`` and ``UI_MOON_TFCACHE``.
   * - ``UI_MOON_BAKE_REVEAL``
     - ``y``
     - Turns the black windows of the current-phase calculation (the
       hourly cold-bake leg, the boot replay's final "now" bake) into
       the calculation made visible: the producer bakes the PSRAM
       bounce strip by strip and publishes a rows-completed watermark;
       the tick displays each completed strip as ONE atomic band flush
       over a moon square cleared to black — the moon draws itself
       north to south at the ACTUAL bake rate. The hourly fade-back is
       paid at the FIRST completed strip (dark for the fade window
       only, never fade + bake). TF hits show atomically as before;
       ``s_frame`` is never touched, so any abort heals the old frame
       back whole. The second sanctioned exception — *atomicity-per-
       state* (:doc:`the bake reveal <../implementation/bake-reveal>`).
       Depends on ``UI_MOON_HOURLY_ENABLE``. Off: the old
       black-until-computed posture everywhere.
   * - ``UI_MOON_TFCACHE``
     - ``y``
     - TF hour cache: persist the hourly bakes on the TF card as small
       gray JPEGs — **one per hour** of the cycle, ≤ ~709 files ~10 MB
       — under ``/sdcard/moon_cache/<calcver>_<target>_<px>/``, keyed
       on the hour **baked at the hour centre** (``hour * 3600 +
       1800``), so a warm hour reads the frame instead of computing it
       (the panel's dark window shrinks from bake+fade to decode+fade,
       ~200 ms + the ramps) and a cached hour is byte-identical across
       boots and boards. Versioned by the calculation (``MOON_CALC_VER``
       = ``git describe`` — bump the repo with commitizen ``cz bump``
       to invalidate) and **self-describing**: a ``meta.txt`` pins the
       bake resolution (plus albedo width, relief, JPEG quality), any
       mismatch wipes and recreates the folder; old version folders are
       deleted on sight. The cache is **cleared on each new moon** and
       cannot hold more than one synodic month of hourly frames: the
       floor is the newer of (one synodic cycle back from the newest
       hour) and (the last new moon, computed by the core-1 producers
       via ``new_moon_epoch`` and persisted as a non-authoritative
       ``meta.txt`` line). Write-through: every hour the device
       computed is stored. Missing card, failed mount, undecodable
       file, foreign filesystem or a mid-run detach all fall back
       **silently** to baking on-device (the card-less path); fetched
       frames go through the same ``moon_gray565`` tint LUT, so fetch
       and compute are pixel-identical by construction. The card mounts
       lazily on the first producer to run (the boot replay when
       enabled, else the first hourly refresh), and the day's sky-event
       occlusion is part of the cached bake epoch.
   * - ``UI_MOON_TFCACHE_JPEG_Q``
     - ``90``
     - Gray JPEG quality, 1–100 for the hour cache. The panel verdict
       chain: q 10 (the knob-demo recipe class) showed visible blocking
       in the flat dark and bright areas; q 32 *still* showed it (a
       warm card replay, 2026-11-08) even though the flats measured
       clean;
       90 is the visually-transparent class for gray DCT encoding. A
       few times q 32's ~7.5 KB (400 px) / ~18 KB (650 px) per frame,
       and encode/decode cost is resolution-bound, not quality-bound.
       ``meta.txt`` pins it — a change wipes and re-bakes the cache
       folder.
   * - ``UI_MOON_BOOT_REPLAY``
     - ``y``
     - Boot animation: a core-1 task replays ALL stored TF hour frames
       (ascending) and stops on the newest; the live-computed current
       phase lands as the final frame right after. No usable cache (no
       card, cold folder, version wipe) → the display stays black until
       the current phase is computed. Frames are shown untinted
       (neutral LUT) as ONE atomic whole-panel flush each,
       drain-witnessed; the brightness never moves (no fade). The
       idle→sleep ladder is held for the presentation and re-armed when
       it ends; a screen switch or sleep entry aborts the animation
       (the day-tick re-bakes later). Demo mode boots without it.
       Depends on ``UI_MOON_HOURLY_ENABLE``, ``UI_MOON_HOURLY_TFCACHE``
       and ``UI_MOON_TFCACHE`` (it rides their bounce buffer and
       one-shot init). The knob has no SD socket — its boot is always
       the black-until-first-frame path.
   * - ``UI_MOON_REPLAY_FPS``
     - ``8``
     - Boot replay pacing floor (minimum dwell per frame), 2–20 fps.
       The actual rate is what the 50 ms tick and the whole-panel flush
       allow on top (~6 fps measured on the sweep-era TF replay);
       higher values simply let the producer feed as fast as the panel
       drains.
   * - ``UI_MOON_REPLAY_BUDGET_MS``
     - ``240000``
     - Wall-clock budget for the whole replay animation, 20–600 s. On
       overrun the animation stops where it is and the CURRENT phase is
       still baked and shown — the budget cuts the animation, never the
       moon.

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
     - Drive the moon viewport from the QMI8658 IMU. The moon's
       in-plane pose is fixed (the burn-in spin was removed 2026-11-13
       — the home shows the baked phase static); tilt changes which
       side of the sphere the camera sees (the out-of-plane orbit, plus
       the whole-screen orientation correction) and counts as user
       activity (holds the idle timer, wakes from sleep).
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
     - **Obsolete.** The moon's in-plane pose is fixed (the burn-in
       spin was removed 2026-11-13), so a resting roll offset has no
       effect. Kept so existing sdkconfig files keep validating.
   * - ``UI_CLOCK_ROLL_SIGN``
     - ``1``
     - Screen-orientation correction sign (+1/−1). The screen-plane
       gravity vector picks the whole-screen 90°-step orientation
       (100 ms sampling → int4 quantization → 1 s running mean →
       quadrant decision via integer dot/cross); if the UI rotates the
       wrong way when tilted, set −1.
   * - ``UI_ORI_BASE_V10``
     - ``900``
     - Base screen orientation in 1/10 deg (clockwise) at step 0: the
       panel mount offset — all three moon-clock panels are mounted the
       same 90° CW off the bezel's 12 o'clock, so it is a single shared
       value, not a per-board cascade (verified on the bench 2026-11-16;
       the S3 had sat at 1800 since 37ee73b and the knob fell through to
       1800 — both regressions, undone by reaching 90 only by hand). It
       is the single source for the offset — the shared clock widget and
       every screen root carry it — applied at init and on the 2 s press
       re-sync (the burn-in spin's pre-rotation it once also fed was
       removed 2026-11-13). Multiple of 900; the ZERO ANGLE roller trims
       it per clock on top.
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

Clock widget
------------
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_CLOCK_ROLL_ENABLE``
     - ``n``
     - Clock digit roll: animate a minute-pair change as a roller-style
       roll (two-line strip, ease-out, 300 ms, wheel-delta direction —
       task #54's split-flap look). OFF by default: the roll fires
       exactly when the seconds arc completes its cycle and read as a
       spurious ARC animation (user 2026-11-13) — pairs hard-swap in
       one frame. The path stays compiled for an opt-in build (the
       hard-swap branch is exactly what a disabled roll takes; LVGL's
       ``lv_arc_set_value`` itself is instant — the arc never animated).

Alarm
-----
.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``UI_ALARM_RING_MAX_MIN``
     - ``30``
     - Hard cap on how long one alarm may ring, 1–240 min (user
       decision 2026-11-16: "the alarm should not fire longer than
       30min"). Two layers enforce it: the ``alarm_audio`` play_task
       breaks on its OWN elapsed deadline (unconditional — the audio
       stops even if the UI is parked in a sleep state), and the UI
       tick auto-dismisses at the same cap: the ring is silenced, the
       alarm-active state is cleared, and — only when the device is
       awake — the alarm screen hands over to the home screen. While
       idle nobody is looking at the panel, so the cap only silences
       it and the sleep ladder keeps ownership
       (:doc:`the alarm screen <../ui/alarm>`).

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
     - Full scripted tour (:doc:`demo mode <../verification/demo-mode>`):
       the clock widget shows MM:SS (per-digit odometer) and the tour
       walks every screen and function — moon rotation, the settings
       hub walk via a second virtual pointer, the wheel keyboard
       (discard only, never commit), the alarm ring, the light-sleep
       entry and exit, the roll script, one bounded sync. Suppresses the deep-sleep
       transition, scheduled alarms, the hourly sync and the sync
       hand-off; logs ``phase=`` markers for the recording harness.
   * - ``UI_DEMO_FULL_TOUR``
     - ``y``
     - Run the full tour. ``n`` keeps the MM:SS odometer + the five
       moon screens on the plain ``UI_DEMO_SCREEN_SEC`` rotation
       (the pre-2026-11-08 showcase).
   * - ``UI_DEMO_SCREEN_SEC``
     - ``15``
     - Seconds per moon screen / settings sub-screen dwell, 5–600
       (75 s per full moon rotation at the default).

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
   * - ``TEST_SETTINGS_RETURN_SEC``
     - 0–3600
     - The scrambled-clock repro (task #30): N seconds after boot open
       the WiFi settings screen (the scan storm), dwell 2 s (so the
       exit-home switch lands MID-scan) and return home. Writes
       NOTHING to settings or credentials; the evidence is the panel
       traffic hold's arm/release lines and the ``co5300`` failure
       signature (:doc:`../implementation/display`).
   * - ``TEST_HOURLY_FORCE_S``
     - 0–3600
     - Fire the full hourly cycle (fade-out, produce, atomic show,
       fade-in) every N seconds while the home screen is active and the
       device is not idle — bench acceptance for the fade/refresh legs
       (cold card = miss + bake + store, warm card = fetch); the clock,
       alarm row and seconds keep running through every cycle.
   * - ``TEST_WIFI_SCAN_SEC``
     - 0–3600
     - Open the WiFi settings screen N seconds after boot — headless
       verification of the scan path (on the P4: the esp_hosted RPC
       scan through the remote C6, including its 5 GHz APs).
   * - ``TEST_CPU_STATS_SEC``
     - 0–3600
     - N seconds after boot, log per-task CPU % once per second —
       10 s on the home screen, then 30 s on the settings hub
       (loading it once; enables the FreeRTOS run-time stats tick).
   * - ``TEST_ROLL_TILT_SEC``
     - 0–3600
     - Feed the real 90°-step orientation pipeline synthetic
       screen-plane gravity for ~25 s (rest → +90 → 180 → −90 → rest)
       — no physical tilt needed.
   * - ``TEST_ALARM_RING_SEC``
     - 0–3600
     - Fire the real alarm path N seconds after the boot bake: ring,
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
   * - ``TEST_BAKE_REVEAL_TRACE``
     - bool
     - Log every bake-reveal band, producer strip and gated tick (rows
       + ms since the arm + drain-witness ms per displayed state; one
       line per producer strip; one gated dump per second naming the
       active gate). Bench pacing diagnostic for the reveal
       (``docs/implementation/bake-reveal``); the code compiles out
       when off.
