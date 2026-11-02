Chapter 21 — The round keyboard
===============================

The product has exactly one text-entry surface: entering a Wi-Fi
password (8–64 arbitrary characters) on a 466×466 panel that is a ~35 mm
disk. This chapter is its design record: the task and its hardware
constraints, the literature the design was checked against, the
interactive design lab that made the alternatives operable, and the
**v2 12-sector hybrid wheel** that shipped — with every decision
(keep / ship / hold / reject) attached to the evidence that made it.

The task and the constraints
----------------------------

The task is *rare, one-handed, and accuracy-first*: the user enters a
password a handful of times in the product's life, with the device
physically present and the display unmasked. Speed is secondary; a
wrong character costs a re-try, and a *double*-enter costs a corrupted
password. That priority — **certainty over speed** — is the axis every
decision below is judged on.

The hardware constraints are the other half of the design space:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Constraint
     - Consequence
   * - 466×466 px, ~35 mm disk (~266 PPI)
     - a 4×4 QWERTY grid lands at ~0.75 mm per key — ~10× below the
       8–10 mm fingertip. Fine tapping is not in the space.
   * - CST9217 capacitive touch, **no haptics**
     - no detents, no confirmation ticks; the display is the only
       feedback channel (the live preview is load-bearing).
   * - No microphone, no camera, no NFC, no BLE peripheral in this
       product (Wi-Fi only)
     - voice, OCR, and every read-side handoff (QR/NFC) are infeasible,
       not merely expensive.
   * - QMI8658C IMU already sampled (the 90° roll tracker)
     - a twist gesture is *free hardware* — but a twist **is** a roll
       change, so the gesture and the re-orientation are the same
       signal (disqualifying conflict, below).
   * - ESP32-S3 + LVGL 9.6 software renderer
     - the wheel must be a few hundred lines over the existing ring
       geometry, hit-test and font — not a new input stack.

The research
------------

A deep-research pass over the HCI literature (CHI, UIST, MobiSys,
SAGE, IJHCI, IEEE, plus NHTSA/TRB and vendor documentation) is
committed at ``docs/research/circular-keyboard-alternatives.md``. Its
verdict, in one paragraph:

**Keep the sweep wheel as the primary entry method — the literature
supports the family (coarse angular targets + sweep/press-release are
the most reliable touch model on a ~35 mm panel) — and bolt on two
cheap alternatives: a 12-sector DTMF-style multi-tap ring for the
123/#$ registers (30° targets, 1–4 presses, no re-aim) and a
hold-to-commit dwell option on the outer zones. Everything else
(QWERTY grid, voice, OCR, QR/NFC/BLE handoff, one-stroke gestures,
word prediction) is infeasible on this hardware or a bad fit for
arbitrary password strings. A physical rotary encoder is the
strongest evidence-backed upgrade, but it is a hardware-revision
item, not a firmware one.**

The load-bearing findings:

1. **A ring-of-keys on the screen edge is a published, studied design
   for exactly this form factor.** C-QWERTY (2020) arranges
   alphanumeric keys along the edge of a circular smartwatch in
   QWERTY order — the only published design that directly addresses
   "round keyboard on round display" for a touch device.
2. **The companion designs all use a small number of large angular
   sectors plus a commit interaction** — never 26 fine taps on the
   rim: COMPASS (CHI 2017, multi-cursor circular keyboard, bezel
   rotation), WrisText (CHI 2018, wrist whirl to a circular
   alphabetical layout), Crownboard (CHI 2023, crown-rotation
   scanning, **2.3 % / 0.53 % error rates** — the best in the corpus),
   RingByte (UIST 2025, rotating outer ring + neural decode).
