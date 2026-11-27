.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Thirty keys across five menus (the ``knob_*`` components' own
``Kconfig`` files; the shared clock keys the P0 seam brings in are
listed on the page above; the battery divider's ``_NUM``/``_DEN`` pair
shares one row):

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
   * - ``KNOB_UI_LAST_FRAME``
     - ``y``
     - Show the persisted last frame before the fresh wake bake lands:
       deep sleep loses the PSRAM double buffer, so every dial/tap/
       TIMER wake would otherwise start from a black window behind the
       Wi-Fi + NTP window. ``y`` persists the last applied frame to
       ``/spiffs/moon_frame.bin`` (raw RGB565, 259,200 B + a 24-byte
       header: magic, baked epoch, UTC day, event signature, crc32)
       **only when the (day, signature) pair changed** — one ~259 K
       flash rewrite per UTC day or sky-event change, not per wake —
       and the next boot's first arm shows a saved frame at most 2
       days old as ONE atomic whole-panel flush, the caption seeded
       from the saved instant (the shared home's gated row hides it;
       the legacy view rolls it with the minute like any day-tick
       frame); the fresh bake swaps over it as one atomic flush (the
       old-frame-then-atomic posture — the seconds arc sweeps the
       watermark while it bakes). A missing/torn/stale file falls back
       to the disc path: the quick phase disc lands first, then the
       fresh bake over it (:doc:`the knob display
       <../implementation/knob-display>`)
   * - ``KNOB_UI_MOVIE_ENABLE``
     - ``y``
     - Play the flash-resident boot movie (the ``moon_movie``
       partition) at boot — the knob's half of the family boot
       re-source: the first arm stages frame 0 on the glass (the
       saved-frame arm takes precedence), the producer plays the
       synodic cycle forward over the Wi-Fi + NTP + opening-bake
       window (ONE atomic whole-panel flush per frame — no new
       atomicity exception), then plays to the frame matching the
       current phase and the fresh bake reveals over it. No
       trustworthy instant by one full cycle: the loop exits and the
       opening bake reveals over the last movie frame. ``n`` keeps
       the saved-frame / disc path; a missing/stale/zero-frame movie
       never blocks a flash or a boot (:doc:`the knob display
       <../implementation/knob-display>`)
   * - ``KNOB_UI_MOVIE_FPS``
     - ``8`` (2..20)
     - The boot movie's target frame rate (one frame per 1000/FPS
       ms, floored to one tick at ``CONFIG_FREERTOS_HZ=100``): the
       knob's wire floor is ~13 ms/frame, so the default is well
       below the panel's own limit
   * - ``KNOB_MOON_TEST_EPOCH``
     - ``1794175200``
     - Frozen UTC instant for bench builds (used **only** under
       ``KNOB_TIME_USE_TEST_EPOCH``); what keeps the K7b/K7c
       acceptance epochs reproducible
       (:doc:`the knob display <../implementation/knob-display>`)
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
     - The P4 gauge: an ``adc_oneshot`` read on GPIO 1 rendered as an
       **iPhone-style battery icon** (outline + nub + proportional
       fill) at the bottom centre of the shared home — the docked fill
       takes the theme's moon color (the seconds-arc ink, not the
       reference green), red at or below ``KNOB_BATTERY_LOW_PCT``,
       bolt when docked (knob-side overlay, minute refresh). ``n``
       leaves every entry point inert — no ADC init, no icon
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
   * - ``KNOB_BATTERY_CHARGING_MV``
     - ``4400`` (0..5200)
     - The bolt heuristic: the board has no charge-state line, but
       docked the sense line reads the USB rail (bench signature
       ~4522 mV, far above the 4200 mV cell ceiling) — at or above
       this cell-mV read the icon takes its docked look (theme-color
       fill + bolt). ``0`` = bolt off
   * - ``KNOB_BATTERY_LOW_PCT``
     - ``20`` (1..99)
     - iOS-style low-battery red: at or below this percent the fill
       turns red; decorative until the multimeter session settles the
       placeholder calibration
   * - ``KNOB_DIAG_FRAME_TRACE``
     - ``n`` (bench)
     - Bench-only: log flush start/complete, swap apply begin/end,
       and every leg of the fade + atomic
       screen switch, so a frozen board's LAST log line names the
       stuck section
   * - ``KNOB_DIAG_PANIC_AT_FRAME``
     - ``0`` (bench)
     - Black-box self-test: ``abort()`` the producer after N
       published frames (the hook rides the static boot bake —
       there, ``1`` is the meaningful value); the boot after MUST
       print the gasp line and the
       flash coredump must be readable — proves the crash witness
       end to end; ``0`` = dead (production)
   * - ``KNOB_TEST_CLASSIFY_STRESS``
     - ``0`` (bench)
     - The deterministic repro of the tick-ISR crash family: the
       ``moon_task`` hammers ``knob_moon_classify_signature()`` N
       times (a swept epoch per pass), each pass preceded by a
       ``settings_store_save()`` — the spiffs write that drops the
       cache and is what turned the field crash on. Red on a
       PSRAM-backed ``knob_moon`` stack (the IDF
       ``esp_task_stack_is_sane_cache_disabled()`` assert), green on
       the internal-RAM stack the producer boots with today;
       logs progress every 50 passes and ``classify-stress: PASS N``
       on survival (:doc:`the knob display
       <../implementation/knob-display>`)
