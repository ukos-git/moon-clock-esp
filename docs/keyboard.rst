The round keyboard
==================

The product has exactly one text-entry surface: entering a Wi-Fi
password (the wheel caps the entry at 63 characters — one below the
Wi-Fi side's 64-byte ceiling, the bound ``wifi.c`` actually enforces)
on a 466×466 panel that is a ~35 mm
disk. This page is the design record of the wheel that shipped — the
**v2 12-sector hybrid** (approved and implemented 2026-11-02): its
functionality, every geometry decision, the LVGL gotchas it cost, and
the uisim verification. The full alternatives study — the 13
considered methods, each in its own operable 1:1 widget with its pros
and cons — is :doc:`the round keyboard lab <keyboard-lab>`; this page
carries only the one that won.

The design goal
---------------

The task is *rare, one-handed, and accuracy-first*: the user enters a
password a handful of times in the product's life, with the device
physically present and the display unmasked. Speed is secondary; a
wrong character costs a re-try, and a *double*-enter costs a corrupted
password. That priority — **certainty over speed** — is the axis the
wheel's design is judged on, and it is the one property release-only
commit buys: the double-entry failure mode of the glide/trace family is
*structurally impossible* on this wheel. The hardware constraints
(466×466 at ~266 PPI, CST9217 touch with no haptics, no mic/camera/NFC,
the QMI8658C roll tracker already sampling) are the full space in
:doc:`the round keyboard lab <keyboard-lab>` — the short version is
that fine tapping is out of the space (a QWERTY grid lands at ~0.75 mm
per key against an 8–10 mm fingertip), so a coarse angular layout with
a deliberate commit is the only family left.

The operable wheel
------------------

The widget below is the shipped wheel at true 1:1 scale — the same
SVG geometry as the lab's panel #1 (the 466×466 panel, the AMOLED
palette), operable in the browser: press a sector, sweep, release.

.. raw:: html

   <iframe src="_static/keyboard/sweep-wheel-v2.html"
           style="width:100%; min-height:730px; border:0; border-radius:12px;"
           loading="lazy" title="Sweep wheel v2 — the shipped 12-sector hybrid, operable at 1:1"></iframe>

