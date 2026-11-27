The round keyboard lab
======================

.. brief:
   Reader: a developer or interaction designer who wants to know why the clock types passwords on a wheel and what else was considered.
   They already know: the shipped wheel from the round keyboard page, or at least what a touch screen can sense.
   After this page they can: compare the thirteen text-entry methods by feel and by evidence, and see why each was kept, shipped, held or rejected.
   Page type: explanation.

This page is the design study behind the clock's keyboard.
It compares thirteen ways to enter text on the 35 mm round glass, each with the pros and cons that decided its fate.
Nine run on this hardware and are embedded below as operable widgets at 1:1 scale; four need sensors the board does not have and stay text cards.
The one that shipped, the v2 12-sector hybrid wheel, has its own page and widget: :doc:`the round keyboard <keyboard>`.

The short version
-----------------

The wheel stays the primary method: coarse angular targets with a deliberate commit are the most reliable touch model on a panel this small.
Three cheap firmware additions are recommended on top of it: a 12-sector multi-tap ring, a hold-to-commit dwell, and an auto-scan accessibility setting.
A physical rotary encoder has the strongest evidence, but it needs a new board.
Everything else (a QWERTY grid, voice, OCR, QR/NFC/BLE handoff, one-stroke glide, word prediction) does not fit the hardware or does not fit arbitrary passwords.
The table in `The decisions, recorded`_ lists every verdict.

The task and the constraints
----------------------------

You enter a password a handful of times in the product's life, with one hand, with the device in front of you and the display unmasked.
Speed is secondary.
A wrong character costs a retry; a character entered twice gives a corrupted password.
So every decision below is judged on one axis: certainty over speed.

The hardware constraints are the other half of the design space:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Constraint
     - Consequence
   * - 466×466 px, ~35 mm disk (~266 PPI)
     - A 4×4 QWERTY grid lands at ~0.75 mm per key, about 10× below the 8–10 mm fingertip. Fine tapping is not in the space.
   * - CST9217 capacitive touch, no haptics
     - No detents and no confirmation ticks. The display is the only feedback channel, so the live preview carries the design.
   * - No microphone, no camera, no NFC, no BLE peripheral in this product (Wi-Fi only)
     - Voice, OCR and every read-side handoff (QR/NFC) are infeasible, not merely expensive.
   * - QMI8658C IMU already sampled (the 90° roll tracker)
     - A twist gesture costs no hardware. But a twist is a roll change, so the gesture and the re-orientation are the same signal (a disqualifying conflict, below).
   * - ESP32-S3 + LVGL 9.6 software renderer
     - The keyboard must be a few hundred lines over the existing ring geometry, hit-test and font, not a new input stack.

What the literature says
------------------------

A deep-research pass over the HCI literature (CHI, UIST, MobiSys, SAGE, IJHCI, IEEE, plus NHTSA/TRB and vendor documentation) is committed at ``docs/research/circular-keyboard-alternatives.md``.
Its verdict:

   Keep the sweep wheel as the primary entry method.
   The literature supports the family: coarse angular targets with sweep or press-release are the most reliable touch model on a ~35 mm panel.
   Add two cheap alternatives: a 12-sector DTMF-style multi-tap ring for the 123/#$ registers (30° targets, 1–4 presses, no re-aim) and a hold-to-commit dwell option on the outer zones.
   Everything else (QWERTY grid, voice, OCR, QR/NFC/BLE handoff, one-stroke gestures, word prediction) is infeasible on this hardware or a bad fit for arbitrary password strings.
   A physical rotary encoder is the strongest evidence-backed upgrade, but it is a hardware-revision item, not a firmware one.

The findings the verdict rests on:

1. A ring of keys on the screen edge is a published, studied design for exactly this form factor.
   C-QWERTY (2020) arranges alphanumeric keys along the edge of a circular smartwatch in QWERTY order.
   It is the only published design that directly addresses a round keyboard on a round touch display.
2. The companion designs all use a few large angular sectors plus a commit interaction, never 26 fine taps on the rim.
   COMPASS (CHI 2017) is a multi-cursor circular keyboard with bezel rotation; WrisText (CHI 2018) uses a wrist whirl to a circular alphabetical layout.
   Crownboard (CHI 2023) uses crown-rotation scanning, with 2.3 % / 0.53 % error rates, the best in the corpus.
   RingByte (UIST 2025) uses a rotating outer ring with a neural decoder.