3. **On small watch screens, fewer bigger targets win.** Smartwatch
   QWERTY tap measures 18–20 WPM with 14–15 % WER; trace 29–33 WPM;
   a 9-sector × 4-character swipe keyboard *improved* accuracy over
   QWERTY at marginal speed loss (Tap'nSwipe, IJHCI 2025).
4. **Even with statistical decoding, fitted QWERTY grids are at the
   resolution limit of a ~35 mm disc** (WatchWriter, FingerText,
   "Is it too small?").
5. **The in-car line explains *why* coarse angular input wins** on
   small round hardware: an on-road CHI 2017 study found target
   accuracy high for all methods (>94 %) — the physical dial's case is
   made on *glance/distraction metrics and physical certainty*, not
   per-target accuracy. The transfer to a wrist-worn clock: the wheel's
   value is certainty and low error, not raw speed — exactly what a
   password entry wants.
6. **Dwell (hold-to-commit) tops out around 20 WPM** (CHI 2025) and
   needs training — enough to ship it as a *fallback*, not the
   primary.
7. **Direct 2D pointing beats circular layouts on raw speed** (T9VR /
   Sector Input) — the counter-evidence, valid *when the panel can
   host a QWERTY*; it cannot, at 35 mm. (The P4's 720 px disk is large
   enough to make a compact QWERTY worth a real benchmark on a future
   board — noted, not done.)

The interactive design lab
--------------------------

A research document that ranks alternatives by citation is one thing;
feeling them is another. The lab at ``docs/keyboard/round-keyboard-lab.html``
(a single self-contained page — vanilla HTML/SVG/JS, no build, no
server, committed to the repo so the documented choices and the
operable reference cannot drift apart) renders **all 13 text-entry
methods** at **true 1:1 scale** — every mock is a 466×466 SVG, the
device's own panel geometry, the device's own black-on-black AMOLED
palette — and the feasible ones are **actually operable in the
browser** (pointer events, the device's timing constants), so the feel
can be compared before any C is written.

The 13 panels, in the lab's verdict-strip order:

.. list-table::
   :header-rows: 1
   :widths: 8 34 58

   * - #
     - Panel
     - Verdict (evidence row on the page)
   * - 1
     - **Sweep wheel v2 — 12-sector hybrid** (operable)
     - **KEEP** — the approved direction; the v2 spec below
   * - 2
     - 12-sector DTMF multi-tap (1–4 presses, 700 ms window; operable)
     - **SHIP** as an addition — the coarse-press + centre-preview
       model v2 inherits
   * - 3
     - Hold-to-commit dwell (400 ms; operable)
     - **SHIP** as the default fallback
   * - 4
     - Auto-scan, Crownboard-style (lit sector steps at 1/s; operable)
     - **SHIP** as an accessibility setting (decoder-free)
   * - 5
     - 9×4 swipe sectors, Tap'nSwipe (operable)
     - HOLD — revisit only if multi-tap proves unlearnable
   * - 6
     - Fitted QWERTY 6×6 (operable, to make the rejection case *by
       feel*)
     - **REJECT** — 0.75 mm keys, below fingertip
   * - 7
     - Ring glide / one-stroke (the over-swipe risk, made visible)
     - **REJECT** as primary — double-entry on arbitrary strings
   * - 8
     - IMU twist (body rotation drives the ring)
     - EXPERIMENTAL — conflicts with the 90° roll tracker
   * - 9
     - Physical rotary encoder (12 sectors + side crown)
     - the **right v2-board upgrade** (Crownboard-class)
   * - 10–13
     - Voice · camera/OCR · QR/NFC/BLE handoff · T9 dictionary decode
     - INFEASIBLE / REJECTED — no sensors; companion app overkill for a
       rare task; arbitrary passwords have no word model

Every panel carries the research report's evidence rows — best
published speed, accuracy evidence with source, implementation cost on
the ESP32-S3 + LVGL, and the verdict — so the lab is the *clickable
half* of this chapter: the static text says what was decided, the
page lets a user **operate** what was decided and what was rejected.

The v2 12-sector hybrid wheel
-----------------------------

The shipped design (approved and implemented 2026-11-02; the
operable 1:1 reference is the lab's panel #1) is the **v2 "12-sector
hybrid"**. The v1 two-frame wheel (a grouped-in-threes inner ring + a
2×-angle outer reveal) is the earlier iteration it replaced. The
screen is reached by tapping an AP row on the WiFi screen; it is a
13th screen (``SCREEN_ID_SETTINGS_WIFI_PASSWORD``) built around the
panel centre, with no moon image (settings are moon-less by design).

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
``[il][jkq][mn][op]``, ``[rs][tu][vxz][wy]``) span ``n·SHARE`` (a 2-letter sector 27.7°, the two
3-letter sectors 41.5°). The grouping is **fixed across all four
registers**, so sector boundaries never move when the register changes
— the muscle memory of the press position survives the ``abc → 123``
switch. (The digit/symbol registers are shorter than 26; their groups
fill in order.)

The reveal
^^^^^^^^^^

On touch-down anywhere in the ring band (r 140–233, radially
forgiving — a transparent ``ADV_HITTEST`` arc is the press target),
**only the pressed sector's boxes appear**: one thick (76 px)
non-rounded arc box per letter, tiling the sector edge-to-edge across
r 150–226 — the ring band itself, so the reveal replaces the at-rest
letters under the finger (no second cramped ring). The lit box
**inverts** (white fill, near-black letter); the pressed sector's
at-rest letters hide so the reveal letters do not double. The reveal is
**sticky** to the pressed sector — sliding across a sector boundary
sweeps over that sector's boxes, it does not re-key the set.

Hit-test and commit
^^^^^^^^^^^^^^^^^^^

The **exact box under the finger**: the finger's angle names the
sector, the offset-in-sector (``floor(Δangle / SHARE)``) names the box
— no nearest-centre tie-break (v1's 2× overlap is gone; the boxes tile
the ring exactly). **Release commits the letter** under the finger;
nothing commits on press or zone-crossing, so a slow sweep can never
double-enter. That last property is the one the research's
"certainty over speed" priority buys: the double-entry failure mode of
the glide/trace family is *structurally impossible* on this wheel.

Centre preview (DTMF-style)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

While pressed the lit letter is shown in the centre (32 px SSID font —
the 48 px font is the digits-only subset, no letters) + "release
commits"; on release the character commits and the full running entry
re-appears, **unmasked** (the user is physically present): the last 6
characters with a ``…`` prefix when longer, ``....`` when empty.

Register/shift arc
^^^^^^^^^^^^^^^^^^

4 individually-tappable 60° switch keys ``abc / ABC / 123 / #$`` on the
top 240° arc (r 84–144, LVGL 150°–390° with a 360/0 wrap; the active
key lit at ``150 + 60·m … 210 + 60·m``, the key labels at LVGL
180/240/300/360° r 114). A press resolves to the **exact 60° sector
under the finger** (not the nearest key) and re-strings the 26 slots
(digits: ``0123456789#@._-+*?!/&%:;`` — 25 slots; symbols:
``#$%&@!_=<>[]{}~^:,/?*+-.'`` — 25 slots). Case is separate (lower/
upper) because it matters for passwords.

Bottom band — commit / discard
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The bottom 120° splits into the house circle-xmark **COMMIT** key
(LVGL 31.5°–88.5°, icon at LVGL 60° r 114) and the trash **DISCARD**
key (LVGL 91.5°–148.5°, icon at LVGL 120° r 114). Commit (with a
non-empty password) writes ``wifi_credentials_write(ssid, pw)`` (two
lines to ``/spiffs/wifi_credentials.txt``, the password never logged)
→ ``action_start_wifi_sync()`` (the 6144 B background task) and the
screen swaps to the transient WiFi screen; on success the app leaves
airplane mode. An empty password is not written. Discard clears the
entry without writing. The circle-xmark is the house exit glyph
**repurposed as the commit key** (a deliberate v2 deviation from v1,
where it was the exit).

Centre gestures
^^^^^^^^^^^^^^^

A plain tap on the centre (the 110 px tap box) **backspaces**; a
**400 ms long press on the centre returns to the WiFi list** (no write,
no sync) — v2 has no in-ring exit (the xmark glyph is the commit key),
and the centre box (r ≈ 55) never overlaps the sweep band (r 140–233),
so the hold cannot start a letter. The screen's angular picking uses
the inverse-transform idiom (``lv_obj_transform_point``,
``LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE``, the
``alarm_time_point_to_day`` pattern), so it stays correct under the 90°
orientation roll.

Known interaction (stated, not suppressed)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The global 2 s press orientation re-sync also fires on long sweeps; it
is pose-stability gated and only re-anchors the home reference, and the
wheel is transform-aware, so a mid-sweep re-pose cannot move the
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
n=5 i=3 / P4 n=6 i=4), drives the touch-down → sweep → release commit,
and the ``wheel_hold`` variant drives the 400 ms centre long press
back to the WiFi list. The 17-shot screenshot set at both panel sizes
(466 and 720) is committed to ``docs/img/screens/``. The uisim pass
proves the *state machine* (hit-test, reveal, commit, backspace,
long-press, register switch); it cannot prove the *feel* on the
CST9217's real glass — which is the pending device check (the
research's open question 2: what does the sweep *actually* feel like,
and does the 0.7–1.5 s/character estimate hold on real glass).

