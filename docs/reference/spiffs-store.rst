The SPIFFS store
================

.. brief:
   Reader: a developer or tinkerer who wants to read or prepare the files on the board's storage partition.
   They already know: what a flash partition is, basic cron syntax.
   After this page they can: write a valid credentials, alarm or settings file and know which code reads and writes each one.
   Page type: reference.

The 64 K ``storage`` partition (:doc:`the partition tables <partitions>`) is a SPIFFS file system with three files: ``wifi_credentials.txt``, ``alarm.txt`` and ``settings.txt``.
The build creates the partition from the source tree's ``spiffs/`` directory on every build.
The app owns the files at runtime and writes them back, so a full flash overwrites the runtime state; the settings screens re-create the files.

The store at a glance
---------------------

The first figure shows the partition, its three files and the code that reads them at boot.

.. graphviz:: ../_graphs/spiffs-store.dot
   :alt: The 64 K storage partition with its three files and the boot code that reads each one.
   :caption: One partition, three files, read at boot. The writers (the wheel keyboard and the settings screens) are in the third figure.
   :align: center

The second figure opens the files and shows what each one holds.

.. graphviz:: ../_graphs/spiffs-store-files.dot
   :alt: The three SPIFFS files and their formats: two-line credentials, five-field cron rules, key = value settings.
   :caption: ``wifi_credentials.txt`` is two bare lines (SSID, password). ``alarm.txt`` is five-field cron, one rule per line; a file with only comments means no alarms. ``settings.txt`` holds nine ``key = value`` lines: five range-validated values, two power-off kill switches and two legacy no-ops, each with a compile-time fallback (the tables below).
   :align: center

The third figure shows who writes each file, and when.

.. graphviz:: ../_graphs/spiffs-store-io.dot
   :alt: The SPIFFS writers: the wheel keyboard writes the credentials, the scheduler replaces the alarm file, settings_store_save rewrites the settings file.
   :caption: Each file has one runtime writer (blue) and its boot-time readers (amber). Read the arrows from writer to file. The knob's power ladder is the only reader that reads while running; every other read happens at boot. The scheduler replaces the whole alarm file, and ``settings_store_save`` rewrites the whole settings file behind its two-line header. Every write fires when the finger lifts (LVGL ``RELEASED``), never while dragging.
   :align: center

``wifi_credentials.txt``
------------------------

Two lines, no key syntax:

.. code-block:: text
   :caption: spiffs/wifi_credentials.example.txt (the template)

   # WiFi Credentials Example
   # Copy this file to wifi_credentials.txt and add your actual credentials
   # Format:
   # Line 1: WiFi SSID
   # Line 2: WiFi Password
   # Example:
   # MyHomeNetwork
   # MySecretPassword123

``wifi_credentials.txt`` is gitignored, so secrets never enter the tree; the example file is what the repository ships.
The Wi-Fi manager reads the real file at boot, and the settings screens write it back.
:doc:`The S3 board <../devices/s3>` describes the Wi-Fi path on each board.

``alarm.txt``
-------------

Cron-like, five fields:

.. code-block:: text
   :caption: spiffs/alarm.txt (the tracked default)

   # Alarm configuration file
   # Format: minute hour day_of_month month day_of_week
   # ...
   # Default: no alarms (a fresh flash must not arm an unasked-for ring)
   # no alarms

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Field
     - Allowed
     - Notes
   * - ``minute``
     - ``0-59`` or ``*/N``
     - ``*/N`` = every N minutes
   * - ``hour``
     - ``0-23``, ``*/N``, ``*``
     - ``*`` = every hour
   * - ``day_of_month``
     - ``1-31``, ``*``
     - ignored (treated as ``*``)
   * - ``month``
     - ``1-12``, ``*``
     - ignored (treated as ``*``)
   * - ``day_of_week``
     - ``0-6`` (0=Sunday), ``*``, ranges/lists
     - ``1-6`` = Mon–Sat, ``0,6`` = the weekend