3. On small watch screens, fewer bigger targets win.
   Smartwatch QWERTY tapping measures 18–20 WPM with 14–15 % WER; trace measures 29–33 WPM.
   A 9-sector × 4-character swipe keyboard improved accuracy over QWERTY at a marginal speed loss (Tap'nSwipe, IJHCI 2025).
4. Even with statistical decoding, fitted QWERTY grids sit at the resolution limit of a ~35 mm disc (WatchWriter, FingerText, "Is it too small?").
5. The in-car studies explain why coarse angular input wins on small round hardware.
   An on-road CHI 2017 study found target accuracy high for all methods (>94 %); the case for the physical dial rests on glance and distraction metrics and on physical certainty, not on per-target accuracy.
   For a clock, the wheel's value is certainty and low error, not raw speed, which is what a password entry needs.
6. Dwell (hold-to-commit) tops out around 20 WPM (CHI 2025) and needs training.
   That is enough to ship it as a fallback, not as the primary method.
7. Direct 2D pointing beats circular layouts on raw speed (T9VR, Sector Input).
   That counter-evidence holds when the panel can host a QWERTY, which a 35 mm disk cannot.
   The P4's 720 px disk is large enough to make a compact QWERTY worth a real benchmark on a future board; this is noted, not done.

.. _thirteen-sketch-lab:

The thirteen in one picture
---------------------------

The sections further down walk the methods one at a time.
The four figures here show the field they live in, drawn from the three tracked sources this page must agree with:

- the research report's comparison table (``docs/research/circular-keyboard-alternatives.md``);
- the lab's own ``CARDS``/``OFF`` tables (``docs/_static/keyboard/round-keyboard-lab.html``);
- the shipped wheel (``components/ui/screens.c``).

The figures follow the house pattern: one black-box overview, then one detail sketch per sealed box.
Steel-blue arrows are the literature vouching for a method.
Dashed red arrows are a hardware constraint striking a method down, with the reason on the wire; dotted grey arrows are what the three SHIP methods cost the firmware.
Each method's colour is its recorded verdict.
Violet dashed arrows are what the shipped wheel kept from the alternatives it rejected: multi-tap's coarse press, the glide's double-entry mode made impossible, dwell's 400 ms hedge, and the in-tree ``LONG_PRESSED`` that the centre back-hold already runs.
The same facts computed, with each published WPM and every back-of-envelope second converted to characters per minute when the figure is built, are :ref:`the certainty ladder <certainty-ladder-perf>`.

.. graphviz:: _graphs/keyboard-lab.dot
   :align: center
   :alt: The study as black boxes: the literature and six constraints feed the thirteen methods, which feed the shipped wheel, with what the wheel kept marked on the edge.
   :caption: The thirteen in black boxes. Three claims hold the field up; the text below the figure states them.

Three claims hold the overview up, and they are this page's own:

1. All thirteen live on one committed page: nine 466×466 SVG widgets running the device's own timings (multi-tap 700 ms, dwell 400 ms, scan 1 s) plus four text cards.
   This page's prose is the record; the widgets are the reference.
2. Nothing has been measured on this panel.
   Every speed is a corpus number (wrist or phone) or the report's own back-of-envelope estimate.
   The report's "4-8 CPM" gloss contradicts its own seconds (its wall-clock pair is 16-32 CPM); the certainty ladder draws the arithmetic, not the gloss.
3. Every SHIP method is pure firmware over the wheel's existing plumbing; the corpus's best-supported method, the physical encoder, is a hardware revision.

The constraint list is the report's own (line 12: "CST9217 capacitive touch, no haptics, no microphone, no camera, no NFC"), the task is its line 16 ("8-64 arbitrary chars"), and the size arithmetic is its lines 250-251: a 4×4 grid puts "≈ 0.75 mm per key — 10× below the ~8-10 mm fingertip".

.. graphviz:: _graphs/keyboard-lab-corpus.dot
   :align: center
   :alt: The literature opened: twelve studies in a column, with arrows to the nine operable methods coloured by verdict.
   :caption: The corpus opened (37 sources). The text below the figure gives the headline numbers the nodes leave out.

The headline numbers the corpus nodes leave out to save width:

- C-QWERTY 2020 is finding 1, "a soft keyboard whose keys run" around the bezel.
- COMPASS CHI 2017 brought the hot-sector idea ("last-typed sector hottest").
- WrisText CHI 2018, Crownboard CHI 2023 and RingByte UIST 2025 stand for "a small number of large angular sectors" (report lines 47-48, 185).
- Crownboard's 2.3 % / 0.53 % errors are the corpus's best (findings 4/6). They vouch both for auto-scan (scan + press) and for the encoder card.
- Tap'nSwipe IJHCI 2025's 9×4 swipe beats QWERTY accuracy on small watches.
- SAGE 2019 carries tap 18-20 WPM at 14-15 % WER, trace 29-33 WPM, and handwriting at 25 % WER.
- The dwell ceiling of ~20 WPM (CHI 2025) demotes dwell to a fallback.
- The in-car CHI 2017 study found "> 94 % accuracy" for all methods; the dial's case is glance and distraction.
- The WatchWriter and FingerText rescues assume a wrist and fingers.
- T9-class big keys keep multi-tap alive (finding 13, MacKenzie CHI 2000).
- T9VR and Sector Input are finding 7: direct 2D pointing wins when a QWERTY fits, which the 35 mm disk cannot do.

The comparison table's verdict cells colour the methods: G and H infeasible, I "rejected now" (the lab cards it INFEASIBLE NOW; both spellings are kept).

.. graphviz:: _graphs/keyboard-lab-constraints.dot
   :align: center
   :alt: The constraints opened: six hardware and task constraints in a column, with dashed red strikes on QWERTY, twist, the three sensor-less methods and the two dictionary methods, and the firmware cost of the three SHIP methods.
   :caption: The constraints opened (report lines 12/16/250-251 and `The task and the constraints`_). The text below the figure walks each strike.

Each constraint in the figure strikes specific methods:

- The 35 mm disk: a 4×4 QWERTY at "≈ 0.75 mm per key" against an 8-10 mm fingertip strikes down the fitted grid.
- The QMI8658C disqualifies the twist card, because a wrist twist is a roll change: the roll the clock already tracks for the 90° step.
- "No microphone / no camera / no NFC" leaves voice, OCR and the QR/NFC/BLE handoff with no sensor to drive a widget.
- The arbitrary-character task (8-64, rare, one-handed) defeats the dictionary pair: nothing repairs an over-swipe, and passwords defeat the word list.
- The firmware budget prices the three SHIP methods in the lab's own Cost rows: multi-tap "few hundred LOC · 2-4 KB", dwell "tens of LOC", scan "low — scan only, no decoder".
  All three sit on the wheel's existing ring geometry: ``WHEEL_SHARE`` 360.0f / 26.0f, ``WHEEL_N_SECTORS`` 12, the twelve frequency groups, ``WHEEL_PW_MAX`` 63; the lab mock's own ring runs ``const C = 233, LARC = 360 / 26``.

.. graphviz:: _graphs/keyboard-lab-methods.dot
   :align: center
   :alt: The methods opened: the twelve non-wheel methods in a column coloured by verdict, with what the wheel kept from multi-tap, dwell and glide, and the HOLD from swipe, all landing on the shipped wheel.
   :caption: The methods opened. Fill colour is the lab's recorded verdict; the text below the figure gives the sources.

The fill colours come from the lab's ``CARDS`` ranks 1..9, with chips keep/ship/ship/ship-setting/hold/rej/rej-primary/exp/v2, plus the ``OFF`` trio and the T9 card (13 in total).
The widgets run the device's own timings: multi-tap ``const T = 700`` ms, the dwell checks ``< 400 / >= 400``, and the scan step ``Math.floor((t - t0) / 1000) % 12``.
The shipped keys are the house circle-check for commit and circle-xmark for discard (the wheel section of :cfile:`components/ui/screens.c`); the operable mock's bottom band shows the proposal's ⊗ commit and trash-can discard.
The wheel's own machine (handlers, hit-test, keys) is ``wheel-commit.dot`` on :doc:`the round keyboard <keyboard>`.
What each method would cost the finger in characters per minute is :ref:`the certainty ladder <certainty-ladder-perf>`.

The operable lab
----------------

A ranking by citation tells you which method the papers favour; operating the methods tells you how they feel.
The lab (``docs/_static/keyboard/round-keyboard-lab.html``) renders all 13 text-entry methods at true 1:1 scale.
It is a single self-contained page in plain HTML, SVG and JavaScript, with no build step and no server.
It is committed to the repo so the documented choices and the operable reference cannot drift apart.
Every on-hardware mock is a 466×466 SVG with the device's panel geometry and its black-on-black AMOLED palette.
The feasible methods are operable in the browser, with pointer events and the device's timing constants (multi-tap 700 ms, dwell 400 ms, auto-scan 1 s), so you can compare the feel before writing any C.

This page does not embed the lab as one big iframe.
Each method's operable disk is embedded on its own in its section below, through the lab's ``?only=N`` embed mode: one widget per panel with its "try it" line and no duplicate prose.
The text on this page (what it is, pros, cons, verdict) is the record.
The four off-hardware methods have no operable disk, because there is no sensor to drive one; they stay text cards here and in the lab's lower grid.
To see all thirteen side by side, with the verdict strip, the evidence rows and the off-hardware cards, open the full lab:

.. raw:: html

   <p style="margin:0 0 18px;"><a href="_static/keyboard/round-keyboard-lab.html"
   target="_blank" rel="noopener"><strong>Open the full lab in a new tab
   ↗</strong></a></p>

The nine on-hardware methods
----------------------------

The methods appear in the lab's verdict-strip order.
Each has what it is, the pros for it, the cons against it, and the verdict.
The evidence rows on the widgets come from the research report.

1 · Sweep wheel v2 — 12-sector hybrid
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: The design that shipped.
   Twelve coarse press sectors are sized to their letter groups, with every letter owning an equal 360/26 of the ring.
   On touch-down a reveal shows only the pressed sector's options on one radius; a DTMF-style centre preview shows the selected letter; the letter commits on release only.
   Four 60° shift keys sit on the top arc and commit/discard in the bottom 120°; a centre tap is backspace.
   The mock's bottom band shows the proposal's ⊗ commit and trash discard; the firmware pairs the circle-check with commit and the circle-xmark with discard (:doc:`the round keyboard <keyboard>`).
:Pros: Coarse targets: the 27.7°/41.5° sectors are well above a fingertip.
   The reveal's equal-share boxes make the pick unambiguous.
   Release-only commit makes double entry impossible by construction.
   The large centre preview gives certainty without lifting the finger.
   The grouping is fixed across all four registers, so the sector boundaries never move.
   It costs a few hundred lines over the existing ring geometry.
:Cons: It has the slowest absolute entry of the operable panels, the price of certainty over speed.
   The report prices it two ways that do not agree.
   Its wall-clock pair, a 16-character password in 30–60 s, is 16–32 CPM; the 0.7–1.5 s it gives per commit motion alone implies a floor of 40–86 CPM.
   The lab card's "4–8 CPM" gloss follows from neither (it would mean 7.5–15 s per commit) and is quoted here only as the report's own; :ref:`the certainty ladder <certainty-ladder-perf>` re-derives the arithmetic, bars and all.
   The sweep's feel on the CST9217's real glass is still to be checked on the device.
   While you press, the preview replaces the entry text; the entry reappears on release.
:Verdict: KEEP · v2, the approved direction. The shipped design's record and widget are on :doc:`the round keyboard <keyboard>`.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=1"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Sweep wheel v2 — the operable 1:1 panel"></iframe>
   </div>

2 · 12-sector DTMF multi-tap
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: Press a sector 1–4 times; each press picks the next character down the sector's column, shown as a dot per press.
   The thin arc is the 700 ms window; the timeout commits, and releasing early also commits.
:Pros: 30° targets, 1.4× the width of a two-letter reveal box.
   No re-aim after the first press, because multi-tap absorbs the aiming, and the timeout absorbs jitter.
   Cheap: a few hundred lines and 2–4 KB of flash.
   This is the coarse-press and centre-preview model v2 inherits.
:Cons: Slowest per character for common letters (up to 4 presses).
   The 700 ms window is not tuned on real glass (the research's open micro-study).
   The character order within a sector is a configuration-table choice, not a familiar one.
:Verdict: SHIP as an addition, the best cost/benefit; take the 123/#$ registers first.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=2"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="12-sector DTMF multi-tap — the operable 1:1 panel"></iframe>
   </div>

3 · Hold-to-commit dwell
^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: Press and hold 400 ms on a letter; the arc fills, then the letter commits.
   Sliding re-targets and restarts the timer; releasing early cancels.
   There is no sweep motion at all.
:Pros: No new error class: the same 27° target and live preview as the wheel.
   Zero sweep motion, so it works for someone who cannot sweep reliably.
   Tens of lines of code.
   It also hedges the open real-glass sweep check, as a fallback that needs no new feel data.
:Cons: Dwell tops out at ~20 WPM (the CHI 2025 ceiling) and needs training.
   A 400 ms hold on a 35 mm panel is tiring over a 64-character password.
:Verdict: SHIP as the default fallback.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=3"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Hold-to-commit dwell — the operable 1:1 panel"></iframe>
   </div>

4 · Auto-scan, Crownboard-style
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: The lit sector steps clockwise at about one step per second (the serial floor, shown live in the widget).
   Tapping anywhere commits the lit character.
   The decoder is deliberately left out, because it only helps dictionary words.
:Pros: 0.53–2.3 % error by construction, the best in the corpus.
   No aiming at all.
   Low implementation cost (scan only, no decoder).
:Cons: The scan period sets a floor of about 1 s per character.
   The tap must land in the lit sector's window.
   It is an accessibility aid, not a primary method.
:Verdict: SHIP as an accessibility setting, not the primary.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=4"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Auto-scan, Crownboard-style — the operable 1:1 panel"></iframe>
   </div>

5 · 9×4 swipe sectors
^^^^^^^^^^^^^^^^^^^^^

:What it is: Press in the ring, drag, and release.
   The release direction (out, left, in or right) picks one of the sector's 4 characters (Tap'nSwipe, IJHCI 2025).
:Pros: Better accuracy than QWERTY on small watches at a marginal speed loss.
   29–33 WPM measured in the trace/tap literature (SAGE 2019).
   One press and release per character.
:Cons: Medium cost: gesture classification plus a new layout table.
   A drag under 15 px reads as a tap, a whole error class at the edge of the gesture.
   It duplicates what multi-tap and the sweep already cover.
:Verdict: HOLD; revisit only if multi-tap proves unlearnable.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=5"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="9×4 swipe sectors — the operable 1:1 panel"></iframe>
   </div>

6 · Fitted QWERTY 6×6
^^^^^^^^^^^^^^^^^^^^^

:What it is: Tap the keys of a 6×6 grid of 60 px (≈ 4.5 mm) keys, clipped by the disk at the corners.
   The widget lets you feel why it was rejected.
:Pros: The familiar layout.
   Direct 2D pointing is the fastest input on panels that can host it; the T9VR / Sector Input counter-evidence holds above ~720 px (the P4 note, finding 7).
:Cons: 4.5 mm keys against an 8–10 mm fingertip.
   14–15 % WER measured at watch key sizes.
   High implementation cost for a rejected outcome.
:Verdict: REJECTED: the keys are well below a fingertip on a 35 mm disk.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=6"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Fitted QWERTY 6×6 — the operable 1:1 panel"></iframe>
   </div>

7 · Ring glide / one-stroke
^^^^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: Press and sweep along the ring; every letter you cross commits at once.
   The widget makes the over-swipe risk visible.
:Pros: 29–33 WPM measured on QWERTY with a dictionary (SAGE 2019).
   One continuous gesture per word.
:Cons: It needs a word model to recover from an over-swipe, and a password is arbitrary, so there is none.
   Deliberate slow motion enters letters twice, and that failure cannot be designed out.
   Release-only commit is exactly the property that removes it.
:Verdict: REJECTED as a primary method for arbitrary strings.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=7"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Ring glide / one-stroke — the operable 1:1 panel"></iframe>
   </div>

8 · IMU twist
^^^^^^^^^^^^^

:What it is: Drag to twist the body; the ring rotates with it, and the badge fires at every 90° boundary crossing (WrisText, CHI 2018, the wrist-whirl family).
:Pros: The hardware is already there: the QMI8658C is sampled for the roll tracker.
   It belongs to a published gesture family for circular input.
:Cons: No published speed figure.
   The disqualifier is structural: a twist is a roll change, so the gesture and the device re-orientation are the same signal, and the 90° roll tracker needs that signal.
:Verdict: EXPERIMENTAL at best; disqualified today.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=8"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="IMU twist — the operable 1:1 panel"></iframe>
   </div>

9 · Physical encoder + haptic detents
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:What it is: Drag vertically or use the mouse wheel; the highlight steps in discrete 30° detents with no sliding, and a click commits the lit sector.
   It needs a board revision (a 6 mm encoder with detents).
:Pros: The strongest evidence base in the corpus: Crownboard-class 2.3 % error, glance-free use and physical certainty.
   It has the in-car study's distraction win and crown-class rotate-and-press speed.
:Cons: It is a hardware item (a board revision), not a firmware one, and out of scope for the current product.
:Verdict: The right upgrade for a v2 board; out of scope today.

.. raw:: html

   <div style="max-width:520px; margin:4px auto 20px;">
   <iframe src="_static/keyboard/round-keyboard-lab.html?only=9"
           style="width:100%; min-height:560px; border:0; background:transparent;"
           loading="lazy" title="Physical encoder + haptic detents — the operable 1:1 panel"></iframe>
   </div>

Off-hardware methods: the sensors are not there
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The four off-hardware methods are text cards in the lab, because there is no sensor to drive an operable panel.

Voice (INFEASIBLE)
   There is no microphone on the board (no on-board AEC mic path on this S3 design).
   A phone has one; the Apple Watch hands voice entry off to it.

Camera / OCR (INFEASIBLE)
   There is no camera on the board, so there is nothing to point at the password.

QR / NFC / BLE handoff (INFEASIBLE NOW)
   There is no NFC and no camera to read a QR code.
   A BLE peripheral is architecturally possible, but it means a companion app, GATT, pairing and a security review for a once-per-network task.

T9 dictionary decode (REJECTED · task fit)
   The T9 speed win comes from one press per character against a word database.
   Passwords are arbitrary, so a 200–500 KB word list buys almost nothing here.
   The multi-tap ring (panel #2) gives the same mechanics without the database.

The decisions, recorded
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 26 42 32

   * - Decision
     - The choice
     - Why
   * - KEEP
     - the sweep wheel (two-frame, then v2) as the primary method
     - the literature's family: coarse angular targets with a deliberate commit; release-only commit makes double entry impossible
   * - SHIP (additions)
     - 12-sector DTMF multi-tap (123/#$), hold-to-commit dwell, auto-scan setting
     - a few hundred lines each over the wheel's plumbing; dwell is also the hedge against the open real-glass check
   * - HOLD
     - 9×4 swipe sectors
     - duplicates what multi-tap and the sweep already do; revisit only if multi-tap proves unlearnable
   * - REJECT
     - fitted QWERTY, ring-glide primary, T9 dictionary, voice/OCR, QR/NFC/BLE handoff
     - targets below a fingertip / no word model for arbitrary strings / no sensors / a companion app is too much for a rare task
   * - V2 BOARD
     - physical rotary encoder with haptic detents
     - the strongest evidence base in the corpus (Crownboard-class 2.3 % error; the in-car distraction win), but a hardware item, not a firmware one

Two of the research's own questions stay open.
First, whether C-QWERTY's QWERTY-versus-alphabetical effect size (behind a bot-blocked full text) justifies re-stringing the ring away from the grouped alphabetical layout.
Second, whether the DTMF ordering is familiar to today's users or only to people who grew up with 20th-century phones.
A 30-second user test can settle each; neither blocks the shipped design.

Where to go next
----------------

- :doc:`The sources <reference/sources>` lists the HCI literature (C-QWERTY, COMPASS, WrisText, Crownboard, Tap'nSwipe, the in-car dial study) with the evidence report.
- :doc:`The round keyboard <keyboard>` is the shipped wheel's design record.
- For the developer-facing interfaces: :doc:`the Python API <reference/python-api>` that the examples and the gate are built on, :doc:`the C API <reference/c-api>` that the device compiles, and :doc:`the build system <reference/build-system>` that keeps the two clock boards buildable from one tree, with the knob's sibling project linking the same shared components. Unfamiliar terms are in :doc:`the glossary <glossary>`.
