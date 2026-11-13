.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Twenty-seven keys across five menus (the sibling project's own
components; the shared clock keys the P0 seam brings in are listed on
the page above):

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Key
     - Default
     - Meaning
   * - ``KNOB_UI_ENABLE``
     - ``y``
     - The P1 umbrella: ``y`` boots the **shared** home screen
       (``components/ui`` over the render seam); ``n`` keeps the K7
       own-screen moon view (disk + caption) and keeps ``ui`` out of
       the runtime start — the plan's staged bring-up
       (:doc:`the knob display <../implementation/knob-display>`)
   * - ``KNOB_MOON_TEST_EPOCH``
     - ``1794175200``
     - Frozen UTC instant for bench builds (used **only** under
       ``KNOB_TIME_USE_TEST_EPOCH``); what keeps the K7b/K7c
       acceptance epochs reproducible
       (:doc:`the knob display <../implementation/knob-display>`)
   * - ``KNOB_MOON_SWEEP_FRAMES``
     - ``1344`` (0..4096)
     - Cold-boot synodic sweep frame count — the clock's task #17
       no-jump count; 0/1 = static only. ~Linear frame cost,
       measured 314 s on the S3R8
   * - ``KNOB_MOON_SWEEP_BAKE_PX``
     - ``128`` (96..360)
     - Intermediate sweep bake edge (cost ~px²); the final frame
       always bakes crisp at 360. Floor 96 = the clock's measured
       sharpness floor
   * - ``KNOB_DEMO_MODE``
     - ``n``
     - Bench demo tour (the knob half of
       :doc:`demo mode <../verification/demo-mode>`): after the boot
       presentation the producer walks 8 canonical phases of the
       current synodic month + a live-now step, holding each
       ``KNOB_DEMO_PHASE_SEC``; the power ladder stays held (no dim,
       no deep sleep), every step logs a ``phase=`` marker, and the
       caption names the phase in the legacy view (under the shared
       home the log marker carries it)
   * - ``KNOB_DEMO_PHASE_SEC``
     - ``15`` (5..600)
     - Seconds per demo phase step (``KNOB_DEMO_MODE`` only)
   * - ``KNOB_TIME_BOOT_SYNC_MS``
     - ``20000`` (5000..120000)
     - How long the boot arm waits for the first plausible SNTP
       answer; after the window an NVS-snapshotted last-known instant
       arms the static phase immediately (a late fix re-bakes), with
       no snapshot the glass stays ``no time`` until one lands
   * - ``KNOB_TZ``
     - ``"UTC0"``
     - POSIX TZ for the glass caption (``Nov 8 17:29`` local, the
       reading the clocks' home row gives); the bench's Central-
       European boards use ``CET-1CEST,M3.5.0/2,M10.5.0/3``; since
       P2 the shared ``clock_tz`` settings screen owns the zone on
       the family app and this key survives only in the legacy
       own-screen caption (``KNOB_UI_ENABLE=n``)
   * - ``KNOB_TIME_USE_TEST_EPOCH``
     - ``n``
     - Bench only: treat ``KNOB_MOON_TEST_EPOCH`` as trusted "now"
       and never start SNTP. Production leaves it ``n``: SNTP truth
       or ``no time``
   * - ``KNOB_DIM_AFTER_S``
     - ``15`` (0..3600)
     - Seed for the dim rung (0 = never): since P2 the light-sleep
       screen's roller (``settings.txt`` ``idle_timeout_sec``) drives
       the rung live and this value only applies while the store
       holds 0
   * - ``KNOB_DIM_DUTY``
     - ``64`` (1..254)
     - LEDC duty while dimmed (full = 255). 64 = the P1 dim-parity
       value: 25 %, the clocks' light-sleep panel; the shared clock
       widget drops to ``UI_CLOCK_ALPHA_PCT`` alongside it
   * - ``KNOB_ALARM_TIMER_WAKE``
     - ``y``
     - Deep sleep arms the S3 internal RTC timer for the next fixed
       alarm rule next to EXT0/EXT1 (the user's alarm-across-deep-sleep
       choice). ``n`` = GPIO-only sleep, alarms fire only while awake
   * - ``KNOB_ALARM_GRACE_S``
     - ``300`` (30..900)
     - A TIMER wake within ± this of the recomputed rule **is** the
       alarm (silent alarm visual); the RC-drift window
   * - ``KNOB_ALARM_LATE_MAX_S``
     - ``900`` (300..7200)
     - Early wakes within this hold at 1 Hz until the rule (never an
       early ring); anything further off (either way) is a stale
       fire → normal dial wake
   * - ``KNOB_ALARM_MAX_SLEEP_GAP_S``
     - ``82800`` (3600..172800)
     - Beyond this distance to the next rule (weekday-mask holes like
       Friday→Monday) the sleep is GPIO-only; a rule inside 60 s
       **defers** the sleep for the awake esp_timer instead
   * - ``KNOB_SLEEP_TIMEOUT_S``
     - ``60`` (0..36000)
     - Seed for the deep-sleep countdown (0 = never sleep): since P2
       the deep-sleep screen's roller (``settings.txt``
       ``deep_sleep_countdown_sec``) drives it live and this value
       only applies while the store holds 0. Sleep is additive —
       one countdown AFTER the dim rung, both from the last
       activity; the boot arm and the opening act HOLD the ladder,
       and the shared ``clock``/``epoch`` snapshot is saved first
   * - ``KNOB_TOUCH_WAKE``
     - ``y``
     - Also arm EXT1 ANY_LOW on the CST816S INT (GPIO 9): a screen
       tap wakes like a dial detent; a pending INT pulse is cleared
       by a status read before sleeping, a rejected pad degrades to
       dial-only
   * - ``KNOB_SLEEP_ALLOW``
     - ``y``
     - ``n`` = the sanctioned log-only bench mode: the would-be sleep
       and the EXT0 RTCIO verdict are logged and the device stays
       reachable over USB-JTAG. Production ships ``y``
   * - ``KNOB_BATTERY_GAUGE``
     - ``y``
     - The P4 gauge: an ``adc_oneshot`` read on GPIO 1 rendered as
       ``NN %`` at the foot of the shared home (knob-side overlay,
       minute refresh). ``n`` leaves every entry point inert — no
       ADC init, no label
       (:doc:`the knob display <../implementation/knob-display>`)
   * - ``KNOB_BATTERY_ADC_CHANNEL``
     - ``0`` (0..7)
     - ADC_UNIT_1 channel for the sense line — GPIO 1 is ADC1_CH0;
       ADC1 only, ADC2 is unusable while the radio runs
   * - ``KNOB_BATTERY_DIV_NUM`` / ``_DEN``
     - ``2`` / ``1``
     - Cell = sense × NUM / DEN across the 10K/10K divider — the
       **placeholder** pair: half-rail maths until the multimeter
       settles cell vs VBUS on the top rail
   * - ``KNOB_BATTERY_MV_EMPTY``
     - ``3300`` (2500..3800)
     - Cell mV at 0 % — LiPo **placeholder**, linear scale
   * - ``KNOB_BATTERY_MV_FULL``
     - ``4200`` (3600..4500)
     - Cell mV at 100 % — LiPo **placeholder**; the percentage is
       clamped between the pair
   * - ``KNOB_DIAG_TOUCH_INJECT_S``
     - ``0`` (bench)
     - Bench-only hook: an esp_timer one-shot arms a
       ~350 ms synthetic home tap at the crash coordinates at N
       seconds after boot, flowing through the real
       ``touch_read_cb`` — the crash-repro (task #27) and the
       sweep tap-abort acceptance (task #28) both ride it;
       ``0`` = dead (production)
       (:doc:`the knob display <../implementation/knob-display>`)
   * - ``KNOB_DIAG_FRAME_TRACE``
     - ``n`` (bench)
     - Bench-only: log flush start/complete, swap apply begin/end,
       sweep-abort, landing, and every leg of the fade + atomic
       screen switch, so a frozen board's LAST log line names the
       stuck section
   * - ``KNOB_DIAG_PANIC_AT_FRAME``
     - ``0`` (bench)
     - Black-box self-test: ``abort()`` the producer after sweep
       frame N; the boot after MUST print the gasp line and the
       flash coredump must be readable — proves the crash witness
       end to end; ``0`` = dead (production)