The tracked default contains only comments (``# no alarms``), so a fresh flash never arms a ring nobody asked for.

The scheduler arms an alarm from these rules in a board-specific way:

- the S3 uses the PCF85063A hardware alarm;
- the P4 uses a software ``esp_timer`` one-shot;
- the knob uses the same rule and carries it across real deep sleep with the internal RTC timer wake (:doc:`the knob display <../implementation/knob-display>`).

A boot with zero rules must disarm any alarm still armed in the chip, or a stale time fires a phantom alarm (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).

``settings.txt``
----------------

The file is gitignored: the app owns it at runtime, and the tracked tree must not carry it.
Lines are ``key = value``; ``#`` comments and blank lines are skipped.
:cfunc:`settings_store_load` in :cfile:`components/clock/settings_store.c` parses the file and validates each key against its range.
A bad or missing value falls back to the Kconfig-seeded default, through an ``#ifndef``-seeded getter (the Kconfig-``n`` convention, :doc:`the build system <build-system>`).

.. list-table::
   :header-rows: 1
   :widths: 24 24 52

   * - Key
     - Valid range
     - Seed (the compile-time default)
   * - ``idle_timeout_sec``
     - 60–1800
     - ``CLOCK_IDLE_TIMEOUT_SEC`` (60)
   * - ``idle_timeout_enable``
     - ``0``/``1``
     - ``true`` (in-code default: the kill switch defaults to armed; the light-sleep screen's power-off toggle writes it)
   * - ``deep_sleep_countdown_sec``
     - 60–1800
     - ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (60)
   * - ``deep_sleep_countdown_enable``
     - ``0``/``1``
     - ``true`` (in-code default: the kill switch defaults to armed; the deep-sleep screen's power-off toggle writes it)
   * - ``boot_anim_enable``
     - ``0``/``1``
     - none: legacy no-op, not shown in any screen.
       Nothing reads it and no Kconfig key seeds it (an in-code fallback of 1 in :cfile:`components/clock/settings_store.c` stands in for the deleted ``UI_MOON_ANIM_ENABLE``).
       The store keeps it so the file round-trips unchanged.
   * - ``dusk_anim_enable``
     - ``0``/``1``
     - none: legacy no-op, not shown in any screen.
       Nothing reads it and no Kconfig key seeds it (an in-code fallback of 1 stands in for the deleted ``UI_MOON_DUSK_ENABLE``).
       The store keeps it so the file round-trips unchanged.
   * - ``timezone``
     - IANA zone name (≤39 chars)
     - ``CONFIG_CLOCK_DEFAULT_TIMEZONE`` (``Europe/Berlin``)
   * - ``alarm_sound_id``
     - 0..3
     - ``SETTINGS_ALARM_SOUND_DEFAULT`` (0, the two-tone)
   * - ``display_rotation_deg``
     - 0–359
     - ``SETTINGS_DISPLAY_ROT_DEFAULT`` (0)

These nine keys are the whole user layer:

- two timeout counters;
- two kill switches that turn each timer off entirely (the sleep screens' power-off toggles);
- the timezone, the alarm sound and the display rotation;
- the two legacy keys.

:ref:`The configuration ladder <config-ladder-perf>` draws the table as a dot matrix (writer, seed origin, validated range, still read), with the two legacy keys as the only open rows.
The file is storage at human pace.
:ref:`The write ladder <write-ladder-perf>` counts its writers: six settings-screen actions (four counters and the two kill switches) and the TEST settings demo.
Each call rewrites 329 B, and nothing writes the file on a schedule.

The timeouts
~~~~~~~~~~~~

The settings screens show the two timeouts in minutes (a 1–30 minute picker); the store keeps them in seconds (60–1800).

On the knob the two timeouts are the power ladder.
The idle-ladder supervisor reads both values and both kill switches (``idle_timeout_enable`` / ``deep_sleep_countdown_enable``) while running.
It dims after ``idle_timeout_sec`` and calls ``esp_deep_sleep_start`` one ``deep_sleep_countdown_sec`` later.
The knob's sleep-screen rollers (dial step, tap to commit) are therefore the real editors of its idle behaviour.

The timezone
~~~~~~~~~~~~

``timezone`` is the IANA name of one of the curated zones in :cfile:`components/clock/clock_tz.c`'s table.
Every entry carries a city label, its daylight-saving rule and the reference latitude and longitude that drive the moon's observer view (:doc:`the timezone screen <../ui/settings-tz>`, :doc:`observer view <../implementation/observer-view>`).

Applying a zone rewrites the POSIX ``TZ`` environment variable and calls ``tzset``.
That is all the P4's system clock needs.
The S3's PCF85063A stores local digits, so the clock re-derives the same moment into the new zone's digits and writes them back before the switch (the epoch stays consistent).
The resting moon frame is then re-gathered in place (``moon_ui_tz_changed``, no re-bake).

A file that still carries the legacy numeric ``timezone_idx`` key (generic 30-minute UTC offsets plus one Berlin entry) migrates once at load.
Index 53 becomes ``Europe/Berlin``; any generic offset becomes the Kconfig default.
The next save writes the name back.

The alarm sound and the display rotation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``alarm_sound_id`` indexes the alarm audio table (``ALARM_SOUND_COUNT`` = 4 in ``components/ui/alarm_audio.h``).
The store keeps its own copy of the range, so the clock component does not depend on the UI component.

``display_rotation_deg`` is the wall-mount base angle.
The Settings display screen's roller rotates the panel live and saves on ``RELEASED``.
``action_orientation_init`` seeds the value into the orientation pipeline.
The effective root rotation is the shared compile-time base ``UI_ORI_BASE_V10`` (one value, ``900`` = +90°, on all three panels) plus this trim, in tenths of a degree.
The clock therefore reads level however the board sits on the wall.
A live change re-anchors every root by the difference, so the current tilt carries over (:doc:`the rotate screen <../ui/settings-display>`).

How the file is saved
~~~~~~~~~~~~~~~~~~~~~

:cfunc:`settings_store_save` rewrites the whole file: a two-line header ("Runtime settings … do not edit by hand — a full flash resets this file") followed by the nine keys.
The file therefore always describes itself.

Where to go next
----------------

- :doc:`kconfig-clock` and :doc:`kconfig-ui`: the compile-time defaults that seed these keys.
- :doc:`the clock and sleep machine <../implementation/clock-sleep>`: how the timeouts and the alarm drive the sleep states.
