The settings hub
================

.. brief:
   Reader: a firmware developer working on the settings menu.
   They already know: C, LVGL basics, the ``UI_PX`` scaling macro.
   After this page they can: find each hub icon's target and explain the ring geometry on all three panels.
   Page type: reference.

The settings hub is the root of the settings menu: a black screen with an exit button in the centre and a ring of icons, one per settings screen.
A tap on the home screen opens it.

How it behaves
--------------

A tap on a ring icon opens that settings screen.
A tap on the centre exit returns to the calling level, which is the airplane home screen on the normal path.

The icons sit at equal arc distances, 360/n degrees apart.
The S3 and the knob have 7 icons, about 51.4° apart; the P4 has 8, 45° apart.
The P4 ring opens with a clock icon for manual time entry.

The deep-sleep icon is anchored at the bottom (180°).
The code finds it by target, not by index, so the anchor cannot drift when the ring's count differs between boards.
The moon icon's weight tells the two sleep screens apart: regular for light sleep, solid for deep sleep.

On the knob, a dial detent moves a focus cursor round the ring and a tap commits (:ref:`dial-semantics-knob <dial-semantics-knob>`).

Screenshots
-----------

.. figure:: ../img/screens/annot/settings-hub_s3.png
   :alt: The settings hub on the S3 panel, with numbered callouts on the exit and each ring icon.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/settings-hub_p4.png
   :alt: The settings hub on the P4 panel, with numbered callouts on the exit and each ring icon.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/settings_hub.png
   :alt: The settings hub on the knob's 360 px panel at rest: seven icons and a white centre exit.
   :align: center

   Knob, 360×360, at rest: the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``.
   Seven icons, like the S3; the centre exit is white.

.. figure:: ../img/screens/knob/settings_hub_dial.png
   :alt: The settings hub on the knob after one clockwise dial detent, with one ring icon white and the others grey.
   :align: center

   The same hub after one clockwise dial detent (``screens_dial_step(+1)``).
   The focused ring icon is white and the unfocused ones grey; when the centre exit is the cursor, it carries the focus ring.
   The knob's EC1 encoder moves the cursor over the shared handlers (:ref:`dial-semantics-knob <dial-semantics-knob>`).

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
     - Exit (centre circle-xmark)
     - Back to the calling level: the airplane screen on the normal path. The exit loads the home screen only and starts no sync. The sync triggers are the hourly gate, the wake path on the clocks and the keyboard commit (:doc:`the transient WiFi screen <wifi>`).
     - ``event_handler_cb_hub_exit``, :cfunc:`settings_hub_icon`'s sibling :cfunc:`settings_create_exit_button`

   * - 2
     - Alarm sound (speaker, solid)
     - The four-loop sound picker. The ES8311 codec is on both clocks; the knob reaches the screen on the shared ring, but its silent ``esp_codec_dev`` shim previews nothing.
     - :doc:`settings-alarm-sound <settings-alarm-sound>`; option ``alarm_sound_id``

   * - 3
     - Alarm (clock, regular)
     - The alarm rules screen (the master toggle and the two entries).
     - :doc:`settings-alarm <settings-alarm>`

   * - 4
     - Light sleep (regular moon)
     - The idle-timeout picker (1–30 min).
     - :doc:`settings-light-sleep <settings-light-sleep>`; option ``idle_timeout_sec``

   * - 5
     - Deep sleep (solid moon)
     - The sleep-countdown picker (1–30 min).
     - :doc:`settings-deep-sleep <settings-deep-sleep>`; option ``deep_sleep_countdown_sec``

   * - 6
     - WiFi
     - The access-point scan list. A background scan runs while the screen is open.
     - :doc:`settings-wifi <settings-wifi>`

   * - 7
     - Timezone (globe, solid)
     - The named-zone picker (67 curated cities).
     - :doc:`settings-tz <settings-tz>`; key ``timezone``

   * - 8
     - Display base angle (rotate, solid)
     - The wall-mount base-angle trim. A 0..359 roller sets the display's zero angle, and the value saves on release (settings.txt ``display_rotation_deg``).
     - :doc:`settings-display <settings-display>`

   * - —
     - Time (clock, regular), P4 only
     - The ring's manual-time-entry icon, compiled in only under ``CONFIG_BOARD_P4``. The S3's PCF85063A needs no manual entry and the knob has no such flow. This icon makes the P4 ring 8 icons and the other two 7.
     - ``#if defined(CONFIG_BOARD_P4)`` in the ring array → :doc:`settings-time <settings-time>`

