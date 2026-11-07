The round keyboard lab
======================

Thirteen ways to enter text on the product's 35 mm round glass — every
alternative the round keyboard was checked against, each presented in
its **own operable 1:1 widget** with the pros and cons that decided
its fate. This page is the design study: the task, the hardware
constraints, the literature, all thirteen panels (nine on-hardware
widgets, four off-hardware text cards), and the recorded decisions.
The one design that shipped — the v2 12-sector hybrid wheel — has its
own page with its own widget: :doc:`the round keyboard <keyboard>`.

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

The operable lab — every panel in its own widget
------------------------------------------------

A research document that ranks alternatives by citation is one thing;
feeling them is another. The lab
(``docs/_static/keyboard/round-keyboard-lab.html`` — a single
self-contained page, vanilla HTML/SVG/JS, no build, no server,
committed to the repo so the documented choices and the operable
reference cannot drift apart) renders **all 13 text-entry methods**
at **true 1:1 scale** — every on-hardware mock is a 466×466 SVG, the
device's own panel geometry, the device's own black-on-black AMOLED
palette — and the feasible ones are **actually operable in the
browser** (pointer events, the device's timing constants: multi-tap
700 ms, dwell 400 ms, auto-scan 1 s), so the feel can be compared
before any C is written.

This page does not embed the lab page as one big iframe. Each
method's operable disk is embedded **individually, in its own section
below** — the lab's ``?only=N`` embed mode, one widget per panel with
its "try it" line, no duplicate prose (the what / pros / cons /
verdict text on this page is the record). The four off-hardware
methods have no operable disk (no sensor to drive one) and stay text
cards, here and in the lab's lower grid. For the side-by-side view of
the whole thirteen at once — the verdict strip, the evidence rows, the
off-hardware cards:

.. raw:: html

   <p style="margin:0 0 18px;"><a href="_static/keyboard/round-keyboard-lab.html"
   target="_blank" rel="noopener"><strong>Open the full lab in a new tab
   ↗</strong></a></p>

The panels, one by one
----------------------

The nine on-hardware methods, in the lab's verdict-strip order — each
with what it is, the pros that pulled for it, the cons that held it
back, and the verdict. The evidence rows on the widgets are the
research report's own.

1 · Sweep wheel v2 — 12-sector hybrid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** The mix that shipped: 12 coarse press sectors sized
to their letter groups (every letter owns an equal 360/26 of the
ring), a reveal that on touch-down shows only the pressed sector's
options on one radius, a DTMF-style centre preview of the selected
letter, and release-only commit. Four 60° shift keys on the top arc,
⊗ commit and trash discard in the bottom 120°, centre tap = backspace.

**Pros.** Coarse targets — the 27.7°/41.5° sectors are well above a
fingertip; the reveal's equal-share boxes make the pick unambiguous;
release-only commit makes double-entry *structurally impossible*; the
large centre preview is certainty without lifting the finger; the
grouping is fixed across all four registers so the finger's sector
boundaries never move; a few hundred LOC over the existing ring
geometry.

**Cons.** The slowest absolute entry of the operable panels (0.7–1.5
s/char, estimated 4–8 CPM — the "certainty over speed" price); the
sweep's feel on the CST9217's real glass is the pending device check;
while pressed, the entry text is replaced by the preview (it re-
appears on release).

