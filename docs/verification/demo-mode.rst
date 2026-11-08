On-device demo tour and recording
=================================

"Every device looks the same and every function works" is only a claim
until someone films the three boards side by side and reads their
serial lines against the same clock. The **demo mode** is the scripted
tour that makes that comparison repeatable (Kconfig on both clocks and
the knob), and ``bench/record_demo.py`` is the harness that records it:
the C920 camera plus one timestamped serial log per board, cut into
per-phase comparison frames by the tour's own ``phase=`` markers.

Enabling it
-----------

.. list-table::
   :header-rows: 1
   :widths: 34 12 54

   * - Key
     - Default
     - Scope
   * - ``UI_DEMO_MODE``
     - ``n``
     - the clocks' scripted tour (S3 and P4, one build each);
       ``n`` builds the production binary unchanged
   * - ``UI_DEMO_FULL_TOUR``
     - ``y``
     - ``n`` keeps the plain MM:SS + five-moon-screen rotation (the
       original showcase)
   * - ``UI_DEMO_SCREEN_SEC``
     - ``15``
     - seconds per moon screen and per settings dwell
   * - ``UI_DEMO_DUSK_WAIT_S``
     - ``180``
     - the dusk phase's wait for the pole spin
   * - ``KNOB_DEMO_MODE`` / ``KNOB_DEMO_PHASE_SEC``
     - ``n`` / ``15``
     - the knob's canonical-phase walk (full tables:
       :doc:`UI Kconfig <../reference/kconfig-ui>`,
       :doc:`knob Kconfig <../reference/kconfig-knob>`)

One cycle of the clock tour takes roughly seven minutes on top of the
boot sweep (the tour arms only once ``moon_ui_sweeping()`` is false —
the same gate as the test hooks); the recording normally spans the
boot sweep too, since the sweep is itself a cross-device comparison.

The clock tour
--------------

Every tap of the tour goes through the **real touch path**: demo.c
creates a second LVGL POINTER indev (the BSP's real indev keeps
working for the human; the alarm/orientation indev callbacks were
bound to the real device before ``ui_demo_init`` runs) and feeds it
press/release pairs from a 10 ms engine timer. Hit-testing, the wheel's
sticky-sector reveal and every ``LV_EVENT_CLICKED`` handler therefore
execute for real — the tour verifies the touch entry, not a
loadScreen shortcut.

.. list-table::
   :header-rows: 1
   :widths: 16 44 40

   * - Phase
     - What the tour does
     - What it exercises
   * - ``moons``
     - the five moon screens on a ``UI_DEMO_SCREEN_SEC`` rotation,
       clock widget on MM:SS (per-digit odometer)
     - the shared clock widget, the atomic screen switches, the
       static current phase on every moon screen
   * - ``hub_walk``
     - a real tap opens the settings hub from home; every ring icon,
       sub-screen dwell, bottom-centre exit — including the alarm
       screen's pomodoro and real-alarm entries
     - the hub ring hit boxes, every settings entry/exit handler,
       the roller screens' entry refreshes
   * - ``wifi``
     - the hub's WiFi icon → the scan list → the first AP row (a
       prefilled fallback when no APs answer)
     - the background scan, row hit-testing, the SSID prefill
   * - ``wheel``
     - types ``demo``, tours all four registers (ABC + ``A``, 123 +
       ``1``, back to abc), centre backspaces ×2, then DISCARD (clear)
       and DISCARD again (exit)
     - the sweep press/release commit-on-release, the mode arc, the
       centre backspace (``SHORT_CLICKED`` at the 90 ms tap), the
       discard ladder
   * - ``back``
     - the WiFi screen's exit → the hub's central exit → home
     - the two exit handler levels
   * - ``dusk``
     - the real light-sleep entry (pause + dusk + dim), wait the
       ``moon_ui_dusk_complete()`` flag, hold the dark side, wake
     - the pole spin to the fully unilluminated side, the wake
       replacing the dark side atomically
   * - ``roll``
     - one run of the synthetic tilt script (``action_roll_test_begin``)
       through the real orientation pipeline
     - the int4 quantization, the 1 s window, the 90-degree steps,
       the PSRAM transform layer + centre pivot
   * - ``sync``
     - one bounded ``action_start_wifi_sync()``, claimed via
       ``action_sync_demo_done`` (45 s budget), land on home
     - the transient WiFi screen, the background handshake, the
       airplane hand-off
   * - ``alarm``
     - the real ring script: fire, stop after the volume ramp,
       re-fire (the codec RE-OPEN), real dismiss through the wake
       path; then the wake's on-demand sync is drained to home
     - the alarm screen, the ES8311 open/ramp/fade/close/re-open
       cycle (audible on both boards), the wake flow

