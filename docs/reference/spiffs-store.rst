The SPIFFS store
================

The 64 K ``storage`` partition (:doc:`the partition tables
<partitions>`) is SPIFFS, baked from the source tree's ``spiffs/``
directory on **every** build and *runtime-owned*: the app writes
``settings.txt`` and ``alarm.txt`` back to it, so a full flash
clobbers the runtime state (accepted — the settings screens re-create
them). Three files live on it.

The store as a sealed box — the 64 K partition and its three files with
their boot readers:

.. graphviz:: ../_graphs/spiffs-store.dot
   :alt: SPIFFS overview — the 64 K storage partition, three files, their boot readers
   :caption: One partition, three files, boot reads only (the wheel-keyboard and settings writers are the box's other side, drawn next). The partition is baked from ``spiffs/`` on every build but *runtime-owned* — a full flash clobbers the runtime state and the settings screens re-create the files.
   :align: center

The formats, opened — what each file actually holds:

.. graphviz:: ../_graphs/spiffs-store-files.dot
   :alt: The three SPIFFS files with their formats — two-line credentials, five-field cron, key equals value
   :caption: ``wifi_credentials.txt`` is two bare lines (SSID, password — the tracked example is what ships, so secrets never enter the tree); ``alarm.txt`` is five-field cron, one rule per line, comments-only meaning no alarms; ``settings.txt`` is ``key = value`` lines with nine keys — five range-validated, two power-off kill switches, and the two legacy no-ops — each falling back to a compile-time default (the tables below).
   :align: center

The writers, opened — who commits what, and when:

.. graphviz:: ../_graphs/spiffs-store-io.dot
   :alt: The SPIFFS writers — wheel keyboard to credentials, scheduler full replace to alarm, settings_store_save whole-file rewrite to settings
   :caption: Each file has exactly one runtime writer (blue) and boot-time readers (amber); the knob's power ladder is the only *live* reader — every other read happens at boot. The scheduler's write is a FULL replace, ``settings_store_save`` rewrites the whole file behind its two-line header, and every commit fires on RELEASED, never on the drag path.
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

``wifi_credentials.txt`` is **gitignored** (secrets never enter the
tree) — the example file is what ships; the WiFi manager reads the
real file at boot and the settings screens write it back
(:doc:`the S3 board <../devices/s3>` for the Wi-Fi path on each board).

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
     - currently ignored (treated as ``*``)
   * - ``month``
     - ``1-12``, ``*``
     - currently ignored (treated as ``*``)
   * - ``day_of_week``
     - ``0-6`` (0=Sunday), ``*``, ranges/lists
     - ``1-6`` = Mon–Sat, ``0,6`` = the weekend

The default is comments-only (``# no alarms`` — 2026-11-19, a fresh
flash must not arm an unasked-for ring; the old ``0 7 * * 1-5``
weekday default rang with "the alarm disabled" in the settings, and
the ``*/5`` debug rule was burned into the alarm screen historically).
The scheduler arms the PCF85063A hardware alarm from this rule on the S3
and the software ``esp_timer`` one-shot on the P4 (and on the knob,
which extends the same rule across real deep sleep with the internal
RTC **TIMER** wake
(:doc:`the knob display <../implementation/knob-display>`); a **zero-rule**
boot must disarm any stale armed ALM or a stale time fires a phantom
alarm (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).

``settings.txt``
----------------

**Gitignored** (the app owns it at runtime; the tracked tree must not
carry it). Lines are ``key = value`` (``#`` comments, blank lines
skipped), parsed by :cfunc:`settings_store_load` in :cfile:`components/clock/settings_store.c` — each key validated by range,
a bad/missing value falling back to the Kconfig-seeded default
(``#ifndef``-seeded getter, the Kconfig-``n`` convention
:doc:`the build system <build-system>`).

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
     - ``true`` (in-code default — the kill switch defaults to
       armed; the light-sleep screen's power-off toggle writes it)
   * - ``deep_sleep_countdown_sec``
     - 60–1800
     - ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (60)
   * - ``deep_sleep_countdown_enable``
     - ``0``/``1``
     - ``true`` (in-code default — the kill switch defaults to
       armed; the deep-sleep screen's power-off toggle writes it)
   * - ``boot_anim_enable``
     - ``0``/``1``
     - none — **legacy no-op, unexposed**: nothing has read it since
       the boot sweep retired 2026-11-13 and its
       ``UI_MOON_ANIM_ENABLE`` seed was deleted in the same config
       sweep (kept so the store round-trips unchanged)
   * - ``dusk_anim_enable``
     - ``0``/``1``
     - none — **legacy no-op, unexposed**: nothing has read it since
       the dusk retired 2026-11-13 and its ``UI_MOON_DUSK_ENABLE``
       seed was deleted in the same config sweep (kept so the store
       round-trips unchanged)
   * - ``timezone``
     - IANA zone name (≤39 chars)
     - ``CONFIG_CLOCK_DEFAULT_TIMEZONE`` (``Europe/Berlin``)
   * - ``alarm_sound_id``
     - 0..3
     - ``SETTINGS_ALARM_SOUND_DEFAULT`` (0, the two-tone)
   * - ``display_rotation_deg``
     - 0–359
     - ``SETTINGS_DISPLAY_ROT_DEFAULT`` (0)

These nine keys are the user's whole layer — the two timeout
counters, the two kill switches that switch each timer off entirely
(power-off, the sleep screens' toggles), the timezone, the alarm
sound, the display rotation, and the two legacy ghosts. The
configuration ladder draws the table as a dot matrix (writer, seed
origin, validated range, still-read) with the two ghosts as the only
open rows (:ref:`the configuration ladder <config-ladder-perf>`).
The file is *human-paced* storage: the write ladder counts its
writers — six settings-screen actions (four counters + the two kill
switches) and the TEST settings demo, 329 B rewritten per call, no
scheduled leg at all
(:ref:`the write ladder <write-ladder-perf>`).

The two timeouts are user-facing in **minutes** on the settings
screens (a 1–30 minute picker) and stored internally in seconds
(60–1800). On the **knob** they are the power ladder: since P2 the
idle-ladder supervisor reads the pair and the two kill switches
(``idle_timeout_enable`` / ``deep_sleep_countdown_enable``) live — dim after
``idle_timeout_sec``, ``esp_deep_sleep_start`` one
``deep_sleep_countdown_sec`` later — so the knob's sleep-screen
rollers (dial-step + tap-commit) are the real editors of its idle
behaviour. ``timezone`` is the IANA *name* of one of the curated
zones in :cfile:`components/clock/clock_tz.c`'s table — every entry
carries a city label, its DST rule and the reference lat/lon that
drives the moon's observer view
(:doc:`the timezone screen <../ui/settings-tz>`,
:doc:`observer view <../implementation/observer-view>`). Applying a
zone rewrites the POSIX TZ env var (+``tzset``), which is all the
P4's system clock needs; the S3's PCF85063A stores *local* digits, so
the same moment is re-derived into the new zone's digits and written
back before the switch (epoch-consistent), and the resting moon frame
is re-gathered in place (``moon_ui_tz_changed``, no re-bake). A file
still carrying the retired numeric ``timezone_idx`` (the pre-2026-11-17
option list: generic 30-minute UTC offsets plus one Berlin entry)
migrates once at load — 53 → ``Europe/Berlin``, any generic offset →
the Kconfig default — and the name is written back on the next save.
``alarm_sound_id`` indexes the
alarm audio table (``ALARM_SOUND_COUNT`` = 4 in
``components/ui/alarm_audio.h``); the store keeps its own copy of
the range so the clock component does not depend on ui.
``display_rotation_deg`` is the wall-mount base angle: the Settings
display screen's roller scrolls the panel live and persists on
RELEASED; ``action_orientation_init`` seeds it into the orientation
pipeline, where the effective root rotation is the shared compile
base ``UI_ORI_BASE_V10`` (single ``900`` = +90° on all three panels)
*plus* this trim in deci-degrees — so the
clock reads level however the board sits on the wall, and the live
commit re-anchors every root by the delta so the current tilt rides
along (:doc:`the rotate screen <../ui/settings-display>`).

The save path (:cfunc:`settings_store_save`) rewrites the whole file
with a two-line header ("Runtime settings … do not edit by hand — a
full flash resets this file") plus the nine keys, so the file is always
self-describing.