**Verdict.** **KEEP · v2** — the approved direction; the shipped
design's record and operable widget are on :doc:`the round keyboard
<keyboard>`.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=1"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Sweep wheel v2 — the operable 1:1 panel"></iframe>
   </div>

2 · 12-sector DTMF multi-tap
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** Press a sector 1–4 times; each press picks the next
character down the sector's column (a dot per press). The thin arc is
the 700 ms window; timeout commits, releasing early also commits.

**Pros.** 30° targets — 1.4× the width of a two-letter reveal box;
no re-aim after the first press (multi-tap absorbs the aiming); the
timeout absorbs jitter; cheap: a few hundred LOC and 2–4 KB of flash;
this is the coarse-press + centre-preview model v2 inherits.

**Cons.** Slowest per character for common letters (up to 4 presses);
the 700 ms window tuning is unverified on real glass (the research's
open micro-study); the character order within a sector is a
configuration-table choice, not a familiar one.

**Verdict.** **SHIP** as an addition — best cost/benefit; take the
123/#$ registers first.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=2"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="12-sector DTMF multi-tap — the operable 1:1 panel"></iframe>
   </div>

3 · Hold-to-commit dwell
^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** Press and hold 400 ms on a letter — the arc fills,
then commits. Sliding re-targets and restarts the timer; early
release cancels. No sweep motion at all.

**Pros.** No new error class — the same 27° target and live preview
as the wheel; zero sweep motion (works for a user who cannot sweep
reliably); tens of LOC; also hedges the pending real-glass sweep
check (a fallback that needs no new feel data).

**Cons.** Dwell tops out at ~20 WPM (the CHI 2025 ceiling) and needs
training; a 400 ms hold on a 35 mm panel is tiring over a 64-character
password.

**Verdict.** **SHIP** as the default fallback.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=3"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Hold-to-commit dwell — the operable 1:1 panel"></iframe>
   </div>

4 · Auto-scan, Crownboard-style
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** The lit sector steps clockwise at ~1/s (the serial
floor, shown live in the widget); tapping anywhere commits the lit
character. The decoder is deliberately skipped — it only helps
dictionary words.

**Pros.** 0.53–2.3 % error *by construction* (the best in the
corpus); zero aiming at all; low implementation cost (scan only, no
decoder).

**Cons.** The ~1 s/character floor is set by the scan period; the tap
must land in the lit sector's window; it is an accessibility aid, not
a primary method.

**Verdict.** **SHIP** as an accessibility setting, not the primary.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=4"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Auto-scan, Crownboard-style — the operable 1:1 panel"></iframe>
   </div>

5 · 9×4 swipe sectors
^^^^^^^^^^^^^^^^^^^^^

**What it is.** Press in the ring, drag, and release: the release
direction (out / left / in / right) picks one of the sector's 4
characters (Tap'nSwipe, IJHCI 2025).

**Pros.** Better accuracy than QWERTY on small watches at marginal
speed loss; 29–33 WPM measured in the trace/tap literature (SAGE
2019); one press-and-release per character.

**Cons.** Medium cost — gesture classification plus a new layout
table; a drag under 15 px reads as a tap (a whole error class at the
edge of the gesture); duplicates what multi-tap + sweep already
cover.

**Verdict.** **HOLD** — revisit only if multi-tap proves
unlearnable.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=5"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="9×4 swipe sectors — the operable 1:1 panel"></iframe>
   </div>

6 · Fitted QWERTY 6×6
^^^^^^^^^^^^^^^^^^^^^

**What it is.** Tap the keys: a 6×6 grid of 60 px ≈ 4.5 mm keys, the
grid the disk clips at the corners (the widget makes the rejection
case *by feel*).

**Pros.** The familiar layout; direct 2D pointing is the fastest
input on panels that can host it (the T9VR / Sector Input
counter-evidence is valid above ~720 px — the P4 note, finding 7).

**Cons.** 4.5 mm keys against an 8–10 mm fingertip; 14–15 % WER
measured at watch key sizes; high implementation cost for the
rejected outcome.

**Verdict.** **REJECTED** — keys well below a fingertip on a 35 mm
disk.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=6"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Fitted QWERTY 6×6 — the operable 1:1 panel"></iframe>
   </div>

7 · Ring glide / one-stroke
^^^^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** Press and sweep along the ring: every crossed letter
commits immediately (the over-swipe risk, made visible in the
widget).

**Pros.** 29–33 WPM measured on QWERTY + dictionary (SAGE 2019); one
continuous gesture per word.

**Cons.** Needs a word model to recover an over-swipe — a password is
arbitrary, so there is none; deliberate slow motion double-enters;
the double-entry failure mode cannot be made structurally impossible.
That is exactly the property release-only commit buys.

**Verdict.** **REJECTED** as a primary for arbitrary strings.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=7"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Ring glide / one-stroke — the operable 1:1 panel"></iframe>
   </div>

8 · IMU twist
^^^^^^^^^^^^^

**What it is.** Drag to twist the body — the ring rotates with it;
the badge fires every 90° boundary crossing (WrisText, CHI 2018 —
the wrist-whirl family).

**Pros.** The hardware is free (the QMI8658C is already sampled for
the roll tracker); a published gesture family for circular input.

**Cons.** No published speed figure; the disqualifier is structural —
a twist **is** a roll change, so the gesture and the device
re-orientation are the same signal, and the 90° roll tracker needs
that exact signal.

**Verdict.** **EXPERIMENTAL** at best; disqualifying today.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=8"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="IMU twist — the operable 1:1 panel"></iframe>
   </div>

9 · Physical encoder + haptic detents
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**What it is.** Drag vertically or use the mouse wheel: the highlight
steps in discrete 30° detents (no sliding), click commits the lit
sector. Needs a board revision (6 mm encoder + detents).

**Pros.** The strongest evidence base in the corpus — Crownboard-class
2.3 % error, glance-free, physical certainty; the in-car study's
distraction win; crown-class rotate+press speed.

**Cons.** It is a hardware item (a board revision), not a firmware
one; out of scope for the current product.

**Verdict.** the **right v2-board upgrade**; out of scope today.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=9"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Physical encoder + haptic detents — the operable 1:1 panel"></iframe>
   </div>

Off-hardware — the silicon is not there
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The four off-hardware methods are text cards in the lab (there is no
panel to operate — no sensor to drive one):

- **Voice (INFEASIBLE).** No microphone on the board (no on-board AEC
  mic path on this S3 design). The phone has one — the Apple Watch
  hands off.
- **Camera / OCR (INFEASIBLE).** No camera on the board, so there is
  nothing to point at the password.
- **QR / NFC / BLE handoff (INFEASIBLE NOW).** No NFC and no camera to
  read a QR. BLE peripheral is architecturally possible but means a
  companion app, GATT, pairing and a security review for a
  once-per-network task.
- **T9 dictionary decode (REJECTED · task fit).** The T9 speed win
  comes from 1-press-per-character against a **word database**.
  Passwords are arbitrary: a 200–500 KB word list buys almost nothing
  here. The mechanics T9 provides are exactly what the multi-tap ring
  (panel #2) generalizes without the database.

The decisions, recorded
-----------------------

The verdict, restated as the record:

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

The HCI literature (C-QWERTY, COMPASS, WrisText, Crownboard,
Tap'nSwipe, the in-car dial study) is listed with the evidence
report at :doc:`the sources <reference/sources>`; the reference
section turns from the *user-facing* design to the *developer-facing*
interface: the Python API the examples and the gate are built on
(:doc:`the Python API <reference/python-api>`), the C API the device
compiles (:doc:`the C API <reference/c-api>`), the build system and
Kconfig that keep two boards buildable from one tree
(:doc:`the build system <reference/build-system>`), and the glossary
and references (:doc:`the glossary <glossary>`).
