.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

Thirteen keys across three menus (the sibling project's own
components; the shared clock keys the P0 seam brings in are listed on
the page above):

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
   * - ``KNOB_DEMO_MODE``
     - ``n``
     - Bench demo tour (the knob half of
       :doc:`demo mode <../verification/demo-mode>`): after the boot
       presentation the producer walks 8 canonical phases of the
       current synodic month + a live-now step, holding each
       ``KNOB_DEMO_PHASE_SEC``; the power ladder stays held (no dim,
       no deep sleep), the caption names the phase, and every step
       logs a ``phase=`` marker
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
       European boards use ``CET-1CEST,M3.5.0/2,M10.5.0/3``; the
       shared ``clock_tz`` settings screen retires this key (parity
       phase P2)
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
       dial-A GPIO 8 (0 = never sleep); the boot arm and the opening
       act HOLD the ladder, and the shared ``clock``/``epoch``
       snapshot is saved first
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
