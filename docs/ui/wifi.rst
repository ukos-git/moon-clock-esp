The transient WiFi screen
=========================

.. brief:
   Reader: a firmware developer working on the network sync.
   They already know: C, LVGL basics, the airplane home screen.
   After this page they can: say what starts and ends this screen and where each step runs.
   Page type: reference.

The transient WiFi screen shows while the clock syncs its time over WiFi and NTP.
It looks like the airplane screen, plus a connection indicator below the clock, and it drops back to the airplane screen when the sync completes.

How it behaves
--------------

Three things start a sync and show this screen:

- the hourly gate, at rest on the home screen (``CLOCK_NTP_SYNC_INTERVAL_HOURS``, checked at the top of a minute by :cfunc:`action_update_clock_display`);
- the wake path, on the two clocks;
- the wheel keyboard's commit.

Leaving the settings hub starts no sync: it loads the home screen only.
A tap anywhere opens the settings hub, the same as on the airplane screen.

The knob reaches this screen through the hourly gate and the keyboard commit.
Its background SNTP join at boot (``boot_arm_task``) shows no screen.

Screenshots
-----------

.. figure:: ../img/screens/annot/wifi_s3.png
   :alt: The transient WiFi screen on the S3 panel, with numbered callouts on each element.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/wifi_p4.png
   :alt: The transient WiFi screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/wifi.png
   :alt: The transient WiFi screen on the knob's 360 px panel, without callouts.
   :align: center

   Knob, 360×360: the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``.
   The knob reaches it through the hourly gate or the keyboard commit, never through the background boot join.

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
     - The same moon image object as the airplane screen. The switch between the two is one atomic flush, never a moon re-bake.
     - :cfunc:`create_screen_wifi`

   * - 2
     - Clock HH:MM
     - The shared widget's parent container (``wifi_clock``). The widget itself is reparented here at the switch (layout contract 2).
     - :cfunc:`ui_clock_attach_to`

   * - 3
     - Seconds arc
     - Same ring as the airplane screen (outer r 223, 0–60 s).
     - :cfunc:`create_screen_wifi`

   * - 4
     - WiFi indicator
     - Below the clock at (185, 320). Grey means still connecting, white means connected. The tick recolours it when ``wifi_manager_is_connected()`` flips (a state compare, not a style set on every tick). It is not clickable, so a tap on it still opens the settings hub.
     - :cfunc:`tick_screen_wifi`, ``event_handler_cb_home_tap``; option ``CLOCK_NTP_TIMEOUT_MS`` (the sync's bound)

   * - 5
     - Tap anywhere
     - Opens the settings hub (same root handler as the airplane screen).
     - ``event_handler_cb_home_tap``

Implementation notes
--------------------

This screen and the airplane screen share the one moon image.
The switch between them is therefore the atomic whole-panel flush with no fade, the both-moon case in :doc:`the display pipeline <../implementation/display>`.

The three figures below draw the whole lifecycle from the code: the three starters, what runs off the UI task, and the one poll that drops the screen.

.. graphviz:: ../_graphs/wifi-sync.dot
   :alt: The sync lifecycle as sealed boxes: three starters, the sync kickoff, the shown screen, the sync task, the tick poll and the return to the airplane screen.
   :caption: The lifecycle in outline. There is no menu entry: the hourly gate, the wake path and the keyboard commit are the only starters, and one poll on the UI tick is the only exit.
   :align: center

The hourly gate fires only at rest: at minute 0, not idle, not in demo mode, not connected, SNTP due per ``CLOCK_NTP_SYNC_INTERVAL_HOURS``, and on the airplane screen.
That keeps a sleeping clock from waking for a 30 s radio fetch and keeps a settings session from being pulled away.
The wake path runs on the wake task itself, and the keyboard commits credentials first.
All three enter the one guarded :cfunc:`action_start_wifi_sync`.
It checks a re-entry guard, disables the idle ladder (so no sleep transition races the task's init), sets the panel dim to 100, holds display traffic before the radio starts, and starts the task (6144 B stack, priority 10).

.. graphviz:: ../_graphs/wifi-sync-start.dot
   :alt: The three starters (hourly gate at rest, wake path when the RTC is due, keyboard commit), all entering the guarded sync kickoff.
   :caption: The three starters opened. The hourly gate is drawn near line 940 of the actions file; all three paths meet at ``action_start_wifi_sync``.
   :align: center

The sync task first probes the heap (the leak watch), then initialises the WiFi manager, and runs SNTP only if it is due.
An NTP failure keeps the RTC (real-time clock) time and still returns to airplane mode.
Airplane mode runs inside the task because an associated station wakes the modem for DTIM beacons, and that receive traffic steals QSPI DMA completions on the S3 AMOLED.
The one exception is a deferred init that hits an in-flight scan: ``ESP_ERR_INVALID_STATE`` means the scan owns the radio.

The tick poll claims the done flag, with three exceptions:

- If the clock went to sleep after the handshake, the poll does not claim the flag; the next wake triggers it again.
- In demo mode the flag stays unclaimed for the demo's own SYNC phase.
- The return to airplane mode re-arms the idle ladder, unless the boot replay holds it (the P4's sync runs for about 30 s through the boot).

.. graphviz:: ../_graphs/wifi-sync-task.dot
   :alt: The sync task opened: SNTP with the RTC time kept on failure, airplane mode inside the task, the tick poll claiming the done flag, and the idle and demo deferrals.
   :caption: The sync task opened. Read top to bottom: init, the optional SNTP step, airplane mode, then the tick poll that decides whether to leave the screen.
   :align: center

Where to go next
----------------

- :doc:`The getting-started page <../getting-started>`: where a new user first meets this screen. A fresh board whose blocking boot sync failed drops to the airplane screen, and the hub, roller and keyboard chain then re-runs this sync through the keyboard commit.
- :doc:`The wheel keyboard <settings-wifi-password>`: the commit that starts one of the three syncs.
- :doc:`The display pipeline <../implementation/display>`: how the switch to and from this screen reaches the panel.