Try it: press one of the 12 outer sectors — only that sector's letters
reveal. Slide within the sector to pick a letter (the centre previews
it live with "release commits"), release to commit. The top arc
switches registers (abc / ABC / 123 / #$); the bottom band is ✔
circle-check to accept and ⊗ circle-xmark to discard; a tap in the
centre backspaces.

The machine in one picture
--------------------------

The page below describes each part; the sketch is the machine they form,
drawn straight out of ``components/ui/screens.c`` (the sweep handler at
``:2221``, the inverse-transform hit-test at ``:1915``, the keys at
``:2311``/:2332/:2352``). The shape to hold in mind before any section:
ONE invisible press arc feeds ONE sticky-sector reveal, and the ONLY
moment a character is added is RELEASED-with-the-finger-still-in-band —
press and zone-crossing never write to the entry, which is what makes a
slow sweep double-entry-proof. Who *reads* the finger (the 33 ms LVGL
indev timer, the CST9217 IRQ versus the GT911's pure polling) belongs to
:ref:`the input chain <input-chain-ui>`; what happens after the
commit key writes the credentials belongs to
:doc:`the Wi-Fi sync machine <ui/wifi>`.

.. graphviz:: _graphs/wheel-commit.dot

The screen itself
-----------------

The wheel is reached by tapping an AP row on the WiFi screen (the
SSID prefilled, case kept); it is the
``SCREEN_ID_SETTINGS_WIFI_PASSWORD`` screen, built around the panel
centre, with no moon image (settings are moon-less by design). The v1
two-frame wheel (a grouped-in-threes inner ring + a 2×-angle outer
reveal) is the earlier iteration it replaced.

The at-rest ring
^^^^^^^^^^^^^^^^

The 26 slots of the active register at r 168 (32 px SSID font), 12
hairline sector dividers spanning r 150–226, and two band hairlines at
r 150 / r 226. The grouping is the wayfinding device: the eye aims at
**12 coarse sectors, not 26 targets**. The SSID caption (case kept)
sits in the upper centre.

Proportional sectors
^^^^^^^^^^^^^^^^^^^^

Every letter owns an equal ``SHARE = 360/26 ≈ 13.846°`` of the ring;
the 12 frequency-grouped sectors (``[ab][cd][ef][gh]``,
``[il][jkq][mn][op]``, ``[rs][tu][vxz][wy]``) span ``n·SHARE`` (a
2-letter sector 27.7°, the two 3-letter sectors 41.5°). The grouping
is **fixed across all four registers**, so sector boundaries never
move when the register changes — the muscle memory of the press
position survives the ``abc → 123`` switch. (The digit/symbol
registers are shorter than 26; their groups fill in order.)

The reveal
^^^^^^^^^^

On touch-down anywhere in the ring band (r 140–233, radially
forgiving — a transparent ``ADV_HITTEST`` arc is the press target),
**only the pressed sector's boxes appear**: one thick (76 px)
non-rounded arc box per letter, tiling the sector edge-to-edge across
r 150–226 — the ring band itself, so the reveal replaces the at-rest
letters under the finger (no second cramped ring). The lit box
**inverts** (white fill, near-black letter); the pressed sector's
at-rest letters hide so the reveal letters do not double. The reveal
is **sticky** to the pressed sector — sliding across a sector boundary
sweeps over that sector's boxes, it does not re-key the set.

Hit-test and commit
^^^^^^^^^^^^^^^^^^^

The **exact box under the finger**: the finger's angle names the
sector, the offset-in-sector (``floor(Δangle / SHARE)``) names the box
— no nearest-centre tie-break (v1's 2× overlap is gone; the boxes tile
the ring exactly). **Release commits the letter** under the finger;
nothing commits on press or zone-crossing, so a slow sweep can never
double-enter.

Centre preview (DTMF-style)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

While pressed the lit letter is shown in the centre (32 px SSID font
— the 48 px font is the digits-only subset, no letters) + "release
commits"; on release the character commits and the full running entry
re-appears, **unmasked** (the user is physically present): the last 6
characters with a ``...`` prefix when longer, ``....`` when empty.

Register/shift arc
^^^^^^^^^^^^^^^^^^

4 individually-tappable 60° switch keys ``abc / ABC / 123 / #$`` on
the top 240° arc (r 84–144, LVGL 150°–390° with a 360/0 wrap; the
active key lit at ``150 + 60·m … 210 + 60·m``, the key labels at LVGL
180/240/300/360° r 114). A press resolves to the **exact 60° sector
under the finger** (not the nearest key) and re-strings the 26 slots
(digits: ``0123456789#@._-+*?!/&%:;,`` — 25 slots; symbols:
``#$%&@!_=<>[]{}~^:,/?*+-.'`` — 25 slots). Case is separate (lower/
upper) because it matters for passwords. The whole keyboard's cost
against the password — taps to enter *N* characters, the 63/64 cap
pair, all parsed at figure time — is plotted as :ref:`the tap ladder
<taps-ladder-perf>` in :doc:`the performance page
<verification/performance>`.

Bottom band — commit / discard
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The bottom 120° splits into the house circle-**check** COMMIT key
(LVGL 31.5°–88.5°, icon at LVGL 60° r 114) and the house
circle-**xmark** DISCARD key (LVGL 91.5°–148.5°, icon at LVGL 120°
r 114) — the 2026-11-08 pairing: X means discard, the check means
accept. Both icons are built from rotated ``lv_obj`` bars (the same
house idiom as every other wheel mark; the FontAwesome subset has no
circle-check code point, so the glyph is drawn, not typed — and the
earlier bar-built trash can read badly on the panel and is gone).
Commit (with a
non-empty password) writes ``wifi_credentials_write(ssid, pw)`` (two
lines to ``/spiffs/wifi_credentials.txt``, the password never logged)
→ ``action_start_wifi_sync()`` (the 6144 B background task) and the
screen swaps to the transient WiFi screen; on success the app leaves
airplane mode. An empty password is not written. Discard clears the
entry without writing (and with an empty entry it exits, doubling as
the zero-input exit).

Centre gestures
^^^^^^^^^^^^^^^

A plain tap on the centre (the 110 px tap box) **backspaces**; a
**400 ms long press on the centre returns to the WiFi list** (no
write, no sync) — v2 has no in-ring exit (the xmark glyph is the
discard key), and the centre box (r ≈ 55) never overlaps the sweep band
(r 140–233), so the hold cannot start a letter. The screen's angular
picking uses the inverse-transform idiom (``lv_obj_transform_point``,
``LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE``, the
``alarm_time_point_to_day`` pattern), so it stays correct under the
90° orientation roll.

Known interaction (stated, not suppressed)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The global 2 s press orientation re-sync also fires on long sweeps; it
is pose-stability gated and only re-anchors the home reference, and
the wheel is transform-aware, so a mid-sweep re-pose cannot move the
letter under the finger.

The v2's LVGL gotchas
^^^^^^^^^^^^^^^^^^^^^

Two implementation details that cost a day each and are recorded so
the next LVGL radial screen does not re-pay:

- **``LV_OBJ_FLAG_ADV_HITTEST`` on every clickable arc.** Without it
  ``lv_arc`` falls back to a plain bounding-box hit test and the
  last-created arc wins the search — the commit/discard arc silently
  swallowed the register arc's taps until the flag was added.
- **``lv_obj_transform_rotation`` is in 0.1° units.** The v2's radial
  hairlines are thin ``lv_obj`` bars (LVGL's ``lv_line`` is not built)
  rotated with the transform; passing raw degrees under-rotates the
  ticks 10× (a latent bug the v2 rewrite fixes — v1 passed raw
  degrees). The 0.1°-units contract is easy to miss because the *first*
  hairline (0°) is correct at either scale.
- Every arc also gets the theme's blue circular KNOB (colour primary,
  opaque) — killed with ``bg_opa TRANSP`` on ``LV_PART_KNOB`` (on a
  full ring it lands at the centre as a blue disc).

The uisim verification
----------------------

The wheel is verified in the UI simulator (``host_sim/uisim``) the same
way every other screen is — the indev read re-driven with simulated
timestamps so the long-press state machine ages: the ``--wheel`` flow
taps the hub's WiFi icon at the equal-arc ring position per board (S3
n=6 i=3 / P4 n=7 i=4 — the ring indices of
:cfunc:`create_screen_settings_hub`'s array), drives the touch-down →
sweep → release commit, and the ``wheel_hold`` variant drives the 400
ms centre long press back to the WiFi list. The 5-shot wheel set (the
list, the four register states — rest, ``123``, ``ABC``, ``sym`` —
and the press in flight) is committed for all three panels:
``docs/img/screens/``, ``docs/img/screens/p4/`` and the knob's
``docs/img/screens/knob/``. The uisim pass
proves the *state machine* (hit-test, reveal, commit, backspace,
long-press, register switch); it cannot prove the *feel* on the
CST9217's real glass — which is the pending device check (the
research's open question 2: what does the sweep *actually* feel like,
and does the 0.7–1.5 s/character estimate hold on real glass).

The shots below are from that pass (466 px, the Waveshare
ESP32-S3-Touch-AMOLED-1.75 panel geometry — the 720-px P4 set sits
alongside in ``docs/img/screens/p4/``): the wheel at rest in the
``abc`` register, a press in flight (the pressed sector's reveal
boxes, the centre preview + "release commits"), and the three other
registers — the grouping is fixed across all of them, as designed.

First the same at-rest screen composited onto the real glass (the
Waveshare ESP32-S3-Touch-AMOLED-1.75 product photo, uisim frame at
true panel geometry) — then the flat captures:

.. figure:: _static/photos/sim_keyboard.png
   :alt: Simulated render of the v2 keyboard on the panel glass
   :figclass: align-center

   The v2 wheel on the Waveshare glass — the 12-sector letter ring,
   the register/shift arc, and the centre entry preview, as the panel
   actually shows them.

.. figure:: img/screens/settings_wifi_password.png
   :alt: The v2 keyboard at rest, abc register
   :figclass: align-center

   The **v2 wheel at rest** — the 12-sector grouped letters, the
   register/shift arc (abc / ABC / 123 / #$$), the SSID caption, the
   centre backspace, and the check-commit / X-discard keys in the
   bottom band.

.. figure:: img/screens/settings_wifi_password_touched.png
   :alt: The v2 keyboard mid-press with the sector reveal
   :figclass: align-center

   **A press in flight** — touch-down on the g/h sector reveals
   only that sector's boxes (the lit h inverts), and the centre
   previews the character with "release commits".

.. figure:: img/screens/settings_wifi_password_ABC.png
   :alt: The v2 keyboard in the ABC register
   :figclass: align-center

   The **ABC register** — the same 12 sector groups, upper case;
   the press in the centre carries over (h still previewed).

.. figure:: img/screens/settings_wifi_password_123.png
   :alt: The v2 keyboard in the 123 register
   :figclass: align-center

   The **123 register** — digits 0–9 with a symbol tail; shorter than
   26 slots, the groups fill in order.

.. figure:: img/screens/settings_wifi_password_sym.png
   :alt: The v2 keyboard in the #$/symbol register
   :figclass: align-center

   The **#$/symbol register** — the punctuation and bracket tail.

.. figure:: img/screens/knob/settings_wifi_password_touched.png
   :alt: The v2 keyboard mid-press on the knob panel (360)
   :figclass: align-center

   **The same wheel on the knob** — the shared ``screens.c`` under
   ``CONFIG_BOARD_KNOB`` at 360 px (the h still previewed mid-press);
   at this size the character catch's press partition (its annulus
   lifts to r ≥ 108 at the knob's scale) is what keeps ring presses
   from landing on the mode arc
   (:doc:`the knob display <implementation/knob-display>`).
