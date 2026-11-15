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

.. _input-chain-ui:

How the finger and the dial reach the widget
--------------------------------------------

The navigation graph above says *where* a tap lands; this answers
*how it gets there*. Two families, opposite ownership: on the clocks
the **BSP** creates the one real ``LV_INDEV_TYPE_POINTER`` device
(CST9217 on the S3, GT911 on the P4) and three guarded listeners
co-locate on that single indev — the idle/touch door
(:c:func:`indev_read_cb` in ``clock_idle.c``, the wake side of which
is :doc:`the wake-doors sketch <../implementation/clock-sleep>`), the
2 s orientation press (:c:func:`action_register_orientation_touch` in
``actions.c``, pipeline in ``tilt-pipeline.dot``) and the alarm touch
dismiss (:c:func:`action_register_alarm_touch`) — while on the knob
**the app owns both read paths itself**: a polled CST816S and the
EC1 dial as two FreeRTOS tasks. One read clock drives every pointer:
LVGL's indev read timer at ``LV_DEF_REFR_PERIOD`` = **33 ms** on all
three boards (``managed_components/lvgl__lvgl/src/indev/lv_indev.c:105``);
the S3's CST9217 has a real INT line (GPIO 11) so the adapter runs in
IRQ mode — but the ISR only signals a semaphore, reads still ride the
33 ms timer — and the P4's GT911 has INT = NC, pure polling (pins in
the wiring sketches). Because all the listeners run in the LVGL task
(indev events land in ``lv_timer_handler``), none of them needs
atomics. The one exception to "the finger talks to LVGL" is the demo:
its **second** POINTER indev injects presses through the real
hit-test while the BSP indev stays live for the human — created
*after* the three registrations so it never steals their target
(:doc:`demo mode <../verification/demo-mode>`).

.. graphviz:: ../_graphs/input-chain.dot

The rules the graph encodes:

- **One indev, three guards on the clocks.** Every
  ``lv_indev_add_event_cb`` binds through ``lv_indev_get_next(NULL)``
  to the BSP's indev; what separates the listeners is their guard,
  not their event: the idle listener wakes only from IDLE (a tap
  while awake only resets the timer), the press listener acts only
  after ``ORI_RESYNC_PRESS_MS`` 2000 *and* on a stable on-axis pose,
  the dismiss listener acts only while ``g_alarm_active &&
  !clock_is_idle()``.
- **The dial is a pulse pair, not a quadrature wheel.** A = GPIO 8,
  B = GPIO 7: every detent pulses exactly one channel and which
  channel pulsed *is* the direction; the vendor ``process_knob_channel``
  shape (LOW re-arms, the debounced RISE counts) lives in
  ``knob_board_encoder.c``. An A-edge quadrature read is the
  documented "the dial only moves one way" bug family.
- **Home dial = brightness, never input.** On the modem-sleep screen
  a detent turns the backlight ±5 % floored at the ladder's own dim
  rung (``CONFIG_KNOB_DIM_DUTY`` 64 → 25 %), is *not* ladder activity,
  and only brightening from a dark panel counts as wake intent (user
  decision 2026-11-10). Off home the same detent steps the edit
  cursor under the LVGL lock — and activity is reported **before**
  the lock so a contended dial never delays the un-dim.
- **The dial task and the indev path meet at the reentrant lock.**
  ``knob_ui_lvgl_lock`` (knob_ui.c) is a holder+depth wrapper: a
  same-task take is a depth bump, because a FreeRTOS mutex is not
  recursively takable and the first tap used to self-deadlock when
  its CLICKED handler re-took the lock through ``loadScreen`` → seam
  (bench 2026-11-10; the dial semantics continue in
  :ref:`dial-semantics-knob <dial-semantics-knob>`).
