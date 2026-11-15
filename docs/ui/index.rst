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

The same graph is also priced in **taps** — BFS depth from the awake
home per board, the four machine-only rows with their drivers, and
the wheel's per-character price — as :ref:`the tap ladder
<taps-ladder-perf>` in :doc:`the performance page
<../verification/performance>`; its bars are checked against this
very matrix (16/15/13 asserted at figure time).

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

.. _screen-population-ui:

The population in one picture (built once, visited free)
--------------------------------------------------------

The navigation graph above says *which screens exist and who may open
whom*. This picture draws what is **alive** while those edges are
walked: the build-once population. ``ui_init`` (``ui.c:77``) runs
``create_screens()`` — all sixteen ``create_screen_*`` in enum order,
each rooted at exactly one detached ``lv_obj_create(0)`` — and only
*then* loads the airplane home (``ui.c:84``): **every object of the
city exists before the first frame is ever shown.** The city is 255
object instances under 16 roots (256 on the P4 — the hub's 7th
``ring[]`` label is the whole per-board delta), and the code's own
contract sits at ``screens.c:1368``: *"The widgets are created once
(loadScreen never destroys them), so the pointers live here."* The
awake user sees 17 of the 255; a visit creates nothing, destroys
nothing, and at most pays an entry refresh.

.. graphviz:: ../_graphs/screen-population.dot
   :align: center
   :alt: The population in black boxes — ui_init feeding the sealed hidden city of 16 roots and 255 objects, feeding the sealed zero-cost visit; the dashed EEZ twin and the dashed knob no-widgets note hang off it.
   :caption: The population in black boxes — boot, city, visit, each sealed; the city and the visit open into their own sketches below. The boot spine with its lines: ``ui_init`` (``ui.c:77``) runs ``create_screens()`` at :78, THEN ``loadScreen(SCREEN_ID_MODEM_SLEEP)`` at :84 — the airplane home is built already; the never-shipped EEZ_FOR_LVGL twin (``ui.c:12-23``) is priced nowhere. Navigation edges are deliberately absent: they are the mermaid graph above (and the tap costs at :ref:`the tap ladder <taps-ladder-perf>`). The arithmetic (per-screen bars, per-board totals) is parsed live by ``docs/tools/gen_perf_assembly.py``, whose SystemExit-failing parsers are this sketch's twin; the object cost in bytes is the static footprint on :doc:`the performance page <../verification/performance>`.

.. graphviz:: ../_graphs/screen-population-city.dot
   :align: center
   :alt: The hidden city opened — create_screens feeding the four moon roots (with the shared moon image and the one reparented clock widget), the hub, the amber wheel at 150, and the ten small screens.
   :caption: The hidden city opened. ``create_screens()`` (``screens.c:3894``) calls exactly the 16 ``create_screen_*`` in enum order; EACH creator roots at exactly ONE detached ``lv_obj_create(0)`` — layout contract 1, and the twin parser dies on a second root or a missing creator. The four moon roots carry 17 (modem-sleep, including the clock widget), 5 (wifi), 3 (light-sleep), 4 (deep-sleep) objects and are the ONLY ``moon_screen_prepare`` call sites (``screens.c:41-45``); ONE moon image serves all four (``moon_ui_create_image``, ``moon_ui.c:2717`` — "s_images[] stays at the four moon screens"), the 11 settings screens + alarm are moon-less by design. The hub's 9: six ``ring[]`` labels (``screens.c:594-603``) + exit + root, and the ``#if defined(CONFIG_BOARD_P4)`` leg adds a 7th (clock icon → settings-time) — the ONLY per-board delta in the whole bill. The wheel's 150 = 59 %: 42 arcs + 49 containers + 59 labels from the 12-sector loop (``WHEEL_N_SECTORS``, ``screens.c:1863``) and the 26 letters × 2 — NOT a roller. The other 10 screens run 3 to 12 objects each (alarm view lightest at 3; settings-time 12, P4-reachable only); ALL 14 ``lv_roller`` instances live there, none in the wheel. The clock widget is created exactly ONCE (layout contract 2 — 13 of the home's 17) and ``ui_clock_attach_to`` (``screens.c:3826``) REPARENTS it onto every moon root: "computed, not rendered" for settings screens.

.. graphviz:: ../_graphs/screen-population-visit.dot
   :align: center
   :alt: A visit opened — loadScreen with zero creator calls feeding the exactly-ten-case entry refresh and the current-screen-only tick dispatch, with the dashed 0x51 fade pointer beside it.
   :caption: A visit opened — the build-once bill. ``loadScreen(old→new)`` (``ui.c:51``): ``lv_scr_load`` swaps roots (:63), then ``ui_clock_attach_to`` (:69) and ``moon_ui_screen_switched`` (:74) land the atomic frame — NO creator call sits inside it (the twin parser DIES on any ``*_create(`` there); the code's own contract, "The widgets are created once (loadScreen never destroys them)" (``screens.c:1368``). ``settings_screen_refresh_for`` (``screens.c:730``) has exactly 10 cases: the 10 sub-screens pay an entry REFRESH (rollers re-seed from model state), never a rebuild. ``ui_tick`` (``ui.c:87``) dispatches through ``tick_screen_funcs[]`` (``screens.c:3872``, the 16 tickers in enum order): the fifteen off-screen roots tick ZERO times, every frame. The switch rides the 0x51 fade-to-black (``UI_SCREEN_SWITCH_FADE``, internals in ``docs/_graphs/screen-switch.dot``); the both-moon test (``ui.c:57``) skips the fade between moon roots — "a fade with nothing to fade". The knob's P2 dial block (``screens.c:3918-4301``) contains ZERO ``lv_*_create`` — the dial cursor only restyles existing objects.

The rules the graph encodes:

- **Built once, before the first frame.** ``create_screens()``
  (``screens.c:3894``) runs inside ``ui_init`` *before* the first
  ``loadScreen``; each of the 16 creators owns exactly one detached
  root (layout contract 1).
- **Never destroyed.** ``loadScreen`` (``ui.c:51``) contains no
  creator call and no destroy — the twin parser refuses to draw if a
  ``*_create(`` ever appears inside it. A switch swaps roots,
  reparents the ONE clock widget (``ui_clock_attach_to``,
  ``screens.c:3826``, "computed, not rendered" for settings screens),
  and lands the atomic frame.
- **The bill is concentrated, not spread.** One screen — the round
  keyboard — assembles 150 of the 255 (59 %), and it is not a
  roller: the family's 14 ``lv_roller`` instances all live on the
  list screens. The four sleep-state roots share the ONE moon image
  (``s_images[]`` stays at the four, ``screens.c:48``).
- **Only the current screen ticks.** ``ui_tick`` (``ui.c:87``)
  dispatches through ``tick_screen_funcs[]`` (``screens.c:3872``) to
  the loaded screen alone; the fifteen off-screen roots do no work
  per frame. The 10 sub-screens additionally pay one entry REFRESH
  (``settings_screen_refresh_for``, ``screens.c:730``, exactly 10
  cases — rollers re-seed, nothing rebuilds).
- **The knob adds no widgets.** Its P2 dial block
  (``screens.c:3918-4301``) contains zero ``lv_*_create`` calls — the
  dial cursor only restyles existing objects. The dial's *input* path
  is its own story, :ref:`below <input-chain-ui>`.

What this picture deliberately omits: the navigation edges (the
mermaid graph above; the tap costs at :ref:`the tap ladder
<taps-ladder-perf>`), the fade mechanics of a switch
(``docs/_graphs/screen-switch.dot``), and the RAM the 255 objects
cost (the static footprint on :doc:`the performance page
<../verification/performance>`). The object *counts* and the
build-once invariants, re-derived from the sources at figure time,
are :ref:`the assembly ladder <assembly-ladder-perf>`.

.. _type-family-ui:

The type in one picture (seven faces, three tiers, two choosers)
----------------------------------------------------------------

The population sketch counts the objects; this one draws what paints
them. Every glyph on every screen comes from **seven faces** — two
Montserrat weights for the digits, two full-ASCII SSID faces, a
hand-tuned 12-glyph weekday face, and two Font Awesome icon faces —
baked **three times**, once per panel density: ``fonts/``
(96/48/32 px on the 466 S3), ``fonts_p4/`` (148/74/50 on the 720 P4),
``fonts_knob/`` (74/37/25 on the 360 knob). The 21 files carry
**identical ``lv_font_t`` symbol names**, so the swap is a directory
choice: ``components/ui/CMakeLists.txt``'s ``FONT_DIR`` ladder picks
one tier per board (``KNOB_BOARD`` / ``CONFIG_BOARD_P4`` / default),
``host_sim/uisim/build.sh --panel`` mirrors it for the screenshots,
and ``fonts.h`` plus every consumer are unchanged. A repo-wide grep
for ``&ui_font_`` hits exactly two files — ``screens.c`` (43
references) and ``styles.c`` (4): the knob's and the clocks' other
sources never ask for a font directly, they ride ``screens.c``.

The family is drawn the way the crew and the city are: one black-box
overview, then a detail sketch per sealed box — the seven faces, the
three tiers with their choosers, and the two askers.

.. graphviz:: ../_graphs/type-family.dot
   :align: center
   :alt: The type family in black boxes — the dashed foundry and the cyan askers feeding the sealed seven faces, which bake into the sealed three tier directories armed by the dashed choosers.
   :caption: The type family in black boxes — foundry, faces, tiers, choosers, askers, each sealed; each box opens below. The three claims this family holds up: (1) 21 files = 7 faces × 3 tiers with IDENTICAL ``lv_font_t`` symbols — the tier swap is a CMake directory choice, ``fonts.h`` and every consumer are unchanged; (2) the ONLY per-board font asymmetry is the clock U+F017, carried in the P4+knob regular sets and asked only by the P4 ring's 7th entry — the knob inherited the glyph from the P4 Opts and never draws it; (3) each face carries exactly what the firmware can ask. The arithmetic — every glyph count, raster byte total and ask count re-derived from the 21 files and the two asker files with SystemExit-failing parsers — is :ref:`the glyph ladder <glyph-ladder-perf>`.

.. graphviz:: ../_graphs/type-family-faces.dot
   :align: center
   :alt: The seven faces opened — the two digit faces, the two SSID faces and the amber weekday face on the left, the two violet icon faces on the right, each with its glyph count and what it inks.
   :caption: The seven faces opened. The digit faces are Montserrat: ``montserrat_bold_96_numbers`` carries 12 glyphs (Opts ``--symbols - --range 48-58`` — digits, ':' from the range, '-'; the home clock's HH:MM ink) and ``montserrat_regular_48_numbers`` carries 13 (adds the dot) — the settings-time DATE editor's face: three day/month/year rollers (``screens.c:2996-3004``) plus the two "." separators (:2998/:3002). The two SSID faces cover the full printable range, Opts ``-r 32-126``, 95 glyphs. ``montserrat_regular_32_range`` is the hand-tuned 12-glyph weekday alphabet, Opts ``-r 45,70,77,83,84,87,97,101,104,111,114,117`` = '-' F M S T W a e h o r u — exactly the alphabet the two weekday tables can spell (``s_day_abbr[7]`` ``actions.c:986`` + ``ALARM_DAY_ABBR[7]`` ``screens.c:1033``). The solid icon face carries 6 (Opts range 61480,61527,61612,61830,61931,62194 = speaker U+F028, xmark U+F057, globe U+F0AC, moon U+F186, wifi U+F1EB, stopwatch U+F2F2); the regular icon face carries 2 on the S3 (62286,61830 = alarm U+F34E, moon U+F186) and 3 on the P4 and knob — the clock U+F017 joins as the P4 ring's 7th entry.

.. graphviz:: ../_graphs/type-family-tiers.dot
   :align: center
   :alt: The three tiers opened — the two amber choosers arming the three tier directories with their per-panel px rungs; dashed edges carry the inheritance from the P4's Opts to the S3 and to the knob.
   :caption: The three tiers opened (``components/ui/CMakeLists.txt:1-21``) — one directory per panel density: ``fonts/`` is the S3's 466 px set (96/48/32, icons 96), ``fonts_p4/`` the P4's 720 px set (148/74/50, icons 148), ``fonts_knob/`` the knob's 360 px set (74/37/25, icons 74) — all three carry the identical symbol names so ``fonts.h`` and every consumer are unchanged (the CMakeLists comment's own words). Six of the seven faces share their Opts charset across tiers byte-for-byte (only ``--size`` differs); the regular icon face is the exception the comment predates — its 720/360 ranges added 61463 for the P4-only clock entry, and the knob tier was baked from that P4 Opts, so the knob carries the clock glyph and never draws it (the ring's clock entry is ``#if defined(CONFIG_BOARD_P4)``, ``screens.c:595-597``). The device chooser is the ``FONT_DIR`` ladder (``if(KNOB_BOARD)`` / ``elseif(CONFIG_BOARD_P4)`` / ``else fonts``), keyed on PROJECT variables deliberately — the early requirements pass reads neither project vars nor ``CONFIG_`` symbols, and it is harmless because SRCS are consumed by the target pass. The host chooser is ``host_sim/uisim/build.sh:51-69``: ``--panel 466`` → ``fonts/``, ``720`` → ``fonts_p4/`` (+ ``-DCONFIG_BOARD_P4=1``), ``360`` → ``fonts_knob/`` (+ ``-DCONFIG_BOARD_KNOB=1``) — "the symbol names are identical, screens.c is shared". The dashed foundry of the overview: all 21 files carry an ``lv_font_conv`` provenance header ("Size/Bpp/Opts"), every file ``--no-compress`` at Bpp 4; the four source faces live in ``assets/fonts/`` (Montserrat Regular+Bold .ttf, Font Awesome 7 Free-Regular-400 + Free-Solid-900 .otf — the shelf also holds siblings no tier bakes).

.. graphviz:: ../_graphs/type-family-asks.dot
   :align: center
   :alt: The askers opened — screens.c and styles.c plus the wheel, the hub ring and the weekday tables funneling their per-face ask counts into the five face groups; the dashed knob node rides screens.c.
   :caption: The askers opened — a repo-wide grep for ``&ui_font_`` finds exactly two files: ``screens.c`` (43 references) + ``styles.c`` (4); the knob's and main's sources ask NOTHING directly, they ride ``screens.c``. Per face: bold96 12 (11+1), 48-numbers 6 (5+1), 48-SSID 3 (Wi-Fi roller MAIN+SELECTED ``screens.c:1684-1685`` + placeholder :1732), 32-SSID 14 (wheel pw label + the 26+26 sector letters "both 32 px (the SSID font)" ``screens.c:2582-2596`` + the Wi-Fi text rows), 32-range 2 (``screens.c:1337`` range summary + :3490 alarm-row weekday prefix), solid 5 (4 screens + 1 style installer), regular 5 (3 ring entries + the weight-routing comparison ``screens.c:157`` + 1 style installer). ``styles.c``'s two installers (``add_style_label_icon_regular``/``_solid``) set the font onto the icon style. The hub ring (``screens.c:594-603``): six unconditional entries — alarm (regular), light-sleep moon (regular), deep-sleep moon (SOLID — "Moon weight distinguishes the two sleep screens"), wifi, globe, speaker (solid) — plus the P4-only 7th, clock (regular) → settings-time; the xmark U+F057 exit button and the stopwatch U+F2F2 join from the same solid face.

The rules the graph encodes:

- **One name per face, three bakes.** All 21 files share the seven
  symbol names; ``fonts.h`` declares each exactly once. The CMake
  comment's claim — identical symbols so "``fonts.h`` and every
  consumer are unchanged" — is the seam's contract, and the ladder's
  parsers assert it (a face appearing in one tier only, or a name
  drifting between tiers, kills the figure).
- **Each face carries exactly what the firmware can ask.** The two
  SSID faces cover the whole printable range 32-126 because an SSID
  is arbitrary (95 glyphs each); the weekday face
  (``montserrat_regular_32_range``) is a 12-codepoint hand-picked set
  — ``- F M S T W a e h o r u`` — which is *exactly* the alphabet of
  the two weekday tables (``s_day_abbr`` ``actions.c:986``,
  ``ALARM_DAY_ABBR`` ``screens.c:1033``) spelling "Mo", "Mo-Fr",
  "Fr-Mo"; the wheel's four registers
  (``s_wheel_sets``, ``screens.c:1868-1873``) stay inside the SSID
  face's 95 by construction.
- **The icons are per-board, and one glyph proves it.** The hub's
  ring (``screens.c:594-603``) carries six entries on every board and
  a seventh — the clock, U+F017, into settings-time — only under
  ``#if defined(CONFIG_BOARD_P4)``. Correspondingly the *regular*
  icon face carries 2 codepoints in the S3 tier and 3 in the P4 and
  knob tiers: the knob was baked from the P4's icon Opts, so it
  carries the clock glyph and never draws it. Moon-weight is the
  ring's own trick: "Moon weight distinguishes the two sleep screens"
  (regular light / solid deep). The solid face's stopwatch define
  keeps a charset war story in place: *"was U+F9F2, absent -> a box"*
  (``screens.c:87``).
- **The font is ink; the wheel is a consumer like any other.** The
  round keyboard's 26+26 sector letters and its password preview all
  draw on the 32 px SSID face — "both 32 px (the SSID font)"
  (``screens.c:2582``) — so the character face is named for Wi-Fi and
  paints the wheel's alphabet.
- **Provenance lives in the file.** All 21 carry an ``lv_font_conv``
  header (Size/Bpp/Opts — the file's own birth certificate), every
  bake is ``--bpp 4 --no-compress``; the four source faces live under
  ``assets/fonts/`` (the Fontawesome shelf holds six more siblings
  that no tier bakes).

What this picture deliberately omits: the raster *bill* per panel
(glyph counts, bitmap bytes — :ref:`the glyph ladder
<glyph-ladder-perf>`), the binary budget the tiers land inside
(:doc:`the binary page <../verification/binary>`), and the per-panel
font statements themselves, which the board pages keep in sync with
the files: :doc:`S3 <../devices/s3>`, :doc:`P4 <../devices/p4>`,
:doc:`knob <../devices/knob>`.

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
   :align: center
   :alt: The input chain in black boxes — the sealed clocks box (one BSP indev, three guarded listeners) and the sealed knob box (own touch and dial tasks) both landing on the shared hit-test, with the dashed demo second-indev joining it.
   :caption: The input chain in black boxes — two families, one hit-test; each box opens into its own sketch below. All listeners and CLICKED handlers run in the LVGL task (indev events land in ``lv_timer_handler``) — no atomics needed. The demo's second indev (``demo.c:797``, ``vt_read_cb`` :97) injects a press 90 + release 60 = ``TAP_TOTAL_MS`` 150 (< 400 ms long-press gate → SHORT_CLICKED) — a real hit-tested tap while the BSP indev stays live for the human; it is created AFTER the three registrations bound to the first indev, so it never steals their target (:doc:`demo mode <../verification/demo-mode>`).

.. graphviz:: ../_graphs/input-chain-clocks.dot
   :align: center
   :alt: The clocks opened — app_main booting the S3 CST9217 and P4 GT911 device paths into the one 33 ms read timer, which fans to the idle, 2 s press and alarm listeners.
   :caption: The clocks opened (S3 + P4, one chain). ``app_main`` calls ``bsp_display_start()`` (:572, S3) / ``bsp_display_start_with_config()`` (:570, P4 — XC BSP 3.0.1, target-gated in ``main/idf_component.yml``), ``bsp_display_unlock`` follows the registrations. The S3 path: ``bsp_display_indev_init`` (BSP :555) → ``bsp_touch_new`` (:443) → ``esp_lcd_touch_new_i2c_cst9217`` (:470) → ``bsp_lv_adapter_register_touch`` (:562) — CST9217 on the shared I2C, INT = GPIO 11 / RST = GPIO 40 (BSP header :56-:57), the default cfg's mirror_x/mirror_y apply (BSP :573); INT ≠ NC so the adapter runs IRQ MODE — but the ISR only signals a semaphore, reads still ride the timer. The P4 path: GT911 over I2C, pure POLLED — RST/INT = NC on the 4C so the adapter's IRQ mode stays off (pins in ``docs/_graphs/p4-wiring.dot``). The three listeners with their lines: ``clock_idle_init`` registers ``indev_read_cb`` on PRESSED+PRESSING (``clock_idle.c:70/:71``) — a PRESSED always resets the idle timer, ONLY while IDLE it funnels ``clock_trigger_idle_wake`` (that one-way door is ``docs/_graphs/wake-doors.dot``); ``action_register_orientation_touch`` (``actions.c:382``) on PRESSED+RELEASED — a press ≥ ``ORI_RESYNC_PRESS_MS`` 2000 (:230) sets ``s_ori_resync_req``, actioned only while the pose is stable AND on-axis (pipeline: ``docs/_graphs/tilt-pipeline.dot``); ``action_register_alarm_touch`` (``actions.c:1169``) on PRESSED — ``g_alarm_active && !clock_is_idle()`` → ``action_wake_screen`` dismisses the ring (the awake bypass of the idle door, same sketch).

.. graphviz:: ../_graphs/input-chain-knob.dot
   :align: center
   :alt: The knob opened — the polled CST816S and the pulse-pair dial at top, feeding the own POINTER indev, the tick-safe floor and the dial task, which land root-CLICKED commits, dim steps and hit-tests on the shared lock.
   :caption: The knob opened (every pin, cadence and story the sealed box hides). ``CST816S`` @ 0x15 (SDA 11 · SCL 12 · INT 9 · RST 10): hard-reset pulse, normal-mode write once, then 7-byte polls from reg 0 — contact count at [2], x/y nibble-joined (``knob_board_touch.c``); the INT line is wired but POLLED anyway (vendor style). Our own ``touch_read_cb`` (``knob/main/main.c:83``) → ``lv_indev_create`` POINTER (:388) — the one difference from the clocks: WE supply the read callback; every press also calls ``knob_power_notify_activity`` (K7e: touch is activity), and the ok/err heartbeat probe logs a dead chip vs a silent panel vs a live one. The dial: EC1 A = GPIO 8 · B = GPIO 7, 10K pull-ups — NOT quadrature, an UP/DOWN PULSE PAIR (one channel pulses per detent and WHICH channel is the direction; bench + vendor 2026-11-08); ``enc_task`` (``knob_board_encoder.c:58``) polls 3 ms in the vendor ``process_knob_channel`` shape — LOW samples only re-arm, the detent fires on the debounced RISE (2 ticks), the count moves under ``portMUX``. The tick-safe floor is the amber footnote: at the knob's 100 Hz systick ``pdMS_TO_TICKS(3)`` == 0 and ``vTaskDelay(0)`` does NOT yield — the priority-5 poller then busy-spins, starves IDLE0 and the task WDT aborts the boot (bench 2026-11-11: "CPU 0: knob_enc" died at t ≈ 6.4 s); one tick = 10 ms, and detents are ≥ 30 ms apart anyway. ``dial_task`` 20 ms (``knob_dial.c:81``) consumes the count ONE detent at a time; off home → activity reported BEFORE the lock (a contended dial must never delay the un-dim), then ``knob_ui_lvgl_lock`` → ``screens_dial_step`` → unlock. On home the dial is BRIGHTNESS, not input: ±5 % steps through ``moon_ui_panel_brightness_set``, floored at the ladder's own dim rung — ``DIAL_DIM_MIN_PCT`` from ``CONFIG_KNOB_DIM_DUTY`` 64 = 25 % (user 2026-11-10). Tap-to-commit: a background CLICKED lands on the screen root (LVGL's no-target fallback) → ``screens_dial_commit`` (``knob_dial.c:38``), already inside the LVGL task's lock; ``knob_dial_start`` re-adds CLICKABLE to every settings root knob-side only (LVGL 9 needs CLICKABLE to make a press TARGET at all) — moon roots excluded, their tap opens the hub. And the reentrant lock (``knob_ui.c:225-:266``): holder+depth, a same-task take is a depth bump — a FreeRTOS mutex is NOT recursively takable by its owner, so the plain take self-deadlocked on the first tap when a CLICKED handler re-took it (loadScreen → seam); the dial task and the indev path meet at exactly this wrapper.

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

The *timing* of this chain — how soon each path lands a visible
change, and how the knob's dedicated encoder path alone beats the
shared 33 ms read timer — is plotted as the response ladder in
:doc:`the performance page <../verification/performance>`.

What the *widget* builds on top of these reads is a different
machine: the round keyboard's exact-box hit-test, sticky-sector
reveal and commit-only-on-release is drawn in
:doc:`the round keyboard <../keyboard>`.

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
