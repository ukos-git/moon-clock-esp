The wheel keyboard (WiFi password)
==================================

.. brief:
   Reader: a developer working on the wheel keyboard or the credential hand-off.
   They already know: C, LVGL arcs and the Wi-Fi screen pair.
   After this page they can: trace a press from sector hit-test to the credential write.
   Page type: reference.

The circular wheel keyboard (v2, the 12-sector hybrid; the design record is :doc:`the round keyboard <../keyboard>`). Reached by tapping the network list's roller centre (the SSID prefilled, case kept). **Proportional sectors**: the 26 slots of the active mode tile the ring with equal shares (``SHARE = 360/26``), and the 12 frequency-grouped sectors (``[ab][cd][ef][gh][il][jkq][mn][op][rs][tu][vxz][wy]``) span n·SHARE — the grouping is fixed across all four registers, so the sector boundaries never move when the register changes (the muscle memory of the press position survives abc → 123). At rest: the 26 letters at r 168 and the 12 hairline sector dividers (r 150..226). The **reveal**: on touch-down in the ring band (r 140..233) only the pressed sector's boxes appear — one thick non-rounded arc box per letter, tiling the sector edge-to-edge — and the lit box inverts (white fill, near-black letter); the reveal is sticky to the pressed sector. The **hit-test** is the exact box under the finger (the finger's angle names the sector, the offset-in-sector names the box). The **centre preview** (DTMF-style): while pressed the lit letter shows in the centre with "release commits"; on release the character commits and the running entry re-appears (last 6 + ellipsis). The **register arc**: four 60° keys (abc / ABC / 123 / #\$) on the top 240° (LVGL 150..390, r 84..144, midline r 114); the **bottom 120°**: the house circle-**check** **commit** key (LVGL ~31.5..88.5) and the circle-**xmark** **discard** key (~91.5..148.5), both at r 114 — the check accepts, the X discards (:doc:`the round keyboard <../keyboard>`). A plain tap on the centre **backspaces**; a 400 ms **long press** on the centre is the ONLY way back to the WiFi list (v2 has no in-ring exit — the wheel owns the circle-xmark glyph as *discard*, so the standard circle-xmark exit button is deliberately skipped). All presses commit on **release** only (the reveal + preview show exactly what the release will commit).

.. figure:: ../img/screens/annot/settings-wifi-password_s3.png
   :alt: the wheel keyboard (wifi password) — the S3 panel (466, annotated)
   :align: center

   The wheel keyboard (WiFi password) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-wifi-password_p4.png
   :alt: the wheel keyboard (wifi password) — the P4 panel (720, annotated)
   :align: center

   The wheel keyboard (WiFi password) — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_wifi_password.png
   :alt: the wheel keyboard (wifi password) — the knob panel (360)
   :align: center

   The wheel keyboard (WiFi password) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; at 360 px the character catch's press partition keeps the ring from stealing mode-arc presses — see :doc:`the knob display <../implementation/knob-display>`)

The wheel's own design record (the geometry, the functionality, the 1:1 operable widget) is :doc:`the round keyboard <../keyboard>`; the full alternatives study — the 13 considered methods, each in its own operable 1:1 widget with its pros and cons — is :doc:`the round keyboard lab <../keyboard-lab>`. The four register/press variants are the raw shots this page's annotated view is built from.

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
     - Letter ring (12 sectors)
     - The 26 letters of the active mode at r 168 (the reveal's letters at r 209); the transparent press band r 140..233 catches the touch-down. The inverse-transform idiom keeps the hit-test correct under the 90° orientation roll.
     - ``wheel_point_to_slot`` (the ``WHEEL_*`` geometry in :cfile:`components/ui/screens.c`)

   * - 2
     - Register key (abc)
     - The four 60° keys at r 114 (LVGL 150/210/270/330); the active mode's key is the lit INDICATOR over the 240° MAIN track (both parts styled — a MAIN-only style leaves the indicator at the theme's vivid blue).
     - ``s_wheel_mode_keys`` (the four-key arc)

   * - 3
     - Commit (house circle-check)
     - Saves the credentials (``wifi_credentials_write``) and starts the WiFi sync (``action_start_wifi_sync``) — the shot's "committed" variant lands on the transient WiFi screen. House circle-check at r 114 (LVGL ~31.5..88.5).
     - ``event_handler_cb_wheel_commit`` — the credentials land in :cfile:`spiffs/wifi_credentials.txt`

   * - 4
     - Discard (circle-xmark)
     - Clears the running entry (and the prefilled SSID) without saving. The circle-xmark key at r 114 (LVGL ~91.5..148.5); the icon is built from rotated bars, not a font glyph.
     - ``event_handler_cb_wheel_discard``

   * - 5
     - Centre — tap backspaces, hold returns
     - The centre tap box (r ~55 — it never overlaps the sweep band r 140..233): a plain tap backspaces, a 400 ms long press is the ONLY way back to the WiFi list.
     - the ``s_wheel_center`` box + ``wheel_hold`` (the sim's long-press re-drive)

   * - 6
     - SSID preview
     - The prefilled SSID ("SELECT A NETWORK" when none — the plain-run shot); the running entry's last 6 + ellipsis after a commit, the lit letter + "release commits" while pressed.
     - :cfunc:`settings_wheel_prefill_ssid`, the ``s_wheel_ssid_label`` object