Implementation notes
--------------------

The centre exit is the fa-circle-xmark glyph itself: the white disk is the 96 px font's ink.
The ``UI_PX(150)`` argument only widens the tap box.
The ring radius is ``UI_PX(140)``: 140, 216 and 108 px on the three panels.
It is balanced against the exit disk, with equal space on both sides.

The wheel keyboard (:doc:`settings-wifi-password <settings-wifi-password>`) has no hub icon.
You reach it by tapping an access-point row on the WiFi screen, and its only way back is the centre long press.

The ring, computed per variant
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Every hub dimension is a ``UI_PX()`` literal of the 466 px design (``UI_PX(x) = x · panel / 466``, integer division).
The one exception is the icon ink, which comes from the per-panel pre-baked font tier.
Its sizes, 96, 148 and 74 px, follow the same 466 → 720 → 360 ratio and are baked once into ``fonts{,_p4,_knob}/`` (:doc:`the knob display <../implementation/knob-display>` records the tiers).
So the ring is one design read at three scales.
The balance it was tuned to at 466 px (inner gap 44, outer gap 45) holds on every board:

.. list-table::
   :header-rows: 1
   :widths: 34 22 22 22

   * - Hub quantity (code source)
     - S3 466
     - P4 720
     - knob 360
   * - ring icons ``n`` (ring array; clock icon only under ``CONFIG_BOARD_P4``)
     - 7
     - 8
     - 7
   * - angle step ``360/n``
     - 51.43°
     - 45.0°
     - 51.43°
   * - ring radius ``R = UI_PX(140)``
     - 140
     - 216
     - 108
   * - icon ink ⌀ (icon font tier)
     - 96
     - 148
     - 74
   * - centre exit tap box ``UI_PX(150)``
     - 150
     - 231
     - 115
   * - exit disk ink ⌀ (same glyph tier)
     - 96
     - 148
     - 74
   * - exit rim → icon inner edge ``R − ink``
     - 44
     - 68
     - 34
   * - icon outer edge → panel rim ``panel/2 − R − ink/2``
     - 45
     - 70
     - 35
   * - adjacent chord ``2R·sin(step/2)``
     - 121
     - 165
     - 93
   * - chord clearance ``chord − ink`` (icons never crowd)
     - 25
     - 17
     - 19
   * - sub-screen exit tap box (call passes ``UI_PX(90)``, floored at ``UI_PX(96)``)
     - 96
     - 148
     - 74

The chord clearance is the crowding margin.
Every ring keeps at least 17 px between neighbouring icons; the P4's 8-icon ring is the tightest (17 px, about one-ninth of an icon).
Integer division floors each literal separately, so the gaps are exact differences of ``UI_PX`` values, not ``UI_PX`` of a difference: ``216 − 148 = 68``, while ``UI_PX(44)`` floors to 67.

``docs/tools/gen_hub_ring.py`` draws the figure from the code's ring arithmetic.
The icon centres are the positions ``create_screen_settings_hub`` computes: θ clockwise from 12 o'clock, ``θ = 180° + step·(i − deep)``, position ``(R·sin θ, −R·cos θ)``.

.. figure:: ../_static/figs/hub_ring.png
   :alt: The hub ring on the three panels, drawn to scale: panel edge, ring circle, exit disk and icon centres at the computed angles.
   :align: center

   The three rings to scale: panel edge (solid), ring circle at ``R`` (dashed), exit disk (amber, tap box dotted) and icon centres (blue) with the clock position they land on.
   The 7-icon ring anchors deep-sleep at exactly 6 o'clock and runs alarm → light-sleep → deep-sleep → WiFi → timezone → speaker (about 11 o'clock) → rotate (about 26° past 12).
   The 8-icon P4 ring inserts the clock icon at 45° (about 1:30) and puts rotate exactly at 12; the deep-sleep anchor holds because the code finds it by target, not index.

Where to go next
----------------

- :doc:`The UI index <index>`: the full navigation graph the hub sits in.
- :doc:`The display trim <settings-display>`: the rotate icon's screen and its roller.
- :ref:`dial-semantics-knob <dial-semantics-knob>`: how the knob's dial drives the same ring.
