The settings hub
================

The settings root: a black screen, a central exit — the fa-circle-xmark glyph IS the button (the white disk is the 96 px font's ink, the ``UI_PX(150)`` argument only widens the tap box) — back to the calling level, and a ring of sub-screen icons at EQUAL arc distances — 360/n degrees apart (S3 and knob: 7 icons × ~51.4°, P4: 8 icons × 45°; the P4 ring opens with the clock icon for manual time entry). The deep-sleep icon is anchored at the bottom (180°) and found **by target**, not by index, so the anchor cannot drift when the ring's count differs between the boards. Moon weight distinguishes the two sleep screens (regular = light sleep, solid = deep sleep); the ring radius is ``UI_PX(140)`` — 140 / 216 / 108 px on the three panels — balanced against the exit disk with equal breathing room both sides.

.. figure:: ../img/screens/annot/settings-hub_s3.png
   :alt: the settings hub — the S3 panel (466, annotated)
   :align: center

   The settings hub — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-hub_p4.png
   :alt: the settings hub — the P4 panel (720, annotated)
   :align: center

   The settings hub — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_hub.png
   :alt: the settings hub — the knob panel (360)
   :align: center

   The settings hub — knob 360×360 at rest (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; seven icons like the S3, centre exit white)

.. figure:: ../img/screens/knob/settings_hub_dial.png
   :alt: the settings hub with the dial cursor focused — the knob panel (360)
   :align: center

   The same hub after one clockwise dial detent — ``screens_dial_step(+1)``: the focused ring icon goes white (grey = unfocused), the central exit carries the focus ring when it is the cursor. Touch the knob's EC1 encoder, the cursor rides the shared handlers (:ref:`dial-semantics-knob <dial-semantics-knob>`)

The ring, computed per variant
------------------------------

Every hub dimension is a ``UI_PX()`` literal of the 466 px design
(``UI_PX(x) = x · panel / 466``, integer division) except the icon
ink, which comes from the per-panel pre-baked font tier — 96 / 148 /
74 px is the same 466 → 720 → 360 ratio, baked once into
``fonts{,_p4,_knob}/`` (:doc:`the knob display <../implementation/knob-display>`
records the tiers). So the ring geometry is not three designs, it is
**one design read at three scales** — and the balance it was tuned at
466 px (inner gap 44 vs outer 45, the 2026-11-04 "stacked icons"
report) holds on every board:

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

The chord clearance is the crowding margin: every ring keeps at least
17 px of air between neighbouring icons — the P4's 8-icon ring runs
the tightest (17 px ≈ one-ninth of one icon). Integer division floors
each literal separately, so the gaps are exact ``UI_PX`` differences,
not ``UI_PX`` of a difference (``216 − 148 = 68`` while ``UI_PX(44)``
would floor to 67).

The figure is drawn by ``docs/tools/gen_hub_ring.py`` straight from
the code's ring arithmetic — the icon centres are the positions
``create_screen_settings_hub`` computes (θ clockwise from 12 o'clock,
``θ = 180° + step·(i − deep)``, position ``(R·sin θ, −R·cos θ)``),
not a schematic guess:

.. figure:: ../_static/figs/hub_ring.png
   :alt: The hub ring geometry on the three panels — panel edge, ring circle, exit disk, icon centres at the computed angles
   :align: center

   The three rings to scale: panel edge (solid), ring circle at ``R`` (dashed), exit disk (amber, with its tap box dotted), icon centres (blue) with the clock-face they land on. The 7-icon ring anchors **deep-sleep at exactly 6 o'clock** and runs alarm → light-sleep → deep-sleep → WiFi → timezone → speaker (~11 o'clock) → rotate (~26° past 12); the 8-icon P4 ring inserts the clock icon at 45° (~1:30) and lands rotate exactly at 12 — the deep-sleep anchor holds because it is found by target, not index.

The wheel keyboard (:doc:`settings-wifi-password <settings-wifi-password>`) has no hub icon of its own — it is reached by tapping an AP row on the WiFi screen, and its only way back is the centre long press.

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
     - Back to the CALLING level — the airplane screen for the normal path. The exit loads the home screen only; it kicks off no sync (the sync triggers are the hourly gate, the wake path on the clocks, and the keyboard commit — :doc:`the transient WiFi screen <wifi>`).
     - ``event_handler_cb_hub_exit``, :cfunc:`settings_hub_icon`'s sibling :cfunc:`settings_create_exit_button`

   * - 2
     - Alarm sound (speaker, solid)
     - The 4-loop sound picker (the ES8311 codec is on both clocks; the knob reaches the screen on the shared ring but its silent ``esp_codec_dev`` shim previews nothing).
     - :doc:`settings-alarm-sound <settings-alarm-sound>` — option ``alarm_sound_id``

   * - 3
     - Alarm (clock, regular)
     - The alarm rules screen (the master toggle + the two entries).
     - :doc:`settings-alarm <settings-alarm>`

   * - 4
     - Light sleep (regular moon)
     - The idle-timeout picker (1–30 min).
     - :doc:`settings-light-sleep <settings-light-sleep>` — option ``idle_timeout_sec``

   * - 5
     - Deep sleep (solid moon)
     - The sleep-countdown picker (1–30 min).
     - :doc:`settings-deep-sleep <settings-deep-sleep>` — option ``deep_sleep_countdown_sec``

   * - 6
     - WiFi
     - The AP scan list (a background scan runs while the screen is open).
     - :doc:`settings-wifi <settings-wifi>`

   * - 7
     - Timezone (globe, solid)
     - The named-zone picker (67 curated cities).
     - :doc:`settings-tz <settings-tz>` — key ``timezone``

   * - 8
     - Display base angle (rotate, solid)
     - The wall-mount base-angle trim: drag the rim handle to set the dial's zero angle, the value saves on release (settings.txt ``display_rotation_deg``).
     - :doc:`settings-display <settings-display>`

   * - —
     - Time (clock, regular) — **P4 only**
     - The ring's manual-time-entry icon, compiled in only under ``CONFIG_BOARD_P4`` (the S3's PCF85063A needs no manual entry; the knob has no such flow) — which is what makes the P4 ring 8-icon and the other two 7-icon.
     - ``#if defined(CONFIG_BOARD_P4)`` in the ring array → :doc:`settings-time <settings-time>`
