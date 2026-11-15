The user interface
==================

One page per screen. The UI has sixteen screens — the five moon
screens (the clock plus the moon, in the product's sleep/wake states)
and the eleven settings screens (the hub and its ten sub-screens) —
enumerated by ``enum ScreensEnum`` in
:doc:`../code/index` (``components/ui/screens.h``). Each page carries
simulator screenshots for the three family panels — S3 466×466 first,
P4 720×720 second, and the knob at 360×360 for the thirteen screens it
reaches (its column shows the raw uisim panel: the same shared
``screens.c`` compiled under ``CONFIG_BOARD_KNOB``, including the P2
dial surface; callout enclosures are authored only for the two clock
housings, and the knob project's own overlays — battery gauge — live
in the sibling project, not in the sim set), annotated callouts
naming every pointing element on the two clock panels, and a
per-element table linking the element to its code and its Kconfig /
SPIFFS option.

The screens share one clock widget (the layout contract in
``screens.h``): the digits you see on every moon screen are the same
three objects, reparented on the screen switch.

Which screens each variant reaches
----------------------------------

The full ``ScreensEnum`` ships on all three boards — the matrix below
is **reachability**, computed from the code that loads screens: the
P4 reaches all **16/16**, the S3 **15/16**, the knob **13/16**. The
knob's three absentees are not missing code but flows the knob does
not run: it dims its real backlight instead of a light-sleep screen,
it deep-sleeps for real (``esp_deep_sleep_start``, panel off) instead
of showing the app-dark screen, and the hub's manual-time clock icon
is compiled in only under ``CONFIG_BOARD_P4``. It *does* reach the
transient WiFi screen — the knob registers
:cfunc:`action_update_clock_display`, so the hourly sync gate runs
there, and the wheel keyboard's commit starts a sync — only the wake
path's sync is clock-only.

.. list-table::
   :header-rows: 1
   :widths: 30 7 7 7 49

   * - Screen
     - S3
     - P4
     - Knob
     - Reached via / why absent
   * - :doc:`modem-sleep <modem-sleep>` (airplane home)
     - ✓
     - ✓
     - ✓
     - Boot home on every board (``ui_init``); a tap opens the hub (gated on ``!clock_is_idle()`` — fires only while awake; a wake tap lands on the sleep screen's root, which has no handler, so opening the hub is a deliberate second tap).
   * - :doc:`wifi <wifi>` (transient sync)
     - ✓
     - ✓
     - ✓
     - Shown while a sync is in flight (hourly gate, wake path on the clocks, keyboard commit). On the knob the hourly gate and the keyboard commit run; its background SNTP join shows nothing.
   * - :doc:`light-sleep <light-sleep>` (dim state)
     - ✓
     - ✓
     - —
     - The knob dims its real LCD backlight (LEDC, GPIO 47) instead of showing a screen.
   * - :doc:`deep-sleep <deep-sleep>` (app-dark state)
     - ✓
     - ✓
     - —
     - The knob runs a true ``esp_deep_sleep_start`` (panel off, RTC TIMER wake) instead.
   * - :doc:`alarm <alarm>`
     - ✓
     - ✓
     - ✓
     - ``action_alarm_triggered``; touch dismisses.
   * - :doc:`settings-hub <settings-hub>`
     - ✓
     - ✓
     - ✓
     - Home tap. Six ring icons at 60° apart on the S3 and knob, seven (~51.4°) on the P4.
   * - :doc:`settings-alarm <settings-alarm>`
     - ✓
     - ✓
     - ✓
     - Hub icon.
   * - :doc:`settings-pomodoro <settings-pomodoro>`
     - ✓
     - ✓
     - ✓
     - From the alarm-rules screen.
   * - :doc:`settings-alarm-time <settings-alarm-time>`
     - ✓
     - ✓
     - ✓
     - From the alarm-rules screen.
   * - :doc:`settings-light-sleep <settings-light-sleep>`
     - ✓
     - ✓
     - ✓
     - Hub icon; on the knob the same ``idle_timeout_sec`` key commits its own dim ladder.
   * - :doc:`settings-deep-sleep <settings-deep-sleep>`
     - ✓
     - ✓
     - ✓
     - Hub icon; on the knob the same countdown key arms its RTC TIMER deep sleep.
   * - :doc:`settings-wifi <settings-wifi>`
     - ✓
     - ✓
     - ✓
     - Hub icon (a background scan runs while open).
   * - :doc:`settings-wifi-password <settings-wifi-password>`
     - ✓
     - ✓
     - ✓
     - An AP row on the scan screen; its only way back is the centre long press.
   * - :doc:`settings-time <settings-time>`
     - —
     - ✓
     - —
     - The ring's clock icon is ``#if defined(CONFIG_BOARD_P4)`` — manual time entry is P4-only.
   * - :doc:`settings-tz <settings-tz>`
     - ✓
     - ✓
     - ✓
     - Hub globe icon.
   * - :doc:`settings-alarm-sound <settings-alarm-sound>`
     - ✓
     - ✓
     - ✓
     - Hub speaker icon. The knob has no codec — its ``esp_codec_dev`` shim is silent, so the preview plays nothing there.

Menu navigation
---------------

Every navigation is one ``loadScreen`` call and therefore one atomic
whole-panel flush (:doc:`the display pipeline <../implementation/display>`). The
graph is the shared ``screens.c`` — identical on all three boards;
the knob adds only its dial cursor on top
(:ref:`dial-semantics-knob <dial-semantics-knob>`).

.. raw:: html

   <pre class="mermaid">
   flowchart TB
     HOME["Airplane home (modem-sleep) — static moon + clock"]
     HUB["Settings hub — ring of icons, centre = exit"]
     ALR["Alarm rules"]
     POM["Pomodoro"]
     AT["Alarm time"]
     LS["Light-sleep picker"]
     DS["Deep-sleep picker"]
     WS["WiFi scan list"]
     PW["Keyboard (password)"]
     TZ["Timezone roller"]
     SND["Alarm sound picker"]
     TM["Manual time (P4 only)"]
     W["Transient WiFi sync"]
     A["Alarm screen (touch dismisses)"]
     HOME -- "tap (awake only — !clock_is_idle())" --> HUB
     HUB -- "centre exit -> calling level (no sync is kicked off)" --> HOME
     HUB --> ALR
     HUB --> LS
     HUB --> DS
     HUB --> WS
     HUB --> TZ
     HUB --> SND
     HUB -. "clock icon, P4 only" .-> TM
     ALR --> POM
     ALR --> AT
     POM -- "exit returns to CALLING level" --> ALR
     AT -- "exit returns to CALLING level" --> ALR
     WS -- "tap an AP row" --> PW
     PW -- "centre long press" --> WS
     PW -- "commit: write credentials, start sync" --> W
     LS -- "exit to hub" --> HUB
     DS -- "exit to hub" --> HUB
     WS -- "exit to hub" --> HUB
     TZ -- "exit to hub" --> HUB
     SND -- "exit to hub" --> HUB
     TM -- "exit to hub" --> HUB
     HOME -- "hourly sync gate / wake path (clocks) / keyboard commit shows it, drops back on sync-done" --> W
     W -- "s_wifi_sync_done" --> HOME
   </pre>

The rules the graph encodes:

- **Sub-screen exits return to the CALLING level**, not to a fixed
  root (:cfunc:`settings_sub_screen_prepare`) — the alarm
  sub-screens return to the alarm-rules screen, everything else to
  the hub, the hub to the level that opened it (the home screen on
  the normal path). Leaving the hub loads the home screen only —
  no sync is kicked off on exit; the sync triggers are the hourly
  gate, the wake path (clocks) and the keyboard commit.
- **The deep-sleep icon is anchored at the bottom (180°) and found
  by target, not by index**, so the anchor cannot drift between the
  six- and seven-icon rings (:doc:`settings-hub <settings-hub>`).
- The transient WiFi screen has no menu entry — it appears when a
  sync goes in flight (the hourly gate at rest on the home screen,
  the wake path on the clocks, the keyboard commit) and drops back
  to the airplane home on ``s_wifi_sync_done``.
- The alarm screen has no entry either — the alarm scheduler shows
  it; a touch dismisses it.

Menu geometry, computed per variant
-----------------------------------

Two settings screens carry real *geometry* beyond the ring: the wheel
keyboard and the alarm-time day band. Like the hub ring
(:doc:`settings-hub <settings-hub>`), their layout is not hand-placed
figures but pure ``UI_PX()`` arithmetic in
:doc:`../code/index` (``components/ui/screens.c``) — so the figure
below **computes** every element position per family panel the way
the device does (the same integer division, the code's own radii and
angle expressions: the wheel's WHEEL_* defines and design comment,
the band's ALARM_TIME_* defines and ``ALARM_DAY_ANG(d) = 181 + 30d``;
LVGL angles, 0 = right, 90 = down). The generator is
``docs/tools/gen_menu_geom.py``; the hub-ring companion is
``gen_hub_ring.py``, the dial-disk one ``gen_disk_geom.py``.

.. figure:: ../_static/figs/menu_geom.png
   :alt: wheel keyboard and alarm-time band geometry, computed per panel
   :figclass: align-center

   Top row: the :doc:`wheel keyboard <settings-wifi-password>` —
   press band r 140..panel/2, at-rest letters r 168, reveal row r 209,
   sector hairlines r 150..226, register track LVGL 150..390 at r
   84..144, commit/discard wedges at r 114, centre box r ~55. Bottom
   row: the :doc:`alarm-time day band <settings-alarm-time>` — outer
   r 223, width 10 (the home seconds-arc ring), seven day handles on
   the r 218 midline, the dark track and indicator running 181..1
   through the 360/0 wrap (sample shown: Mon–Fri). The S3 column
   carries the 1:1 numbers; P4 and knob scale every radius through
   ``UI_PX``.

.. toctree::
   :maxdepth: 2

   modem-sleep
   wifi
   light-sleep
   deep-sleep
   alarm
   settings-hub
   settings-alarm
   settings-pomodoro
   settings-alarm-time
   settings-light-sleep
   settings-deep-sleep
   settings-wifi
   settings-wifi-password
   settings-time
   settings-tz
   settings-alarm-sound