- **Tap-to-commit is the no-target fallback.** A background press
  with no clickable widget under it resolves to the screen root,
  where ``screens_dial_commit`` listens; the settings creators clear
  root CLICKABLE, so ``knob_dial_start`` restores it knob-side only —
  LVGL 9 needs CLICKABLE to make a press a *target* at all. The moon
  roots stay out: their tap opens the hub (``screens.c``
  :c:func:`event_handler_cb_home_tap`, awake-only — the wake tap
  never opens the hub).

.. _menu-geometry:

Menu geometry, computed per variant
-----------------------------------

The menu surfaces that carry real *geometry* beyond the ring are the
wheel keyboard, the alarm-rules triangle, the alarm-time day band and
the six roller screens (settings-time, pomodoro, the single-roller
family, and the Wi-Fi scan list). Like the hub
ring (:doc:`settings-hub <settings-hub>`), their layout is not
hand-placed figures but pure ``UI_PX()`` arithmetic in
:doc:`../code/index` (``components/ui/screens.c``) — so the figures
below **compute** every element position per family panel the way the
device does (the same integer division, the code's own radii, box
sizes and ``lv_obj_align`` offsets: the wheel's WHEEL_* defines and
design comment, the alarm-rules screen's ``UI_PX(30)``/``UI_PX(130)``
align offsets and its 96/148/74 FA icon tiers, the band's
ALARM_TIME_* defines and ``ALARM_DAY_ANG(d) = 181 + 30d``, the
rollers' create-call arguments; LVGL angles, 0 = right, 90 = down).
The generators are ``docs/tools/gen_menu_geom.py`` and
``gen_roller_screens.py``; the hub-ring companion is
``gen_hub_ring.py``, the dial-disk one ``gen_disk_geom.py``, the
home-family one ``gen_home_screens.py``.

.. figure:: ../_static/figs/menu_geom.png
   :alt: wheel keyboard, alarm-rules triangle and alarm-time band geometry, computed per panel
   :figclass: align-center

   Top row: the :doc:`wheel keyboard <settings-wifi-password>` —
   press band r 140..panel/2, at-rest letters r 168, reveal row r 209,
   sector hairlines r 150..226, register track LVGL 150..390 at r
   84..144, commit/discard wedges at r 114, centre box r ~55. Middle
   row: the :doc:`alarm-rules screen <settings-alarm>` — the master
   deactivate-all alarm glyph at (0, −30), the two diagonal entries at
   (±130, +130) (left solid stopwatch opens pomodoro, right regular
   alarm-clock opens alarm-time); the disks are the tier's FA icon
   face (96/148/74 px, the same ink gen_hub_ring.py draws). Bottom
   row: the :doc:`alarm-time day band <settings-alarm-time>` — a pad-0
   band whose outer ink edge is the panel half itself (the width-10
   stroke inked INWARD, 224..233 at 466 — it touches the round rim,
   unlike the pad-inset home seconds arc at outer 223), the seven
   34 px day handles centred at r 218, the dark track and indicator running
   181..1 through the 360/0 wrap (sample shown: Mon–Fri), and the
   screen's own content: the two 140×110 time rollers at the home
   clock's glyph centres (−80/+70, −23), the "Mo–Fr" range summary at
   y −100, and the shared sub-screen exit button (the ``UI_PX(90)``
   call ``settings_create_exit_button`` floors at ``UI_PX(96)``,
   BOTTOM_MID −34 — top edge y 336, the x 185..281 / y 336..432 free
   band of the code comment; dashed). The S3 column carries the
   1:1 numbers; P4 and knob scale every radius through ``UI_PX``.

.. figure:: ../_static/figs/roller_screens.png
   :alt: settings-time, pomodoro, the single-roller family and the Wi-Fi scan list geometry, computed per panel
   :figclass: align-center

   The roller family — the settings rollers are one visible row (the
   roller *is* the value display); the Wi-Fi roller is the exception.
   Top: :doc:`settings-time <settings-time>` — the
   time row (two 140×110 boxes, 96 px font) at ±75/−30 with the `:` at
   centre, the date row (three 60×70 boxes, 48 px font) at ±75/0 with
   the dots at ±37, caption at y −110. Second:
   :doc:`pomodoro <settings-pomodoro>` — the twin rollers at the home
   clock's glyph centres (−80/+70, −23, same pattern as the alarm-time
   band's screen above), caption at y −100. Third: the
   single-roller family (:doc:`light-sleep <settings-light-sleep>` and
   :doc:`deep-sleep <settings-deep-sleep>` at 210, :doc:`timezone
   <settings-tz>` at 380, :doc:`alarm sound <settings-alarm-sound>` at
   260 plus the 100×100 preview button at +150) — all three shown
   true-centred at once for width comparison; each device screen shows
   only its own roller. Bottom: the :doc:`Wi-Fi scan list
   <settings-wifi>` — the family's ONE NORMAL-mode roller (the user's
   2026-11-10 "make the network selection a lv_roller", 2026-11-11
   family-wide): 430 wide (knob 300) at (0, +18), three visible rows
   (the knob's four — the reason it was asked for at 360 px), its
   height being lv_roller.c's ``rows × line-height`` of the tier's
   SSID face (51/77/39 → 153/231/156). Entries are centred, truncated
   to 14 chars + a '+' with the " −54DBM" tail, and the cursor is
   simply the SELECTED white entry (amber frame, no box). The dashed
   SCANNING / NO APS FOUND label shares the exact band and is the
   roller's only co-occupant — the pair toggles, since a one-option
   NORMAL roller pins its single row to the window top (which read as
   "not centred", user 2026-11-11). The wifi glyph sits at y −150 and
   the NETWORKS title (32 px face) at y −84. The exit button —
   ``UI_PX(90)`` floored by ``settings_create_exit_button`` to the
   96 px box, BOTTOM_MID −34 — is the shared sub-screen furniture on
   every one of these screens.

.. figure:: ../_static/figs/home_screens.png
   :alt: the five moon-family screens computed per variant — object sets, clock cells, arc band, glyph anchors
   :figclass: align-center

   The **home family** — the five moon screens × the three variants,
   drawn from the builders' own object sets (``create_screen_*`` in
   ``components/ui/screens.c``; generator
   ``docs/tools/gen_home_screens.py``). Rows top→bottom:
   :doc:`modem-sleep <modem-sleep>` (moon disk + shared clock widget +
   the seconds arc — gray band inked inward from get_center's outer
   edge, 212..223 / 334..345 / 162..173, the
   :doc:`disk-geometry <modem-sleep>` family), :doc:`wifi <wifi>`
   (same plus the wifi connection glyph — solid FA face),
   :doc:`light-sleep <light-sleep>` (moon + clock at the container's
   ``UI_CLOCK_ALPHA_PCT`` 20 % over the 0x51-dimmed panel, no arc, no
   glyph; the knob cell is marked — it dims its real backlight instead),
   :doc:`deep-sleep <deep-sleep>` (moon + sleep glyph — the clock
   container carries ``LV_OBJ_FLAG_HIDDEN`` (``actions.c``) and the
   backlight is off; the knob deep-sleeps for real instead), and
   :doc:`alarm <alarm>` (clock + alarm glyph in the REGULAR weight,
   no moon). The clock widget (layout contract 2) sits at byte-
   identical absolute geometry on all five — the boxes are one
   ``--clockprobe`` run: S3 cells 136/30/136 at x 72/218/258 in the
   322×71 row with the dotted 322×34 alarm row under it, P4 210/46/210
   in 488×108, knob 105/23/105 in 253×55 (the ``UI_PX``-scaled
   ``lv_obj`` cells with the theme card's pad-row gap 10/11/10). The
   amber dashed boxes are the indicator labels anchored top-left at
   (``UI_PX(185)``, ``UI_PX(320)``, ``LV_SIZE_CONTENT`` = the tier's
   FA icon face 96/148/74).

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