The alarm runs **last** in the cycle on purpose. The 2026-11-08 bench
take showed the S3's QSPI completion-loss race (lost DMA completion ->
``PANEL IO STUCK`` -> panel watchdog controlled reboot) firing ~29 s
into this phase in 2 of 2 cycles — the alarm's wake/dismiss window,
where the 0x51 hardware brightness ramp, the fade-to-black switch and
the spin resume overlap (the same family as the #96 settings-freeze
stall). Placing the phase at the cycle end keeps dusk, roll and sync
comparable on every cycle even while that watchdog recovery runs; the
race itself is a documented follow-up.

Tap-point geometry: the screen roots carry the base orientation
(``UI_ORI_BASE_V10`` — 180° on the S3, 90° on the P4) and the roll
steps, always multiples of 90° about the panel centre. A widget's AABB
centre is therefore rotation-safe for ordinary taps; the wheel's
annulus/sector points (which live in the wheel's LOCAL space, the
space its handlers' inverse transform expects) are mapped into display
coords through the wheel screen root's own
``lv_obj_transform_point`` matrix.

Two things are deliberately **never tapped**: the wheel's circle-CHECK
commit key (it would write ``spiffs/wifi_credentials.txt`` and start a
connect) and the alarm screen's deactivate-all master (it would really
disarm the user's alarms). The tour's edits are bounded to the
pomodoro/real-alarm entry screens, which only re-seed their rollers'
current values.

Suppression while the tour runs
--------------------------------

The tour owns the display until reset; anything that would hijack it
is refused at the action layer (``components/ui/actions.c``):

* the **deep-sleep transition** returns early (and disarms the source
  countdown) while ``action_demo_active()``;
* **scheduled alarms** are dropped in ``action_alarm_triggered`` —
  the alarm phase opens this with ``action_demo_allow_alarm(true)``
  while it deliberately rings;
* the **hourly on-demand sync** and the **sync-complete hand-off** are
  skipped (the flag stays unclaimed; the tour claims it with
  ``action_sync_demo_done`` when it wants the result);
* the idle monitor is switched off at ARM and the idle timer is held
  every second, so no sleep countdown ever expires mid-take.

The knob tour
-------------

With ``KNOB_DEMO_MODE`` the producer task (``knob/components/knob_ui``)
replaces the day-tick loop after the boot presentation: eight
canonical phases of the current synodic month (epochs pinned at k/8
of the month — the same phase the clocks show, frame-comparable),
then a ninth step on the LIVE current phase (re-baked every cycle,
events on). The caption carries the phase name; the power ladder stays
held, so the knob never dims or deep-sleeps during a take.

The serial contract
-------------------

Every phase entry logs, on all three devices:

.. code-block:: text

   I (123456) DEMO: phase=hub_walk cycle=2 screen=6

The harness parses exactly this line; ``cycle`` lets a long take
contain several passes and the montages group by ``(cycle, phase)``.

The recording harness
----------------------

.. code-block:: text

   source ~/esp/esp-idf/export.sh          # pyserial lives in the IDF env
   python3 bench/record_demo.py --minutes 30 --boot-pulse

* **Board identity from udev, never a hard-coded port** (the mapping
  follows the USB topology): ``1a86:55d3`` is the P4's CH9102,
  ``303a:1001`` with the bench MACs is the S3 / knob; ``--map PORT=BOARD``
  overrides. A sleeping knob has no node until the dial is turned.
* **Passive serial**: ``dtr``/``rts`` are held low BEFORE ``open()`` —
  a naive open of an Espressif USB-JTAG node cold-boots the board.
  ``--boot-pulse`` pulses RTS deliberately (the EN reset), so the
  boards boot in the camera's first second.
* **Camera**: raw ``yuyv422`` 1080p straight through x264 — the
  C920's own MJPEG encoder is the artifact source. At 1080p the raw
  YUYV stream tops out at 5 fps over USB 2 (measured: exactly 30
  frames in 6 s whatever rate is requested); resolution wins over
  frame rate for panel comparison, and every demo dwell is seconds
  long, so 200 ms granularity loses nothing.
* **Post-processing**: per phase event, one PNG per board extracted
  from the video at the marker + ``--lead`` seconds, then an
  ``hstack`` montage per ``(cycle, phase)`` under ``compare/`` — the
  side-by-side evidence. Output tree: ``video.mp4``, ``logs/<board>.log``
  (host-timestamped), ``frames/``, ``compare/``, ``timeline.csv``,
  ``meta.json`` under ``bench/recordings/<stamp>/``.
* Preflight refuses to run while ``fuser`` shows another session
  holding a port (parallel peer sessions are the usual cause).

Reading a take
--------------

* The **sweep** plays the 1344-frame spec (693 frames at the current
  cycle age) from the last new moon on every device. With a warm TF
  cache it replays (measured 2026-11-08: S3 295 s at 400 px, ~2.3 fps;
  P4 217 s at 650 px, ~3.2 fps); cold it bakes (~1.5 s/frame:
  ≈ 17 min on the S3, ≈ 1120 s ≈ 19 min on the P4): identical frame
  pacing and identical illumination curves are the first look-test.
* ``moons`` and the knob's ``full-moon``/``now`` steps compare the
  SAME epoch's rendering across the three panels (the clocks' home
  phase vs the knob's live now step).
* ``dusk`` compares the shade map + terminator smoothness; ``alarm``
  is audible in the video's audio track (both clocks carry the ES8311).
* The logs beside each take are read like any
  :doc:`device-verification capture <device-verify>`: progress lines
  present, timing sane, zero failure-class lines (no WDT fires that
  change behaviour, no ``PANEL IO STUCK``, no ``ESP_ERR_NO_MEM``).

Bench status (2026-11-08)
-------------------------

The first formal take (45 min, both clocks, ``--boot-pulse``, 1080p5;
the boards ran a binary predating the 900 ms tap settle and the
alarm-last order) produced:

* Both boards swept 693/693 frames from the warm TF cache (S3 295 s,
  P4 217 s) and armed 78 s apart. The tour walked the script
  identically on both panels: hub walk 16 tap steps on the S3 and 18
  on the P4 (the CLOCK icon), the WiFi scan found 2 APs and the tour
  tapped row 0, the wheel typed ``demo``/``A``/``1`` plus backspaces
  and ran the discard ladder. The P4 completed every cycle with no
  reboot.
* Two tour bugs surfaced from the take's own logs: the 350 ms tap
  settle verified *through* the fade-to-black switch (the 36/66
  ``loadScreen`` fallback lines in those logs are the pre-fix binary;
  the settle is now 900 ms) and a stale wheel end-check that expected
  the pre-discard entry length.
* **Reproduced in the field** on the S3: ``PANEL IO STUCK`` fired
  every cycle at +29.1 s / +28.9 s after ``phase=alarm`` — each time
  the panel watchdog rebooted the board (``rst:0xc`` RTC_SW_CPU_RST,
  no panic), back up in ~13 s plus the warm re-sweep. That is why the
  alarm phase runs last now; the race itself (the wake/dismiss
  window: 0x51 brightness ramp + fade-to-black + spin resume, the
  same family as the #96 settings-freeze stall) is an open follow-up.
* Known noise, non-fatal: intermittent ``task_wdt`` IDLE0-starvation
  bursts on the S3 during the boot sweep. Zero ``ESP_ERR_NO_MEM``
  anywhere, zero WDT lines on the P4.
* The knob was not on the bench for this take (a sleeping knob has no
  serial node until the dial is turned); it is build-gated with the
  clocks.
