On-device demo tour and recording
=================================

.. brief:
   Reader: a developer who wants to show, on camera, that the three boards look the same and every function works.
   They already know: how to build and flash a board, and how to read a serial log.
   After this page they can: build the demo tour, record a three-board take with the harness, and read the result.
   Page type: reference, with one how-to block.

The demo mode is a scripted tour that drives every screen and function of a board through its real touch path, in a loop, until reset.
It exists so that three boards can be filmed side by side and their serial logs read against the same clock.
The tour is a Kconfig option on both clocks and on the knob.
``bench/record_demo.py`` is the harness that records it: the C920 camera plus one timestamped serial log per board.
The harness cuts the video into per-phase comparison frames, using the ``phase=`` markers the tour writes to the serial log.

Turn on the demo mode
---------------------

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
   * - ``KNOB_DEMO_MODE`` / ``KNOB_DEMO_PHASE_SEC``
     - ``n`` / ``15``
     - the knob's canonical-phase walk (full tables:
       :doc:`UI Kconfig <../reference/kconfig-ui>`,
       :doc:`knob Kconfig <../reference/kconfig-knob>`)

One cycle of the clock tour takes about seven minutes.
The tour arms on a schedule after the boot bake; it does not wait for a boot animation.

Demo mode boots without the TF boot replay: ``UI_MOON_BOOT_REPLAY`` is compiled out of the boot path while ``UI_DEMO_MODE`` is on.
The scripted tour owns the display, so the moon starts static, as it does with the replay turned off.

The clock tour
--------------

Every tap of the tour goes through the real touch path.
``demo.c`` creates a second LVGL POINTER input device (indev) and feeds it press/release pairs from a 10 ms engine timer.
The BSP's real indev keeps working for a person at the board.
The alarm and orientation indev callbacks are bound to the real device before ``ui_demo_init`` runs, so the second indev never steals their target.
Hit-testing, the wheel's sticky-sector reveal and every ``LV_EVENT_CLICKED`` handler therefore run for real.
The tour checks the touch entry, not a ``loadScreen`` shortcut.

Each tap is a 90 ms press and a 60 ms release, which LVGL reads as a ``SHORT_CLICKED``.
After each tap the tour waits 900 ms for the click handler and the fade-to-black screen switch to finish, then checks it is on the expected screen.
If it is not, it logs ``loadScreen fallback`` and loads the expected screen itself.
See :ref:`input-chain-ui <input-chain-ui>` for how the second indev sits in the input chain.

The phases run in this order, and each one logs a marker on entry:

.. list-table::
   :header-rows: 1
   :widths: 16 44 40

   * - Phase
     - What the tour does
     - What it exercises
   * - ``arm``
     - waits for the boot bake, then takes over the display on schedule
     - the hand-off from the normal boot
   * - ``moons``
     - the five moon screens on a ``UI_DEMO_SCREEN_SEC`` rotation,
       clock widget on MM:SS (per-digit odometer)
     - the shared clock widget, the atomic screen switches, the
       static current phase on every moon screen
   * - ``hub_walk``
     - a real tap opens the settings hub from home; then every ring icon,
       sub-screen dwell and bottom-centre exit, including the alarm
       screen's pomodoro and real-alarm entries
     - the hub ring hit boxes, every settings entry/exit handler,
       the roller screens' entry refreshes
   * - ``wifi``
     - the hub's WiFi icon, then the scan list, then the first AP row (a
       prefilled fallback when no APs answer)
     - the background scan, row hit-testing, the SSID prefill
   * - ``wheel``
     - types ``demo``, tours all four registers (ABC + ``A``, 123 +
       ``1``, back to abc), two centre backspaces, then DISCARD (clear)
       and DISCARD again (exit)
     - the sweep press/release commit-on-release, the mode arc, the
       centre backspace (``SHORT_CLICKED`` at the 90 ms tap), the
       discard ladder
   * - ``back``
     - the WiFi screen's exit, the hub's central exit, then home
     - the two exit handler levels
   * - ``home``
     - lands on home and drains an on-demand sync if one is owed
     - the home screen's current-phase moon
   * - ``roll``
     - one run of the synthetic tilt script (``action_roll_test_begin``)
       through the real orientation pipeline
     - the int4 quantization, the 1 s window, the 90-degree steps,
       the PSRAM transform layer + centre pivot
   * - ``sync``
     - one bounded ``action_start_wifi_sync()``, claimed via
       ``action_sync_demo_done`` (45 s budget), then lands on home
     - the transient WiFi screen, the background handshake, the
       airplane hand-off
   * - ``alarm``
     - the real ring script: fire, stop after the volume ramp,
       re-fire (the codec re-open), real dismiss through the wake
       path; then the wake's on-demand sync is drained to home
     - the alarm screen, the ES8311 open/ramp/fade/close/re-open
       cycle (audible on both boards), the wake flow

