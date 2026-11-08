.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Ten keys across three menus (the sibling project's own components —
the clock's Kconfig tree is never involved):

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Key
     - Default
     - Meaning
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
   * - ``KNOB_NTP_SERVER``
     - ``pool.ntp.org``
     - The single lwIP POLL server (the clock's same choice); the
       credentials WLAN needs an uplink
   * - ``KNOB_TIME_BOOT_SYNC_MS``
     - ``20000`` (5000..120000)
     - How long the boot arm waits for the first plausible SNTP
       answer; after the window the glass stays ``no time`` and a
       late fix arms the static phase instead of a sweep
   * - ``KNOB_TIME_USE_TEST_EPOCH``
     - ``n``
     - Bench only: treat ``KNOB_MOON_TEST_EPOCH`` as trusted "now"
       and never start SNTP. Production leaves it ``n``: SNTP truth
       or ``no time``
   * - ``KNOB_DIM_AFTER_S``
     - ``15`` (0..3600)
     - Idle seconds before the backlight dims (0 = never)
   * - ``KNOB_DIM_DUTY``
     - ``40`` (1..254)
     - LEDC duty while dimmed (full = 255)
   * - ``KNOB_SLEEP_TIMEOUT_S``
     - ``60`` (0..36000)
     - Idle seconds before ``esp_deep_sleep_start`` with EXT0 on
       dial-A GPIO 8 (0 = never sleep); the opening act HOLDS the
       ladder
   * - ``KNOB_SLEEP_ALLOW``
     - ``y``
     - ``n`` = the sanctioned log-only bench mode: the would-be sleep
       and the EXT0 RTCIO verdict are logged and the device stays
       reachable over USB-JTAG. Production ships ``y``
