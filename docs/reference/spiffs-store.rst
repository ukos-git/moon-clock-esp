The SPIFFS store
================

The 64 K ``storage`` partition (:doc:`the partition tables
<partitions>`) is SPIFFS, baked from the source tree's ``spiffs/``
directory on **every** build and *runtime-owned*: the app writes
``settings.txt`` and ``alarm.txt`` back to it, so a full flash
clobbers the runtime state (accepted — the settings screens re-create
them). Three files live on it.

.. graphviz:: ../_graphs/spiffs-store.dot

Each file has exactly one runtime writer (blue) and boot-time readers
(amber); the knob's power ladder is the only *live* reader — every
other read happens at boot.

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
   # Default: weekday alarm
   0 7 * * 1-5

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

The default is the weekday alarm (``0 7 * * 1-5`` — 7:00 Mon–Fri); the
``*/5`` debug rule was burned into the alarm screen historically. The
scheduler arms the PCF85063A hardware alarm from this rule on the S3
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
   * - ``deep_sleep_countdown_sec``
     - 60–1800
     - ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (60)
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
   * - ``timezone_idx``
     - 0..53
     - ``CLOCK_TZ_BERLIN_IDX`` (53)
   * - ``alarm_sound_id``
     - 0..3
     - ``SETTINGS_ALARM_SOUND_DEFAULT`` (0, the two-tone)

These six keys are the user's whole layer — the configuration ladder
draws the table as a dot matrix (writer, seed origin, validated
range, still-read) with the two ghosts as the only open rows
(:ref:`the configuration ladder <config-ladder-perf>`).

The two timeouts are user-facing in **minutes** on the settings
screens (a 1–30 minute picker) and stored internally in seconds
(60–1800). On the **knob** they are the power ladder: since P2 the
idle-ladder supervisor reads this pair live — dim after
``idle_timeout_sec``, ``esp_deep_sleep_start`` one
``deep_sleep_countdown_sec`` later — so the knob's sleep-screen
rollers (dial-step + tap-commit) are the real editors of its idle
behaviour. ``timezone_idx`` indexes the runtime timezone option list:
indices 0–52 are the fixed 30-minute UTC offsets (UTC−12:00 …
UTC+14:00) and index 53 is Europe/Berlin (DST-aware, the Kconfig
default) — :cfile:`components/clock/clock_tz.c` rewrites the POSIX TZ
env var (+``tzset``) when an option is applied, which is all the P4's
system clock needs; the S3's PCF85063A stores *local* digits, so the
same moment is re-derived into the new zone's digits and written back
before the switch (epoch-consistent). ``alarm_sound_id`` indexes the
alarm audio table (``ALARM_SOUND_COUNT`` = 4 in
``components/ui/alarm_audio.h``); the store keeps its own copy of
the range so the clock component does not depend on ui.

The save path (:cfunc:`settings_store_save`) rewrites the whole file
with a two-line header ("Runtime settings … do not edit by hand — a
full flash resets this file") plus the six keys, so the file is always
self-describing.