The alarm runs last in the cycle on purpose.
On the S3, the alarm's wake/dismiss window was where the panel's lost-DMA-completion race fired (lost DMA completion, then ``PANEL IO STUCK``, then a controlled reboot by the panel watchdog).
With the alarm at the end of the cycle, the ``roll`` and ``sync`` phases stay comparable on every cycle even if that watchdog recovery runs.
The race was traced to the wake path's WiFi-init storm and closed by the sync-storm panel-traffic hold (:ref:`display-lost-dma`, :commit:`3b61fa9d`).
:ref:`The lost-completion record <lost-completion-record>` on the on-device verification page has the captures.

Where the tour taps
^^^^^^^^^^^^^^^^^^^

The screen roots carry the base orientation (``UI_ORI_BASE_V10``, 90° on all three panels) and the roll steps, which are always multiples of 90° about the panel centre.
The centre of a widget's bounding box (AABB) is therefore safe to tap at any rotation.
The wheel's annulus and sector points live in the wheel's local space, the space its handlers' inverse transform expects.
The tour maps them into display coordinates through the wheel screen root's own ``lv_obj_transform_point`` matrix.

Two widgets are never tapped on purpose:

- the wheel's circle-CHECK commit key, which would write ``spiffs/wifi_credentials.txt`` and start a connect;
- the alarm screen's deactivate-all master, which would really disarm the alarms.

The tour edits only the pomodoro and real-alarm entry screens, and those edits only re-seed the rollers' current values.

The tour as figures
^^^^^^^^^^^^^^^^^^^

The first figure shows ``demo.c``'s phase machine as one box; the second opens it phase by phase.

.. graphviz:: ../_graphs/demo-tour.dot
   :alt: The demo tour as one box: arm, the scripted phases, the alarm that rings last, and the loop back through s_cycle.
   :caption: Armed on schedule, the tour owns the display until reset. The cycle loops via ``s_cycle++`` with the moon index reset. The alarm sits at the cycle's tail on purpose (:commit:`bc4d0b0b`): the 2026-11-08 bench take showed the QSPI completion-loss race firing ~29 s into the alarm phase, and placing it last keeps roll and sync comparable on every cycle (the race was root-caused 2026-11-12 to the wake path's WiFi-init storm).
   :align: center

.. graphviz:: ../_graphs/demo-tour-phases.dot
   :alt: The scripted phases as a chain (moons, hub walk, WiFi, wheel, back, home, roll, sync, alarm), looping via s_cycle.
   :caption: The five-screen moon rotation follows ``s_moon_order`` (modem-sleep, WiFi, light-sleep, deep-sleep, alarm). The wheel walks the keyboard register tour (ABC + A, 123 + 1, back to abc) with DISCARD only, so the tour never commits a credential. The sync phase runs one bounded on-demand sync (``home_drain_sync``, 45 s cap).
   :align: center

What the tour switches off
--------------------------

The tour owns the display until reset.
The action layer (``components/ui/actions.c``) refuses anything that would take the display away while ``action_demo_active()`` is true:

