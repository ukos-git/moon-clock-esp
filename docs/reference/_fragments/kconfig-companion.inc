.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

.. list-table::
   :header-rows: 1
   :widths: 30 14 56

   * - Key
     - Default
     - Meaning
   * - ``COMPANION_ENABLE``
     - ``y`` (defaults file; ``n`` in Kconfig)
     - The companion knob UDP input: starts the listener + mDNS
       advertisement (``_moonclock._udp.local``) that accepts the
       knob's fixed 10-byte binary frames. The listener task
       (core 0) only queues; the UI tick drains the queue and calls
       the injection API, so every effect lands in the LVGL context
       like a touch event. Frames are gated against the current
       screen — the clock is authoritative
       (:doc:`companion knob input <../implementation/companion-knob>`).
   * - ``COMPANION_TOKEN``
     - ``0``
     - Shared token byte, 0–255 (0 = accept any frame). LAN-trusted
       posture: it keeps a curious neighbour off the dial, it is not
       a secret. Both sides must set it (knob side:
       ``knob/sdkconfig.defaults``).
   * - ``COMPANION_PORT``
     - ``50188``
     - UDP listen port, 1024–65535.
   * - ``COMPANION_KEEP_WIFI``
     - ``y`` P4 / ``n`` S3
     - Keep the Wi-Fi association alive through the airplane /
       light-sleep hand-offs so the listener stays reachable between
       syncs (the clock is airplane-first by design). Costs radio
       power: right for the wall-powered P4 4C, wrong for the battery
       S3. Deep sleep always deinits the radio — the companion is
       useless while the clock is hidden.
