The transient WiFi screen
=========================

Shown while a WiFi + NTP sync runs — the hourly gate at rest on the home screen (``CLOCK_NTP_SYNC_INTERVAL_HOURS``, checked at the top of a minute by :cfunc:`action_update_clock_display`), the wake path on the two clocks, or the wheel keyboard's commit — and dropped back to the airplane screen when the sync completes. Leaving the settings hub starts no sync (it loads the home screen only). Same structure as the airplane screen — the moon, the clock, the seconds arc — with a connection indicator below the clock. The two screens share the ONE moon image, so the switch between them is the atomic whole-panel flush (the both-moon exception in :doc:`the display pipeline <../implementation/display>`). The knob reaches this screen through the hourly gate and the keyboard commit; its background boot SNTP join (``boot_arm_task``) shows no screen.

.. figure:: ../img/screens/annot/wifi_s3.png
   :alt: the transient wifi screen — the S3 panel (466, annotated)
   :align: center

   The transient WiFi screen — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/wifi_p4.png
   :alt: the transient wifi screen — the P4 panel (720, annotated)
   :align: center

   The transient WiFi screen — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/wifi.png
   :alt: the transient wifi screen — the knob panel (360)
   :align: center

   The transient WiFi screen — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; reached here by the hourly gate or the keyboard commit, never by the background boot join)

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

The whole lifecycle — the three starters, what runs off the UI task,
and the one poll that drops the screen — drawn from the code:

.. graphviz:: ../_graphs/wifi-sync.dot

Where the screen first meets a new user — the fresh board whose
blocking boot sync failed, the drop to the airplane screen, and the
hub → roller → keyboard chain whose circle-check commit re-runs this
very sync (one of the three starters above) — is drawn as the whole
beginner's journey on
:doc:`the getting-started page <../getting-started>`.