.. graphviz:: ../_graphs/demo-tour-suppress.dot
   :alt: The six refusals actions.c applies while the demo is active.
   :caption: The six refusals while ``action_demo_active()`` is true, the same list as below. The boot replay shares the idle gate. The alarm phase re-arms the ring it needs with ``action_demo_allow_alarm(true)``.
   :align: center

* The deep-sleep transition returns early and disarms the source countdown.
* ``action_alarm_triggered`` drops scheduled alarms.
  The alarm phase opens this gate with ``action_demo_allow_alarm(true)`` while it rings on purpose.
* The hourly on-demand sync and the sync-complete hand-off are skipped.
  The flag stays unclaimed until the tour claims it with ``action_sync_demo_done``.
* The idle monitor is switched off at ``arm`` and the idle timer is held every second, so no sleep countdown expires during a take.

The knob tour
-------------

With ``KNOB_DEMO_MODE`` set, the producer task (``knob/components/knob_ui``) replaces the day-tick loop after the boot presentation.
It shows eight canonical phases of the current synodic month (epochs pinned at k/8 of the month).
A ninth step shows the live current phase, re-baked every cycle and re-reading the current time at the step itself, with events on.

The eight canonical phases are knob-only coverage.
The clocks have no canonical-epoch display, so those steps have no partner step on the clocks to compare against.
They check that the knob's bake path renders every phase correctly; because the core does not depend on the view, all three boards would agree.

The real cross-device pair is the knob's live step against the clocks' ``home`` step.
Both show the current phase, baked from the live instant by the shared core.
The harness folds the knob's ``phase=now`` marker into the ``home`` montage group (``PHASE_ALIASES``).
The caption carries the phase name; the live step shows the glass clock.
The power ladder stays held, so the knob never dims or deep-sleeps during a take.

The serial marker
-----------------

Every phase entry logs this line on all three devices:

.. code-block:: text

   I (123456) DEMO: phase=hub_walk cycle=2 screen=6

The harness parses exactly this line.
``cycle`` lets one long take contain several passes, and the montages group by ``(cycle, phase)``.

Record a take
-------------

Before you start, build and flash each board with the demo mode on, and connect the C920 camera.

1. Identify the boards.
   The harness reads board identity from udev and never hard-codes a port, because the mapping follows the USB topology.
   ``1a86:55d3`` is the P4's CH9102.
   The two ``303a:1001`` boards (S3 and knob) look the same over USB, so pass them with ``--map PORT=BOARD``.
   Their MACs are not stored in the repo; identify the ports with the CLAUDE.md board-identification steps.
   If the knob is asleep, turn the dial: a sleeping knob has no serial node.
2. Close every other session on the ports.
   The harness's preflight refuses to run while ``fuser`` shows another process holding a port (parallel sessions are the usual cause).
3. Start the recording:

   .. code-block:: text

      source ~/esp/esp-idf/export.sh          # pyserial lives in the IDF env
      python3 bench/record_demo.py --minutes 30 --boot-pulse

   ``--boot-pulse`` pulses RTS on every board (the EN reset), so the boards boot in the camera's first second.
   Other flags: ``--camera`` (default ``/dev/video0``), ``--no-camera``, ``--outdir`` (default ``bench/recordings``) and ``--lead`` (default 1.5 s).
4. When the run ends, open ``bench/recordings/<stamp>/compare/``.
   It holds one side-by-side montage per ``(cycle, phase)``.

How the harness records
^^^^^^^^^^^^^^^^^^^^^^^

The serial ports are passive.
The harness holds ``dtr`` and ``rts`` low before ``open()``, because a plain open of an Espressif USB-JTAG node cold-boots the board.
Only ``--boot-pulse`` resets the boards, and it does so on purpose.

