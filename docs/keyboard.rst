The round keyboard
==================

.. brief:
   Reader: a developer or curious technical reader who wants to know how the Wi-Fi password keyboard works and why it looks the way it does.
   They already know: LVGL basics, polar coordinates, how a touch screen reports a finger.
   After this page they can: explain the wheel's layout, its press/release machine and its geometry, and change it without re-paying the known LVGL traps.
   Page type: explanation.

The clock has one place where you type text: the Wi-Fi password.
You type it on the *wheel*, a round keyboard with 12 letter sectors around the edge of a 466×466 panel that is a disk of about 35 mm.
This page is the design record of that keyboard (the v2 12-sector hybrid): what it does, every geometry decision, the LVGL traps it hit, and how the UI simulator verifies it.
The 12 alternatives it was weighed against are on :doc:`the round keyboard lab <keyboard-lab>`, each as an operable widget.
How the whole field of 13 methods fits together (who vouched for each, which constraint struck it down, and what the wheel kept) is drawn at :ref:`the thirteen in one picture <thirteen-sketch-lab>`.

The short version
-----------------

- You press one of 12 sectors; only that sector's letters appear, larger, on the ring.
- You slide to the letter you want; the centre previews it.
- The letter is added when you lift your finger, and only then. Pressing and sliding never add anything, so a slow slide cannot add a letter twice.
- The top arc switches between four registers (``abc``, ``ABC``, ``123``, ``#$``). The sector layout is the same in all four.
- The bottom band has a check key (save the password and connect) and an X key (clear the entry, or leave when it is empty).
- A tap in the centre deletes the last character; a 400 ms hold in the centre goes back to the Wi-Fi list.
- The entry holds at most 63 characters, one below the 64-byte limit that ``wifi.c`` enforces (``MAX_PASSWORD_LEN``).

Why a wheel: certainty over speed
---------------------------------

You type a password a handful of times in the product's life, with one hand, with the device in front of you and the entry shown unmasked.
Speed matters little.
A wrong character costs a retry; a character entered twice gives a corrupted password that is hard to spot.
So the design goal is certainty over speed.
Release-only commit is what buys it: the double-entry failure of the glide/trace family cannot happen on this wheel.

The hardware sets the rest.
The panel is 466×466 at about 266 PPI, the CST9217 touch controller has no haptics, there is no microphone, camera or NFC, and the QMI8658C roll tracker is already sampling.
:doc:`The round keyboard lab <keyboard-lab>` lays out the full space.
The short version is that fine tapping does not fit: a QWERTY grid lands at about 0.75 mm per key against an 8–10 mm fingertip.
That leaves a coarse angular layout with a deliberate commit.

Try the wheel
-------------

The widget below is the wheel's interaction at true 1:1 scale, with the same SVG geometry as the lab's panel #1 (the 466×466 panel, the AMOLED palette).
You can operate it in the browser: press a sector, sweep, release.

One difference from the firmware: the widget's bottom band shows the original proposal's keys, ⊗ for commit and a trash can for discard.
The firmware draws a circle-check for commit and a circle-xmark for discard (the wheel section of :cfile:`components/ui/screens.c`; the simulator captures further down show the real screen).

.. raw:: html

   <iframe src="_static/keyboard/sweep-wheel-v2.html"
           style="width:100%; min-height:730px; border:0; border-radius:12px;"
           loading="lazy" title="Sweep wheel v2 — the shipped 12-sector hybrid, operable at 1:1"></iframe>

Press one of the 12 outer sectors and only that sector's letters appear.
Slide within the sector to pick a letter; the centre previews it with "release commits".
Release to add it.
The top arc switches registers (abc / ABC / 123 / #$), and a tap in the centre deletes the last character.

How a press becomes a character
-------------------------------

The sections further down describe each part; the three sketches here show the machine they form.
They are drawn from :cfile:`components/ui/screens.c`: the sweep handler :cfunc:`event_handler_cb_wheel_sweep`, the inverse-transform hit-test :cfunc:`wheel_point_to_slot`, and the key handlers :cfunc:`event_handler_cb_wheel_mode`, :cfunc:`event_handler_cb_wheel_commit` and :cfunc:`event_handler_cb_wheel_discard`.

Keep one shape in mind.
One invisible press arc feeds one sticky-sector reveal.
A character is added only on RELEASED with the finger still in the band; press and zone-crossing never write to the entry, so a slow sweep cannot double-enter.
How the finger is read (the 33 ms LVGL input-device timer, the CST9217 interrupt against the GT911's pure polling) is in :ref:`the input chain <input-chain-ui>`.
What happens after the commit key writes the credentials is in :doc:`the Wi-Fi sync machine <ui/wifi>`.

.. graphviz:: _graphs/wheel-commit.dot
   :alt: The keyboard as one box: a transparent press band feeding the sweep machine and the release commit, plus four other touch targets.
   :caption: The machine sealed. The press target is one transparent ``lv_arc`` (annulus r 140..233, ADV_HITTEST + PRESS_LOCK). Without ADV_HITTEST the arc falls back to a bounding-box test and the last-created arc wins, the trap that cost a day. The sweep and the keys are opened below.
   :align: center

.. graphviz:: _graphs/wheel-commit-sweep.dot
   :alt: The sweep machine opened: press finds the slot through the inverse transform, the pressed sector reveals, sliding clamps to that sector, release adds the character, cancel restores the ring.
   :caption: The sweep machine. PRESSED maps the input point through the inverse transform of the screen root (the 90° orientation roll, the alarm-time idiom); the band test and atan2 then name the exact box, with no nearest-centre tie-break. Only the pressed sector reveals, its at-rest twins hide, and the lit box inverts. PRESSING clamps to the sticky sector and never re-keys. RELEASED adds the character (bounds-checked one below the cap at which ``wifi.c`` rejects) and always restores the ring; CANCEL restores it without adding.
   :align: center

.. graphviz:: _graphs/wheel-commit-keys.dot
   :alt: The at-rest ring and the four keys: 26 letters in 12 sectors, the register track, commit, discard and the centre, with notes on the knob, the orientation re-sync and demo mode.
   :caption: The at-rest ring, the register track and the four one-press touch targets. The register track is one arc with the INDICATOR's own pad pinned to 0 (the track-pad fix for the radius dropping into pie wedges). The register arc picks the exact 60° key and clears the stray 25th slot. COMMIT writes the credentials and hands over to :doc:`the Wi-Fi sync machine <ui/wifi>`. DISCARD clears an entry, or leaves when the entry is empty. A centre tap deletes a character and its 400 ms long press leaves. The three amber nodes are machines drawn elsewhere: the knob's z-order lift (the inner-half "mode 3" bug, bench 2026-11-11), the 2 s orientation re-sync (stated, not suppressed), and demo mode's second pointer.
   :align: center

The screen and its parts
------------------------

You reach the wheel by tapping a network row on the Wi-Fi screen; the network name (SSID) is prefilled with its case kept.
It is the ``SCREEN_ID_SETTINGS_WIFI_PASSWORD`` screen, built around the panel centre.
It shows no Moon image; settings screens are moon-less by design.

The ring at rest
^^^^^^^^^^^^^^^^

The 26 slots of the active register sit at r 168 in the 32 px SSID font.
Twelve hairline sector dividers span r 150–226, with two band hairlines at r 150 and r 226.
The grouping is how you find your way: the eye aims at 12 coarse sectors, not 26 targets.
The SSID caption, case kept, sits in the upper centre.

Proportional sectors
^^^^^^^^^^^^^^^^^^^^

Every letter owns an equal ``SHARE = 360/26 ≈ 13.846°`` of the ring.
The 12 frequency-grouped sectors (``[ab][cd][ef][gh]``, ``[il][jkq][mn][op]``, ``[rs][tu][vxz][wy]``) each span ``n·SHARE``: 27.7° for a 2-letter sector, 41.5° for the two 3-letter sectors.
The grouping is fixed across all four registers, so sector boundaries never move when you change register.
Your memory of where to press survives the ``abc → 123`` switch.
The digit and symbol registers have fewer than 26 characters; their groups fill in order.

The keyboard is also the most expensive screen to build.
Its 150 objects (42 arcs, 49 containers and 59 labels, from the ``WHEEL_N_SECTORS`` loop and the 26 letters drawn twice) are 59 % of the whole 255-object population.
``create_screens()`` builds them once at boot and never destroys them; the wheel is not an ``lv_roller``.
:ref:`The assembly ladder <assembly-ladder-perf>` parses the bill and the build-once rules from the live source.

The reveal
^^^^^^^^^^

Touch down anywhere in the ring band (r 140–233; the band is forgiving in radius because a transparent ``ADV_HITTEST`` arc is the press target), and only the pressed sector's boxes appear.
Each letter gets one thick (76 px) non-rounded arc box, and the boxes tile the sector edge to edge across r 150–226.
That is the ring band itself, so the reveal replaces the at-rest letters under the finger instead of adding a second, cramped ring.
The lit box inverts (white fill, near-black letter), and the pressed sector's at-rest letters hide so the letters do not appear twice.
The reveal sticks to the pressed sector: sliding across a sector boundary moves over that sector's own boxes and does not switch to the next sector's letters.

Hit-test and commit
^^^^^^^^^^^^^^^^^^^

The hit-test returns the exact box under the finger.
The finger's angle names the sector, and the offset inside the sector (``floor(Δangle / SHARE)``) names the box.
The boxes tile the ring exactly, so no nearest-centre tie-break is needed.
Release adds the letter under the finger.
Nothing is added on press or on crossing into another box, so a slow sweep cannot enter a letter twice.

Centre preview (DTMF-style)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

While you press, the lit letter shows in the centre with "release commits" below it, in the 32 px SSID font (the 48 px font is a digits-only subset with no letters).
On release the character is added and the running entry reappears, unmasked because you are in front of the device.
It shows the last 6 characters with a ``...`` prefix when longer, and ``....`` when empty.

Register/shift arc
^^^^^^^^^^^^^^^^^^

Four separately tappable 60° keys, ``abc / ABC / 123 / #$``, sit on the top 240° arc (r 84–144, LVGL 150°–390° with a 360/0 wrap).
The active key is lit at ``150 + 60·m … 210 + 60·m``, and the key labels sit at LVGL 180/240/300/360°, r 114.
A press resolves to the exact 60° sector under the finger, not the nearest key, and re-strings the 26 slots:

- digits: ``0123456789#@._-+*?!/&%:;,`` (25 slots);
- symbols: ``#$%&@!_=<>[]{}~^:,/?*+-.'`` (25 slots).

Lower and upper case are separate registers because case matters in passwords.

The keyboard's cost against the password (taps to enter *N* characters and the 63/64 cap pair, all parsed when the figure is built) is plotted as :ref:`the tap ladder <taps-ladder-perf>` in :doc:`the performance page <verification/performance>`.
What the twelve alternatives from the lab would cost the same finger, using the published speed and error figures converted when the figure is built, is :ref:`the certainty ladder <certainty-ladder-perf>` on the same page.

Bottom band: commit and discard
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The bottom 120° splits into two keys:

- the circle-check COMMIT key (LVGL 31.5°–88.5°, icon at LVGL 60°, r 114);
- the circle-xmark DISCARD key (LVGL 91.5°–148.5°, icon at LVGL 120°, r 114).

X means discard and the check means accept.
Both icons are built from rotated ``lv_obj`` bars, the same idiom as every other wheel mark.
The FontAwesome subset has no circle-check code point, so the glyph is drawn, not typed.

COMMIT with a non-empty password calls ``wifi_credentials_write(ssid, pw)``, which writes two lines to ``/spiffs/wifi_credentials.txt`` and never logs the password.
It then calls ``action_start_wifi_sync()`` (a background task with a 6144 B stack), and the screen swaps to the transient Wi-Fi screen.
On success the app leaves airplane mode.
An empty password is not written.
DISCARD clears the entry without writing; with an empty entry it leaves for the Wi-Fi list, so it doubles as the exit when nothing has been typed.

Centre gestures
^^^^^^^^^^^^^^^

A plain tap on the centre (the 110 px tap box) deletes the last character.
A 400 ms long press on the centre returns to the Wi-Fi list, with no write and no sync.
These are the two ways out of the screen: the centre long press, and DISCARD on an empty entry.
The ring itself has no exit key, because the xmark glyph is the discard key.
The centre box (r ≈ 55) never overlaps the sweep band (r 140–233), so the hold cannot start a letter.

The screen's angular picking uses the inverse-transform idiom (``lv_obj_transform_point`` with ``LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE``, the ``alarm_time_point_to_day`` pattern).
So it stays correct under the 90° orientation roll.

Known interaction: the orientation re-sync
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The global 2 s press orientation re-sync also fires on long sweeps.
It is gated on a stable pose and only re-anchors the home reference.
The wheel is transform-aware, so a re-pose in the middle of a sweep cannot move the letter under the finger.

LVGL traps
^^^^^^^^^^

Two of these cost a day each.
They are recorded so the next radial LVGL screen does not hit them again.

- Every clickable arc needs ``LV_OBJ_FLAG_ADV_HITTEST``.
  Without it, ``lv_arc`` falls back to a plain bounding-box hit test and the last-created arc wins the search.
  The commit/discard arc silently swallowed the register arc's taps until the flag was added.
- ``lv_obj_transform_rotation`` takes 0.1° units.
  The radial hairlines are thin ``lv_obj`` bars rotated with the transform, because ``lv_line`` is not built on the device.
  Passing raw degrees under-rotates the ticks 10×.
  The mistake is easy to miss because the first hairline (0°) is correct at either scale.
- Every arc also gets the theme's blue circular knob (primary colour, opaque).
  On a full ring it lands at the centre as a blue disc; ``bg_opa TRANSP`` on ``LV_PART_KNOB`` removes it.

How the simulator verifies it
-----------------------------

The UI simulator (``host_sim/uisim``) verifies the wheel the same way as every other screen.
It re-drives the input-device read with simulated timestamps, so the long-press state machine ages.

The ``--wheel`` flow taps the hub's Wi-Fi icon at its equal-arc ring position for each board (S3 n=6 i=3, P4 n=7 i=4; the ring indices of the array in :cfunc:`create_screen_settings_hub`).
It then drives touch-down, sweep and release commit.
The ``wheel_hold`` variant drives the 400 ms centre long press back to the Wi-Fi list.
The 5-shot wheel set (the list, the four register states rest, ``123``, ``ABC`` and ``sym``, and the press in flight) is generated for all three panels: ``docs/img/screens/``, ``docs/img/screens/p4/`` and ``docs/img/screens/knob/``.

The simulator proves the state machine: hit-test, reveal, commit, backspace, long press and register switch.
It cannot prove how the sweep feels on the CST9217's real glass.
That device check is still open (the research's open question 2: what the sweep feels like, and whether the 0.7–1.5 s per character estimate holds on real glass).

The shots below come from that pass at 466 px, the panel geometry of the Waveshare ESP32-S3-Touch-AMOLED-1.75; the 720 px P4 set is in ``docs/img/screens/p4/``.
They show the wheel at rest in the ``abc`` register, a press in flight (the pressed sector's boxes and the centre preview with "release commits"), and the three other registers, with the same grouping in each.
The first shot puts the at-rest screen onto a photo of the real board (the Waveshare ESP32-S3-Touch-AMOLED-1.75 product photo, simulator frame at true panel geometry); the rest are flat captures.

.. figure:: _static/photos/sim_keyboard.png
   :alt: The keyboard at rest, drawn onto a photo of the S3 board's round screen.
   :figclass: align-center

   The wheel on the Waveshare glass: the 12-sector letter ring, the register/shift arc and the centre entry preview, as the panel shows them.

.. figure:: img/screens/settings_wifi_password.png
   :alt: The keyboard at rest in the abc register.
   :figclass: align-center

   The wheel at rest: the 12 grouped sectors, the register/shift arc (abc / ABC / 123 / #$), the SSID caption, the centre backspace, and the check (commit) and X (discard) keys in the bottom band.

.. figure:: img/screens/settings_wifi_password_touched.png
   :alt: The keyboard during a press, with one sector's letters shown large.
   :figclass: align-center

   A press in flight: touch-down on the g/h sector shows only that sector's boxes, the lit h is inverted, and the centre previews the character with "release commits".

.. figure:: img/screens/settings_wifi_password_ABC.png
   :alt: The keyboard in the upper-case register.
   :figclass: align-center

   The ABC register: the same 12 sector groups in upper case. The centre preview carries over (h still previewed).

.. figure:: img/screens/settings_wifi_password_123.png
   :alt: The keyboard in the digits register.
   :figclass: align-center

   The 123 register: digits 0–9 with a symbol tail. It has fewer than 26 slots, so the groups fill in order.

.. figure:: img/screens/settings_wifi_password_sym.png
   :alt: The keyboard in the symbols register.
   :figclass: align-center

   The #$ symbol register: the punctuation and bracket tail.

.. figure:: img/screens/knob/settings_wifi_password_touched.png
   :alt: The keyboard during a press on the knob board's 360 px screen.
   :figclass: align-center

   The same wheel on the knob: the shared ``screens.c`` under ``CONFIG_BOARD_KNOB`` at 360 px, with h previewed mid-press.

On the knob's smaller panel, the press partition of the character catch keeps ring presses from landing on the mode arc: its annulus starts at r ≥ 108 at the knob's scale (:doc:`the knob display <implementation/knob-display>`).

The v1 wheel, the earlier bottom-band pairing and the approval of v2 are recorded in :doc:`the design history </project/history>`.

Where to go next
----------------

- :doc:`The round keyboard lab <keyboard-lab>`: the twelve alternatives, each operable.
- :doc:`The Wi-Fi sync machine <ui/wifi>`: what happens after you press commit.
- :doc:`Performance <verification/performance>`: the tap and certainty ladders.
