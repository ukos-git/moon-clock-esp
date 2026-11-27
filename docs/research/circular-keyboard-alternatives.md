# Deep Research: Alternatives to the Circular Wheel Keyboard on a Small Round Touch Panel

Date: 2026-11-01 · Method: Firecrawl search + scrape across HCI proceedings
(CHI, UIST, MobiSys, SAGE, IJHCI, IEEE), open-access PDFs (author copies on
university/author servers), the Semantic Scholar API, vendor and standards
literature (NHTSA/TRB, Nordic, Apple/Google support docs), and the project's
own wheel implementation for the cost model. ResearchGate/ACM/SAGE full texts
are bot-blocked; abstracts were taken from open mirrors and the Semantic
Scholar API, and that is marked wherever a claim rests on an abstract only.

Scope question: the device is a 466×466 RGB565 AMOLED (~35 mm disk, ~266 PPI),
CST9217 capacitive touch, **no haptics, no microphone, no camera, no NFC, no
BLE peripheral in this product** (Wi-Fi only), QMI8658C IMU, ESP32-S3 + LVGL
9.6 SW renderer, ~647 KB free flash (factory 2048 K, binary 1,450,176 B at
23adecd). The task it must serve is **rare, one-handed, and accuracy-first**:
enter a Wi-Fi password (8–64 arbitrary chars) on the table/wrist, physically
present, unmasked display. Speed is secondary; a wrong character costs a
re-try. The in-tree baseline is the two-frame wheel (§7.13 of the document):
inner 26-slot ring grouped in threes (wayfinding), touch-down reveals an outer
ring where the letter under the finger is a 2× arc (27°, nearest-centre
resolution), sweep + release commits one letter, 4-key register arc, centre
commit/backspace.

## Verdict

**Keep the sweep wheel as the primary entry method — the literature supports
the family (coarse angular targets + sweep/press-release are the most reliable
touch model on a ~35 mm panel) — and bolt on two cheap alternatives: a
12-sector DTMF-style multi-tap ring for the 123/#$ registers (30° targets, 1–4
presses, no re-aim) and a hold-to-commit dwell option on the outer 2× zones.
Everything else (QWERTY grid, voice, OCR, QR/NFC/BLE handoff, one-stroke
gestures, word prediction) is infeasible on this hardware or a bad fit for
arbitrary password strings. A physical rotary encoder is the strongest
evidence-backed upgrade, but it is a hardware-revision item, not a firmware
one.**

## Executive Summary

The HCI literature on round-display text entry is small but consistent:
circular layouts are the natural fit for circular panels, and the dominant
designs cluster around **coarse angular targets selected by rotation
(bezel/crown/twist) or by sweep-and-release**, rather than fine taps. The
closest analogue in the tree is C-QWERTY (2020), a soft keyboard whose keys run
along the _edge of a circular smartwatch screen_ in QWERTY order — the same
"ring of keys" concept as this project's wheel, and the only published design
that directly addresses "round keyboard on round display" for a touch device.
Its companion findings (COMPASS, CHI 2017; WrisText, CHI 2018; Crownboard, CHI
2023; RingByte, UIST 2025) all use **a small number of large angular sectors
plus a commit interaction** (cursor-rotation, wrist whirl, crown rotation, ring
rotation) — none attempt 26 fine taps on the rim.