The camera stream is raw ``yuyv422`` at 1080p, encoded straight through x264, because the C920's own MJPEG encoder produces artifacts.
At 1080p the raw YUYV stream tops out at 5 fps over USB 2 (measured: exactly 30 frames in 6 s whatever rate is requested).
Resolution matters more than frame rate for comparing panels, and every demo dwell lasts seconds, so 200 ms granularity loses nothing.

After the run, the harness extracts one PNG per board for each phase event, from the video at the marker plus ``--lead`` seconds.
It then builds an ``hstack`` montage per ``(cycle, phase)`` under ``compare/``: the side-by-side evidence.
The output tree under ``bench/recordings/<stamp>/`` is ``video.mp4``, ``logs/<board>.log`` (host-timestamped), ``frames/``, ``compare/``, ``timeline.csv`` and ``meta.json``.

Reading a take
--------------

* ``home`` (with the knob's ``now`` step folded into it) compares the current-phase rendering across the three panels: same core, same live instant.
  The knob's canonical phase steps are its own bake-path coverage, because the clocks cannot display a canonical epoch.
* ``alarm`` is audible in the video's audio track; both clocks carry the ES8311 codec.
* Read the logs beside each take like any :doc:`device-verification capture <device-verify>`: progress lines present, timing sane, and zero failure-class lines.
  That means no WDT fires that change behaviour, no ``PANEL IO STUCK`` and no ``ESP_ERR_NO_MEM``.
* A ``loadScreen fallback`` warning in the hub walk is known noise, described in `Known tour noise`_.

Known tour noise
^^^^^^^^^^^^^^^^

The settings screens can outlast the 900 ms tap settle.
The 720² fade and the roller re-seeds take longer than that, so the tour's check sometimes reads the previous screen.
A take logs about 18 (S3) and 22 (P4) ``loadScreen`` fallbacks per cycle, all in the hub walk, all with the "one screen behind" signature.
Every one recovers: the fallback loads the expected screen before the next tap, and no mistap has been seen.
The cost is log noise plus one redundant atomic flush per step.
The agreed fix is to poll until the screen settles instead of waiting a fixed time; it is kept as a tuning item.

The first formal take
---------------------

The first formal take ran on 2026-11-08: 45 minutes, both clocks, ``--boot-pulse``, 1080p at 5 fps.
The boards ran a binary that predated the 900 ms tap settle and the alarm-last order.

* Both boards armed 78 s apart.
  The tour walked the script the same way on both panels.
  The hub walk took 16 tap steps on the S3 and 18 on the P4 (the P4 has the CLOCK icon).
  The WiFi scan returned 2 APs on the S3 and 3 on the P4 (radio visibility), and row 0 was tapped on each.
  The wheel typed ``demo``/``A``/``1`` plus backspaces and ran the discard ladder.
  The P4 completed six full cycles without a reboot.
* The take's logs showed two tour bugs.
  A stale wheel end-check expected the entry length from before the discard; it is fixed in the tree.
  The tap settle went from 350 to 900 ms, which fixed verification on the plain screens but not on the settings screens (see `Known tour noise`_).
* On the S3, ``PANEL IO STUCK`` fired about 29 s into the alarm phase in all four cycles the board ran.
  This was the lost-completion race, and it is why the alarm runs last.
  :ref:`The lost-completion record <lost-completion-record>` follows it from this take to the fix.
* The take logged zero ``ESP_ERR_NO_MEM`` on either board and zero WDT lines on the P4.
  The S3's intermittent, non-fatal ``task_wdt`` IDLE0-starvation bursts came from the boot sweep, a retired flow (:ref:`retired-flow-captures`).
* The knob was not on the bench for this take (a sleeping knob has no serial node until the dial is turned); it is build-gated with the clocks.

Where to go next
----------------

- :doc:`On-device verification <device-verify>`: how to read a capture, the incidents the device caught, and the full lost-completion record.
- :ref:`display-lost-dma`: the current design of the panel-write guards the takes exercise.
- :doc:`The UI Kconfig reference <../reference/kconfig-ui>`: every demo option and its default.