The decisions, recorded
-----------------------

The chapter's verdict, restated as the record:

.. list-table::
   :header-rows: 1
   :widths: 26 42 32

   * - Decision
     - The choice
     - Why
   * - **KEEP**
     - the two-frame → v2 sweep wheel as primary
     - the literature's family: coarse angular targets + deliberate
       commit; release-only commit makes double-entry structurally
       impossible
   * - **SHIP** (additions)
     - 12-sector DTMF multi-tap (123/#$), hold-to-commit dwell,
       auto-scan setting
     - few hundred lines each over the wheel's plumbing; dwell is also
       the hedge against the pending real-glass check
   * - **HOLD**
     - 9×4 swipe sectors
     - duplicates what multi-tap + sweep already do; revisit only if
       multi-tap proves unlearnable
   * - **REJECT**
     - fitted QWERTY, ring-glide primary, T9 dictionary, voice/OCR,
       QR/NFC/BLE handoff
     - targets below fingertip / no word model for arbitrary strings /
       no sensors / companion app overkill for a rare task
   * - **V2 BOARD**
     - physical rotary encoder + haptic detent
     - the strongest evidence base in the corpus (Crownboard-class
       2.3 % error; the in-car distraction win) — a hardware item, not
       a firmware one

Two open questions are recorded as open (the research's own): whether
C-QWERTY's QWERTY-vs-alphabetical effect size (behind a bot-blocked
full text) justifies re-stringing the ring against the grouped
alphabetical layout, and whether DTMF ordering is actually familiar to
this user population or 20th-century nostalgia. Both are settleable by
a 30-second user test; neither blocks the shipped design.

The next chapters turn from the *user-facing* design to the
*developer-facing* interface: the Python API the book's examples and
the gate are built on (Chapter 22), the C API the device compiles
(Chapter 23), the build system and Kconfig that keep two boards
buildable from one tree (Chapter 24), and the glossary and references
(Chapter 25).
