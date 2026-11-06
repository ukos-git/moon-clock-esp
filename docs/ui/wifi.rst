The transient WiFi screen
=========================

Shown while a WiFi + NTP sync runs (boot, the hourly ``CLOCK_NTP_SYNC_INTERVAL_HOURS``, or the manual sync from the settings hub's exit button) and dropped back to the airplane screen when the sync completes. Same structure as the airplane screen — the moon, the clock, the seconds arc — with a connection indicator below the clock. The two screens share the ONE moon image, so the switch between them is the atomic whole-panel flush (the both-moon exception in :doc:`the display pipeline <../implementation/display>`).

.. figure:: ../img/screens/annot/wifi_s3.png
   :alt: the transient wifi screen — the S3 panel (466, annotated)
   :align: center

   The transient WiFi screen — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/wifi_p4.png
   :alt: the transient wifi screen — the P4 panel (720, annotated)
   :align: center

   The transient WiFi screen — P4 720×720 (the same UI_PX layout, scaled)

Elements
--------

.. list-table::
   :header-rows: 1
   :widths: 4 24 44 28

   * - #
     - Element
     - Purpose
     - Code / option

   * - 1
     - Moon (shared image)
     - The same moon image object as the airplane screen — the switch between the two is one atomic flush, never a moon re-bake.
     - :cfunc:`create_screen_wifi`

   * - 2
     - Clock HH:MM
     - The shared widget's PARENT container (``wifi_clock``) — the widget itself is reparented here at the switch (layout contract 2).
     - :cfunc:`ui_clock_attach_to`

   * - 3
     - Seconds arc
     - Same ring as the airplane screen (outer r 223, 0–60 s).
     - :cfunc:`create_screen_wifi`

   * - 4
     - WiFi indicator
     - Below the clock at (185, 320): grey = still connecting, white = connected. The tick recolours it the moment ``wifi_manager_is_connected()`` flips (a state compare, not a per-tick style set). Non-clickable: a tap on it still opens the settings hub.
     - :cfunc:`tick_screen_wifi`, ``event_handler_cb_home_tap`` — option ``CLOCK_NTP_TIMEOUT_MS`` (the sync's bound)

   * - 5
     - Tap anywhere
     - Opens the settings hub (same root handler as the airplane screen).
     - ``event_handler_cb_home_tap``
