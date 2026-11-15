The user interface
==================

One page per screen. The UI has sixteen screens — the five moon
screens (the clock plus the moon, in the product's sleep/wake states)
and the eleven settings screens (the hub and its ten sub-screens) —
enumerated by ``enum ScreensEnum`` in
:doc:`../code/index` (``components/ui/screens.h``). Each page carries
simulator screenshots for the two clock panels (S3 466×466 first, P4
720×720 second; the knob runs the same ``screens.c`` at 360 px under
its render seam and has no dedicated capture), annotated callouts
naming every pointing element, and a per-element table linking the
element to its code and its Kconfig / SPIFFS option.

The screens share one clock widget (the layout contract in
``screens.h``): the digits you see on every moon screen are the same
three objects, reparented on the screen switch.

Which screens each variant reaches
----------------------------------

The full ``ScreensEnum`` ships on all three boards — the matrix below
is **reachability**, computed from the code that loads screens: the
P4 reaches all **16/16**, the S3 **15/16**, the knob **12/16**. The
knob's four absentees are not missing code but flows the knob does
not run: it never shows the transient WiFi sync screen (its SNTP join
runs in the background), it dims its real backlight instead of a
light-sleep screen, it deep-sleeps for real (``esp_deep_sleep_start``,
panel off) instead of showing the app-dark screen, and the hub's
manual-time clock icon is compiled in only under ``CONFIG_BOARD_P4``.

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
     - Boot home on every board (``ui_init``); a tap opens the hub (gated on the clock not rolling).
   * - :doc:`wifi <wifi>` (transient sync)
     - ✓
     - ✓
     - —
     - Shown while a sync is in flight; the knob's Wi-Fi join exists only for SNTP and shows no screen.
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
     HOME -- "tap (not while rolling)" --> HUB
     HUB -- "centre exit -> calling level, starts manual sync (S3/P4)" --> HOME
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
     LS -- "exit to hub" --> HUB
     DS -- "exit to hub" --> HUB
     WS -- "exit to hub" --> HUB
     TZ -- "exit to hub" --> HUB
     SND -- "exit to hub" --> HUB
     TM -- "exit to hub" --> HUB
     HOME -- "hourly refresh / wake sync shows it, drops back on sync-done" --> W
     W -- "s_wifi_sync_done" --> HOME
   </pre>

The rules the graph encodes:

- **Sub-screen exits return to the CALLING level**, not to a fixed
  root (:cfunc:`settings_sub_screen_prepare`) — the alarm
  sub-screens return to the alarm-rules screen, everything else to
  the hub, the hub to the level that opened it (the home screen on
  the normal path; its exit also triggers the manual WiFi sync).
- **The deep-sleep icon is anchored at the bottom (180°) and found
  by target, not by index**, so the anchor cannot drift between the
  six- and seven-icon rings (:doc:`settings-hub <settings-hub>`).
- The transient WiFi screen is the only screen with no user entry —
  it appears when a sync goes in flight (wake, manual, hourly) and
  drops back to the airplane home on ``s_wifi_sync_done``.
- The alarm screen has no entry either — the alarm scheduler shows
  it; a touch dismisses it.

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