Smartwatch touch studies (SAGE 2019; Tap'nSwipe 2025; FingerText 2021) add the
important nuance that **on ~35 mm touch panels, gesture/trace and
reduced-target-count inputs beat fine tapping**: trace (glide) reached 29–33
WPM vs 18–20 WPM for tap on a smartwatch QWERTY, and 9-sector × 4-character
swipe keyboards _improved_ accuracy over QWERTY at only marginal speed loss. A
fitted QWERTY grid on a 35 mm disc is the one design the literature uniformly
cannot save: keys land below fingertip resolution even with statistical
decoding (WatchWriter, FingerText, "Is it too small?").

The in-car line points the same way for _why_ coarse angular input wins on
small round hardware: an on-road CHI 2017 study found target accuracy high for
all methods (>94%) but with direct tapping the _fastest_ and pressure-touch the
_slowest_, while the physical dial's case is made on **glance/distraction
metrics and physical certainty**; follow-on work (NHTSA-cited, TRB, LYRIQ
HCII 2026) reports rotary controls cutting visual-manual distraction (up to 47%
in one secondary source — low confidence) and helping drivers keep eyes on the
road for simple control tasks. The transfer to a wrist-worn clock is: _the
wheel's value is certainty and low error, not raw speed_ — which is exactly
what a password entry wants.

For this exact board, the cost model is decisive: the wheel is already shipped
and uisim-verified; a 12-sector multi-tap mode and a hold-to-commit dwell are
each a few hundred lines reusing the wheel's geometry, hit-testing and font;
every "fancier" alternative either needs a sensor the board lacks (mic, camera,
NFC), a second device (phone companion app over BLE — high cost for a rare
task), or a hardware revision (rotary encoder). Ranked recommendations are in
§Recommendations.

## Key Findings

1. **A ring-of-keys on the screen edge is a published, studied design for
   exactly this form factor.** C-QWERTY
   (<https://www.researchgate.net/publication/337284352_C-QWERTY_a_text_entry_method_for_circular_smartwatches>)
   arranges alphanumeric keys along the edge of a circular smartwatch in QWERTY
   order and studies how much the QWERTY _ordering_ (vs the circular layout
   itself) helps in early sessions — full text is bot-blocked, so the effect
   size is unknown (Open Question 1).
2. **Multi-cursor circular keyboards with dynamic target placement exist and
   work** (COMPASS, CHI 2017, <https://dl.acm.org/doi/10.1145/3025453.3025454>;
   open PDF:
   <https://pi.cs.tsinghua.edu.cn/lab/papers/COMPASS_Xin_Yi_CHI2017.pdf>):
   several cursors on a circular keyboard, each re-positioned during typing to
   minimize rotation distance. The transferable idea: _place the next likely
   target where the finger/eye already is_ — on a password, "last-typed sector
   stays hottest" is a zero-cost approximation.
3. **Wrist-rotation to a circular alphabetical keyboard is viable** (WrisText,
   CHI 2018, <https://dl.acm.org/doi/10.1145/3173574.3173755>; author notes:
   <https://www.jungong.me/blog/wristext>): 6-direction whirls select a key in
   a circular alphabetical layout. Note the device is worn on the wrist — the
   very gesture the clock's 90° roll tracker consumes.
4. **Crown-rotation scanning keyboards reach very low error** (Crownboard, CHI
   2023, full PDF:
   <https://grakhmetulla.com/papers/Rakhmetulla_CHI2023_Crownboard.pdf>): 8
   alphabetical zones scanned clockwise, crown-press to select, statistical
   decoder with word suggestions. Measured error rates: **2.3 % (manual vs
   automated scan) and 0.53 % (shortest-path vs clockwise scan)**; manual
   rotation beat automated scan on speed. The touch-drag version of "manual
   scan" is precisely this project's sweep.
5. **Circular layouts beat rectangular for angular search** (gaze-based
   circular keyboard,
   <https://link.springer.com/chapter/10.1007/978-3-032-30819-1_55>, 31
   participants, p = 0.042) — Listing's-Law geometry: a ring's targets are
   equidistant from the selection point, so angular selection is the native
   motion on a round panel.
6. **On small watch screens, fewer bigger targets win.** Tap'nSwipe (IJHCI
   2025, <https://www.tandfonline.com/doi/full/10.1080/10447318.2025.2490709>):
   9 areas × up to 4 chars, swipe-in-area to pick the char — _better_ accuracy
   and less correction than QWERTY at marginal speed loss; the authors note
   QWERTY shows higher keystrokes-per-character on a small smartwatch.
   Smartwatch QWERTY tap measures 18–20 WPM with 14–15 % WER; trace 29–33 WPM;
   handwriting 18 WPM with 25 % WER (SAGE 2019,
   <https://journals.sagepub.com/doi/10.1177/0018720819891291>; walking-variant
   abstract:
   <https://soar.wichita.edu/items/e1c65c0f-9aa5-4351-b568-795a6711520d>).
7. **Even with statistical decoding, fitted QWERTY grids are at the resolution
   limit of a ~35 mm disc** (WatchWriter, CHI 2018:
   <https://www.researchgate.net/publication/301931220>; FingerText, CHI EA
   2021: <https://dl.acm.org/doi/10.1145/3411764.3445106> — optimized 5/10-key
   layouts need _intra-hand_ touches and only then gain ~10 WPM / ~39 % WER vs
   QWERTY while walking; "Is it too small?":
   <https://www.sciencedirect.com/science/article/pii/S1071581917300654>).
8. **Direct 2D pointing beats circular layouts on raw speed** (T9VR / Sector
   Input, VR, 24 subjects,
   <https://dlnext.acm.org/doi/pdf/10.1145/3385956.3422114>): controller
   pointing (a virtual QWERTY in space) was significantly faster and cleaner
   than the circular methods, which won only subjectively. Counter-evidence to
   "the wheel is optimal" — valid _when the panel can host a QWERTY_; it cannot
   at 35 mm.
9. **Radial selection with dynamically resized keys trades speed for
   security/usability** (arXiv 2601.05516, 2026,
   <https://arxiv.org/abs/2601.05516>, N = 30): keys expand to aid selection
   and the ring re-randomizes per keystroke. Confirms the "grow the target
   under the finger" pattern this project's 2× reveal uses.
10. **In-car: accuracy is a wash; speed and distraction differ by method** (Ng,
    Brewster, Beruscha, Krautter, CHI 2017, open abstract:
    <https://eprints.gla.ac.uk/134441/>): on-road list-targeting — physical
    dial, pressure-touch, plain touchscreen, ± haptics. **Target accuracy > 94
    % for all methods; direct tap fastest, pressure-based slowest;
    pressure-based drew the most glances** (though shorter ones). The physical
    dial's advantage shows up in distraction/glance metrics, not per-target
    accuracy.
11. **Rotary controls reduce driver distraction; the effect is task-dependent**
    (TRB record: <https://trid.trb.org/view/1462682> — rotary + haptics better
    than touchscreen for _simple continuous / shortcut_ tasks, mixed for
    complex ones; LYRIQ rotary usability, HCII 2026 poster:
    <https://link.springer.com/chapter/10.1007/978-3-032-30552-7_37>). One
    secondary source (low confidence — SEO-content site) claims "up to 47 %
    less visual-manual distraction" for knobs vs center-stack touchscreens,
    attributed to NHTSA + a 2023 MIT AgeLab study
    (<https://carinterior.alibaba.com/tips/knob-vs-touchscreen-safety>); the
    primary source could not be located.
12. **Crown interaction details matter** (CapCrown, PACMHCI 2026,
    <https://dl.acm.org/doi/10.1145/3812770>): one-finger crown rotation is
    faster than two-finger, but two-finger overshoots less and needs fewer
    corrections — relevant if a future board revision adds a physical knob.
    Touch-vs-crown on a _linear_ QWERTY watch keyboard (IEEE SMC 2025,
    <https://ieeexplore.ieee.org/document/11342892>) confirms both are viable
    channels to the same keyboard.
13. **T9/multi-tap survives because keys are big** (Wikipedia T9:
    <https://en.wikipedia.org/wiki/T9_(predictive_text)>): "those with larger
    fingertips still use the T9-based keyboard on smartphones, because key
    press accuracy increases with the larger screen area per key on a
    numeric-style 4×3 keyboard." Classic speed anchor: MacKenzie et al., CHI
    2000 (<https://www.yorku.ca/mack/chi00.html>); modern phone baseline: 36.2
    WPM / 2.3 % errors (37,370-volunteer study, CHI 2019,
    <https://dl.acm.org/doi/10.1145/3338286.3340120> /
    <https://userinterfaces.aalto.fi/typing37k/>).
14. **Dwell (hold-to-commit) tops out around 20 WPM and needs training** (CHI
    2025, <https://dl.acm.org/doi/10.1145/3706598.3713781>; feedback/dwell
    effects: <https://www.yorku.ca/mack/uais2006.html>). Finger-dwell on a
    large angular sector is a different (easier) animal than gaze-dwell, but
    the ceiling argues for offering dwell as a _fallback_, not the primary.
15. **Smart-ring input is a rotating-ring + decoding space** (systematic
    review, arXiv 2502.02459: <https://arxiv.org/abs/2502.02459>; RingByte,
    UIST 2025: <https://dl.acm.org/doi/10.1145/3746059.3747717> — rotating
    outer ring, stable inner ring, neural decoder; TypingRing, MobiSys 2015:
    <https://dl.acm.org/doi/10.1145/2742647.2742665>). The input primitive —
    _rotate a ring, commit, decode_ — is the same family as the wheel; the
    decoder needs a model the board cannot run (ML inference) for arbitrary
    strings anyway.
16. **Phone-side ecosystems do exactly what a companion app would do for this
    device** (Apple Watch text entry: on-screen keyboard, dictation, Scribble,
    and _typing on the paired iPhone_,
    <https://support.apple.com/guide/watch/enter-text-watch-...> — the handoff
    pattern; Gboard glide: <https://support.google.com/answer/2811346>; Nordic
    BLE HID keyboard + NFC pairing examples: <https://docs.nordicsemi.com/>,
    nRF5 SDK knowledge base). The pattern is: **type on the big screen, hand
    off once** — which assumes a phone app the user is willing to install for a
    rare task.

## Detailed Analysis

### The baseline, for the record

The shipped wheel commits one letter per _press–sweep–release_. No published
benchmark exists for this exact design (an on-device check of sweep feel is
still pending), so the cost model uses a back-of-envelope: 0.7–1.5 s per sweep
→ ~4–8 CPM → a 16-character password in roughly 30–60 s with zero errors when
done carefully. That is unimpressive as a _typing_ speed and irrelevant as a
_password-entry_ speed — the design bet, which the literature supports, is on
**error rate**: each commit is one deliberate motion over a 27° (≈ 90 px, ≈ 3.3
mm arc) target with live preview and no auto-commit on crossing, so the failure
mode is a slow retry, not a corrupted password.

### Alternative by alternative

**B. 12-sector DTMF-style multi-tap ring (123/#$ registers).** 12 sectors (30°
each — the widest angular targets a 12-target ring can have) in the familiar
DTMF arrangement; pressing a sector 1–4 times within a ~700 ms window commits
that sector's 1st–4th character (release or timeout commits). No re-aiming
between multi-tap presses. Speed: one press ≈ 0.3–0.5 s + inter-press timeout →
~1–1.5 s per multi-tap character, comparable to a sweep but _without_ the
re-aim-and-sweep motion for repeats. Accuracy: the 30° target is 1.4× the
current 2× zone and the timeout absorbs jitter; this is the direct application
of finding 13 (big keys + multi-tap). Cost: a new layout table + a press-count
state machine + one timeout, reusing the wheel's ring geometry, hit-test
(`LV_OBJ_FLAG_ADV_HITTEST` arcs) and 32 px font — a few hundred lines in
`screens.c`, ~2–4 KB flash. The known 2 s press orientation re-sync is
pose-stability-gated and the wheel is transform-aware, so a long multi-tap
sequence is benign (§7.13 "Known interaction"). **Verdict: the single best
cost/benefit addition; ship for 123/#$ first, then offer the same 12-sector
frame with T9-style 1-press-per-character + suffix decode for letters as an
option.**

**C. 9-sector × 4-char swipe sectors (Tap'nSwipe-style).** 9 radial areas, each
holding up to 4 chars; a short swipe _within_ an area picks the character by
direction. Directly applicable at the ring's radius (a 40° sector at r 168 is
~94 px ≈ 3.5 mm — swipe direction inside it is resolvable). Published result:
better accuracy

- less correction than QWERTY at marginal speed loss (finding 6). Cost: medium
  — per-sector gesture classification (direction + length thresholds), a larger
  layout table, and a new error class (swipe too short reads as a tap; too long
  reads as a sweep). It duplicates much of what B and the existing sweep
  already do. **Verdict: hold; revisit only if B's multi-tap proves unlearnable
  to users.**

**D. Fitted QWERTY grid (3×4 / 4×4 inside the disk).** At 466 px on a 35 mm
disk, a 4×4 grid of tappable keys is ~20 px ≈ 0.75 mm per key — 10× below the
~8–10 mm fingertip. The literature's rescues (statistical decoding:
WatchWriter; optimized small layouts + intra-hand touches: FingerText;
touch-point-distribution modelling: "Is it too small?") all assume _thumb +
fingers on a wrist_ and still sit at the limit. **Verdict: rejected on this
panel, full stop.**

**E. One-stroke / edge glide gestures (D-Shaped EdgeWrite,
<https://link.springer.com/chapter/10.1007/978-3-031-93845-0_1>; trace: finding
6).** Glide _along_ the alphabet ring (one continuous gesture passing a, b, c…)
is attractive for short strings, and trace is the fastest smartwatch method
(29–33 WPM, finding 6). But the wheel's release-only commit exists precisely
because crossing-commit **double-enters** on a deliberate slow motion, and
passwords are arbitrary (no word model to recover from an over-swipe; the 29–33
WPM figure was measured on a QWERTY with a spelling dictionary). **Verdict:
rejected as a primary; the existing press-sweep-release is the deliberately
safer member of this family.**

**F. Scanning (auto-scan) + statistical decoder (Crownboard/T9VR).** Auto-scan:
the ring cycles its highlight at a fixed rate, tap commits the lit sector.
Crownboard's measured errors (2.3 % / 0.53 %, finding 4) are the best in this
corpus, and auto-scan needs no gesture at all — but it serializes entry on the
scan period (~1 s/char floor) and the statistical decoder only helps for
dictionary words, not arbitrary passwords (the decoder is the part to skip; the
scan mode is the part to keep). **Verdict: ship the scan mode as an
accessibility fallback (a setting), no decoder.**

**G. Voice.** No microphone on the board; ESP32-S3 has no on-board AEC mic path
on this Waveshare design. **Infeasible.** (For the record: on phones, dictation
is the dominant watch input precisely because the _phone_ has the mic — Apple
Watch hands off, finding 16.)

**H. Camera / OCR.** No camera. **Infeasible.**

**I. QR / NFC / BLE handoff.** No NFC and no camera on the board, so the device
cannot _read_ a handoff; the device as BLE peripheral receiving credentials
from a phone app is architecturally possible (the S3 has BT, unused in this
product) but means: companion app, GATT service, pairing UX, security review —
for a task the user performs once per network. **Verdict: rejected now; note it
as the natural answer if the product ever gains a phone app for other
reasons.**

**J. Physical rotary encoder + haptic detents (new hardware).** The strongest
evidence base in the corpus (findings 4, 10, 11, 12): physical certainty,
glance-free operation, distortion-free angular positioning; CapCrown adds that
one-finger rotation is the fast mode. Not available on this board; a 6 mm
encoder on a future revision would turn the wheel into Crownboard-grade input
(rotate + press, 2.3 % error class). **Verdict: the right upgrade for a v2
board; out of scope for firmware.**

**K. IMU twist gesture (free hardware).** The QMI8658C is already sampled for
the 90° roll; a "twist the body" command to rotate the ring (WrisText's
primitive, finding 3) would need no new parts. Two problems: it directly
conflicts with the existing roll tracker (a twist _is_ a roll change — the
gesture and the re-orientation are the same signal), and a deliberate twist
while the device sits on a table is an unnatural motion. **Verdict:
experimental at best; the conflict with the roll tracker is disqualifying
today.**

**L. Hold-to-commit dwell.** Press and hold (400 ms, the in-tree `LONG_PRESSED`
constant) on a lit outer 2× zone commits that letter without any sweep. Dwell
tops out ~20 WPM (finding 14) — but again, for one 8–64 char string that
matters not; what it buys is a second, _gesture-free_ commit path for users who
find the sweep awkward, with zero new error class (the hold is on a 27° target
with live preview). Cost: extend the existing long-press handling from the
centre disc to the outer sectors — tens of lines. **Verdict: ship as the
default fallback; it also de-risks the "sweep feels wrong on real glass"
uncertainty of the pending device check.**

**M. T9 word prediction (dictionary decode).** The T9 speed advantage
(finding 13) comes from 1-press-per-character against a _word database_.
Passwords are arbitrary; a 200–500 KB word list (flash: affordable at ~647 KB
free, but the P4 port and future features will eat it) buys almost nothing for
this task. **Verdict: rejected as a task fit; the 1-press-per-character
_mechanics_ of T9 are exactly what B (multi-tap ring) generalizes without the
database.**

### Comparison table

| Alt.                       | Best published speed                | Accuracy evidence                               | On this board                  | Impl. cost               | Verdict                      |
| -------------------------- | ----------------------------------- | ----------------------------------------------- | ------------------------------ | ------------------------ | ---------------------------- |
| A. Sweep wheel (baseline)  | n/a (0.7–1.5 s/char est.)           | release-only commit; 27° target; live preview   | ✔ shipped, uisim-verified     | done                     | **keep**                     |
| B. 12-sector multi-tap     | T9-class (finding 13)               | 30° target, timeout absorbs jitter              | ✔ pure firmware               | ~few hundred LOC, 2–4 KB | **ship (123/#$ first)**      |
| C. 9×4 swipe sectors       | tap 18–20 / trace 29–33 WPM (watch) | better than QWERTY on small watches (finding 6) | ✔ firmware                    | medium                   | hold                         |
| D. Fitted QWERTY grid      | watch QWERTY 18–20 WPM              | 14–15 % WER at _watch_ key sizes                | ✘ 0.75 mm keys < fingertip     | high                     | **rejected**                 |
| E. Ring glide / one-stroke | trace 29–33 WPM (QWERTY + dict.)    | needs word model; double-entry risk             | partial                        | medium–high              | rejected (primary)           |
| F. Auto-scan (+ decoder)   | ~1 s/char floor                     | 0.53–2.3 % (Crownboard)                         | ✔ firmware                    | low (scan only)          | **ship as fallback setting** |
| G. Voice                   | dictation-class                     | —                                               | ✘ no mic                       | n/a                      | infeasible                   |
| H. Camera/OCR              | —                                   | —                                               | ✘ no camera                    | n/a                      | infeasible                   |
| I. QR/NFC/BLE handoff      | instant                             | —                                               | ✘ no NFC/camera; BLE app heavy | very high                | rejected now                 |
| J. Physical encoder        | Crownboard-class                    | 2.3 % class; distraction win (in-car)           | ✘ needs v2 board               | hardware                 | **right v2 upgrade**         |
| K. IMU twist               | n/a                                 | conflicts with 90° roll tracker                 | partial (IMU free)             | medium                   | experimental                 |
| L. Hold-to-commit dwell    | dwell ~20 WPM ceiling               | same target/preview as wheel                    | ✔ firmware                    | tens of LOC              | **ship**                     |
| M. T9 dictionary decode    | T9-class                            | needs word model; passwords arbitrary           | partial (flash ok)             | medium                   | rejected (task fit)          |

## Recommendations (ranked for this hardware)

1. **Keep the two-frame sweep wheel as the primary input.** Every serious
   design for round touch displays (C-QWERTY, COMPASS, Crownboard, RingByte,
   the secure radial keyboard) converges on coarse angular targets + a
   deliberate commit; the wheel's sweep-release with 2× reveal is the
   touch-native member of that family, and the in-car literature's lesson
   (certainty and low error beat raw speed for precision tasks) is exactly the
   right priority for password entry.
2. **Ship the 12-sector DTMF-style multi-tap ring for the 123/#$ registers**
   (30° targets, 1–4 presses, timeout commit). Biggest accuracy win where
   passwords are densest, smallest code, reuses the wheel's plumbing. Optional
   follow-on: the same frame with T9-style 1-press-per-character for the letter
   registers.
3. **Ship hold-to-commit dwell on the outer 2× zones** (400 ms hold) as the
   default fallback for users who find the sweep awkward — and as a hedge
   against the pending real-glass device check.
4. **Offer auto-scan (Crownboard-style, decoder-free) as a setting** for
   accessibility; 0.5–2 % error class by construction.
5. **Explicitly rejected:** fitted QWERTY (targets below fingertip), voice/OCR
   (no sensors), QR/NFC/BLE handoff (no hardware; companion app overkill for a
   rare task), ring-glide primary (double-entry risk on arbitrary strings), T9
   dictionary (arbitrary passwords).
6. **Board v2:** add a small physical rotary encoder + haptic detent; the
   in-car/crown literature makes it the strongest evidence-backed input upgrade
   available. Experiment with IMU twist only after the 90° roll tracker has a
   clean gesture-disambiguation story.
7. **Do not re-order the ring to QWERTY order on C-QWERTY's authority yet** —
   the effect size is behind the bot-blocked full text (Open Question 1); the
   grouped alphabetical ring is uisim-verified and matches the alphabetical
   layouts used by WrisText, Crownboard and RingByte.

## Contrarian Views And Risks

- **Direct pointing is faster, full stop.** T9VR (finding 8) and the Aalto 37 k
  study (36.2 WPM) show that when the panel can host a QWERTY, it is
  meaningfully faster than circular methods. The wheel's case rests entirely on
  the 35 mm constraint; if a future board is larger (the P4 is 720 px ≈ 54 mm
  at similar PPI), a fitted compact QWERTY (FingerText-class 5–10 keys)
  deserves a real benchmark here.
- **The in-car accuracy result cuts against the knob narrative.** Brewster 2017
  found >94 % accuracy for _all_ methods and the touchscreen _fastest_
  (finding 10) — the physical-dial win is a distraction/certainty story, not a
  per-target story. A touchscreen wheel inherits the same >94 % accuracy claim,
  and the 47 % distraction figure rests on a low-confidence secondary source.
- **C-QWERTY may argue for re-ordering the ring against the current design**
  (QWERTY order in the earliest sessions) — the opposite of recommendation 7.
  Unknown effect size; flagging so the next user test can settle it
  deliberately rather than by accident.
- **29–33 WPM trace numbers are QWERTY+dictionary numbers on a ~1.2″ square
  watch.** Extrapolating them to a 35 mm disk with an arbitrary-string task
  overstates the glide/swipe alternatives; the honest reading is "gesture input
  has a ~1.5× speed headroom over tap on small screens," which the existing
  sweep already captures.
- **Multi-tap has its own failure class**: an accidental double press or a
  dropped timeout commits the wrong character with no preview between presses —
  the sweep's live-preview advantage is lost in B. Mitigate with a per-press
  highlight + a short (150 ms) undo window before the timeout commits.
- **The baseline is unbenchmarked on real glass.** Every ranking above assumes
  the sweep feel holds up on the CST9217 (the pending device check). If the
  sweep misfires in practice, the _existing_ dwell and auto-scan fallbacks
  (recs 3–4) are the cheap recovery path.
- **Wrist-worn gestures conflict with the device's own pose behaviour**
  (WrisText's whirl vs the 90° roll tracker; K above). Any IMU-driven input on
  this product must negotiate with the orientation system, which currently has
  priority.

## Open Questions

1. C-QWERTY's QWERTY-vs-alphabetical effect size (full text bot-blocked;
   Studocu/ResearchGate mirrors are gated). Does the ordering gain justify
   re-stringing the ring?
2. What does the sweep _actually_ feel like on the CST9217 (the pending user
   device check)? The whole primary/fallback split depends on it.
3. Multi-tap timeout tuning: 500 vs 700 vs 900 ms between presses on a 266-PPI
   panel with the 90° roll in play — needs a real-glass micro-study (uisim can
   prototype the state machine, not the feel).
4. Is DTMF ordering actually familiar to this user population, or is it
   20th-century nostalgia? A 30-second user test with the 12-sector frame would
   settle whether "familiar" transfer is real.
5. If the product ever gains a phone companion (settings sync, updates), does a
   BLE credentials handoff (rec 5's "rejected now") become the right first
   feature of it?
6. Dwell ceiling on a _finger_ over a 27° capacitive target — is the 20 WPM
   gaze literature transferable, or does finger dwell clear 30+ WPM? (Cheap to
   measure with the in-tree timing hooks.)

## Sources

1. <https://www.researchgate.net/publication/337284352_C-QWERTY_a_text_entry_method_for_circular_smartwatches>
   — C-QWERTY: ring-of-keys on a circular smartwatch, QWERTY order (abstract).
2. <https://www.researchgate.net/publication/335154483_C-QWERTY_a_text_entry_method_for_circular_smartwatches_S>
   — C-QWERTY (S) companion study.
3. <https://dl.acm.org/doi/10.1145/3025453.3025454> — COMPASS (CHI 2017):
   multi-cursor circular keyboard, bezel rotation (abstract).
4. <https://pi.cs.tsinghua.edu.cn/lab/papers/COMPASS_Xin_Yi_CHI2017.pdf> —
   COMPASS full PDF (author copy).
5. <https://dl.acm.org/doi/10.1145/3173574.3173755> — WrisText (CHI 2018):
   wrist whirl to a circular alphabetical keyboard (abstract).
6. <https://www.jungong.me/blog/wristext> — WrisText author implementation
   notes.
7. <https://dlnext.acm.org/doi/pdf/10.1145/3385956.3422114> — T9VR + Sector
   Input: circular keyboard on a VR controller; direct pointing faster
   (abstract via Semantic Scholar).
8. <https://arxiv.org/abs/2601.05516> — Secure text entry via virtual radial
   keyboard with dynamically resized keys (2026, N=30).
9. <https://link.springer.com/chapter/10.1007/978-3-031-93845-0_1> — D-Shaped
   EdgeWrite: unistroke edge gestures for round watches.
10. <https://link.springer.com/chapter/10.1007/978-3-032-30819-1_55> —
    Gaze-based circular keyboard beats rectangular for search (31 participants,
    p=0.042).
11. <https://grakhmetulla.com/papers/Rakhmetulla_CHI2023_Crownboard.pdf> —
    Crownboard (CHI 2023) full PDF: 8-zone crown-scan keyboard; errors 2.3 % /
    0.53 %.
12. <https://dl.acm.org/doi/10.1145/3746059.3747717> — RingByte (UIST 2025):
    rotating smart-ring text entry with neural decoding (abstract via Semantic
    Scholar).
13. <https://dl.acm.org/doi/10.1145/2742647.2742665> — TypingRing (MobiSys
    2015): ring-on-finger surface tapping.
14. <https://arxiv.org/abs/2502.02459> — Computing with smart rings: systematic
    literature review.
15. <https://journals.sagepub.com/doi/10.1177/0018720819891291> — Smartwatch
    tap vs trace vs handwriting (2019): trace 29–33 WPM, tap/handwriting 18–20
    WPM; WER 14–15 % vs 25 %.
16. <https://soar.wichita.edu/items/e1c65c0f-9aa5-4351-b568-795a6711520d> —
    Walking variant abstract (trace 30 WPM).
17. <https://dl.acm.org/doi/10.1145/3411764.3445106> — FingerText (CHI EA
    2021): optimized 5/10-key layouts, intra-hand touches, +10.45 % WPM /
    −39.44 % WER while walking (abstract via Semantic Scholar).
18. <https://www.tandfonline.com/doi/full/10.1080/10447318.2025.2490709> —
    Tap'nSwipe (IJHCI 2025): 9-area × 4-char swipe keyboard beats QWERTY
    accuracy on a small smartwatch (full text).
19. <https://www.researchgate.net/publication/301931220> — WatchWriter (CHI
    2018): statistical decoding on a miniature watch keyboard (abstract).
20. <https://ieeexplore.ieee.org/document/11342892> — Touch vs rotary crown on
    a linear QWERTY watch keyboard (IEEE SMC 2025).
21. <https://dl.acm.org/doi/10.1145/3812770> — CapCrown (PACMHCI 2026):
    finger-count detection on crown rotation; one-finger faster, two-finger
    steadier (abstract via Semantic Scholar).
22. <https://eprints.gla.ac.uk/134441/> — Ng/Brewster/Beruscha/Krautter (CHI
    2017): on-road dial vs pressure-touch vs touchscreen; >94 % accuracy all
    methods, tap fastest (open abstract).
23. <https://trid.trb.org/view/1462682> — Remote rotary controller vs
    touchscreen: distraction results, task-dependent.
24. <https://link.springer.com/chapter/10.1007/978-3-032-30552-7_37> — LYRIQ
    infotainment rotary controllers usability (HCII 2026 poster).
25. <https://carinterior.alibaba.com/tips/knob-vs-touchscreen-safety> — "up to
    47 % less visual-manual distraction" knob claim, attributed to NHTSA/MIT
    AgeLab 2023 (secondary, low confidence; primary not located).
26. <https://dl.acm.org/doi/10.1145/2667317.2667338> — Schaub et al. (UbiComp
    2014): authentication via rotary knob in cars.
27. <https://en.wikipedia.org/wiki/T9_(predictive_text)> — T9 history and why
    multi-tap persists (key size).
28. <https://www.yorku.ca/mack/chi00.html> — MacKenzie, Silfverberg, Korhonen
    (CHI 2000): predicting mobile text-entry speed.
29. <https://dl.acm.org/doi/10.1145/3338286.3340120> — 37,370-volunteer mobile
    typing study (36.2 WPM, 2.3 % errors).
30. <https://userinterfaces.aalto.fi/typing37k/> — same study, project page +
    PDF.
31. <https://dl.acm.org/doi/10.1145/3706598.3713781> — Dwell-based text entry
    peaks ~20 WPM (CHI 2025).
32. <https://www.yorku.ca/mack/uais2006.html> — Dwell-time/feedback effects on
    eye typing.
33. <https://www.sciencedirect.com/science/article/pii/S1071581917300654> — "Is
    it too small?": touch-point distribution on smartwatch QWERTY.
34. <https://support.apple.com/guide/watch/enter-text-watch> — Apple Watch text
    entry options incl. handoff to iPhone.
35. <https://support.google.com/answer/2811346> — Gboard glide/swipe help (what
    swipe-typing does on phones).
36. <https://docs.nordicsemi.com/> — Nordic nRF5 SDK knowledge base: BLE HID
    keyboard + NFC pairing patterns (the handoff building blocks).
37. <https://www.studocu.com/it/document/universita-degli-studi-di-salerno/human-computer-interaction/text-entry-on-circular-smartwatches-the-c-qwerty-layout-study/132704354>
    — C-QWERTY study document (gated mirror; motivation confirmed).

## Rerun Inputs

```
workflow: firecrawl-deep-research
topic: alternatives to the circular wheel keyboard for WiFi password
  entry on a 466x466 / ~35 mm round AMOLED touch panel (ESP32-S3 +
  LVGL 9.6; smartwatch and in-car keyboard systems in scope)
depth: exhaustive
output: markdown (docs/research/circular-keyboard-alternatives.md)
```
