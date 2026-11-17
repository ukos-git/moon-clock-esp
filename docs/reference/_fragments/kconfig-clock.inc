.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Eight keys:

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Key
     - Default
     - Meaning
   * - ``CLOCK_USE_PCF85063A``
     - ``y``
     - Use the external PCF85063A RTC (with the TCA9554 IO expander)
       as the time base. The S3 board carries it; the P4 4C does not
       — there the time base is the 32.768 kHz crystal-backed internal
       RTC timer (``RTC_CLK_SRC_EXT_CRYS``) + SNTP, minute ticks are
       detected in software, and the countdown / alarm arming run on
       ``esp_timer`` instead of the PCF85063A hardware registers. When
       disabled, ``clock_set_alarm`` / ``clock_enable_alarm`` /
       ``clock_clear_alarm`` become no-ops (the scheduler's
       ``s_next_alarm`` cache is the source of truth for the software
       alarm). The P4 defaults file sets this to ``n``
       (:doc:`the P4 board <../devices/p4>`).
   * - ``CLOCK_TIME_FORMAT`` (choice)
     - ``24H``
     - Time display format: ``CLOCK_TIME_FORMAT_24H`` (00:00–23:59) or
       ``CLOCK_TIME_FORMAT_12H`` (12-hour with AM/PM). The RTC always
       stores 24-hour time internally; the choice only affects display.
   * - ``CLOCK_DEFAULT_TIMEZONE``
     - ``Europe/Berlin``
     - Default timezone as the IANA name of a zone in the
       ``clock_tz`` table (the compile-time fallback applied at boot
       until the user picks one; the *runtime* selection is the
       settings store's ``timezone`` key —
       :doc:`the SPIFFS store <../reference/spiffs-store>`).
   * - ``CLOCK_NTP_SERVER``
     - ``pool.ntp.org``
     - Primary NTP server to synchronize with.
   * - ``CLOCK_NTP_SYNC_INTERVAL_HOURS``
     - ``1``
     - Interval between NTP synchronizations, 1–168 hours. The device
       activates Wi-Fi on demand (wake + the hourly tick while at rest)
       purely to refresh the RTC, then drops back to airplane mode.
   * - ``CLOCK_NTP_TIMEOUT_MS``
     - ``10000``
     - Maximum time to wait for NTP synchronization, 3000–30000 ms.
   * - ``CLOCK_IDLE_TIMEOUT_SEC``
     - ``60``
     - Seconds of no user interaction before switching to the deep
       sleep screen, 60–1800. The timer resets on any touch/display
       interaction.
   * - ``CLOCK_IDLE_CHECK_INTERVAL_MS``
     - ``1000``
     - How often the idle timeout is checked, 500–5000 ms. Lower values
       give more precise timing at more CPU cost.

Two of these interact with the sleep machine: the idle timeout feeds
the light-sleep entry, and ``CLOCK_USE_PCF85063A`` decides whether the
deep-sleep countdown is armed by the PCF's hardware registers or by a
software ``esp_timer`` one-shot — the whole difference in alarm
capability between the two boards
(:doc:`the clock and sleep machine <../implementation/clock-sleep>`).
