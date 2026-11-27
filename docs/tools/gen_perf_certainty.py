#!/usr/bin/env python3
"""Generate the certainty ladder for docs/verification/performance.rst.

Every ladder on the page prices the SHIPPED product — the tap ladder
priced the wheel that won, the bus/pixel ladders price movement, the
silicon ladder prices the chips. NONE of them priced the twelve
COUNTERFACTUALS: what the methods the keyboard lab rejected or deferred
would have cost the finger and bought against error. This ladder prices
exactly that — the corpus's speed and accuracy evidence, recomputed
from the live sources at figure time, one method per row, colour-coded
by the recorded verdict.

The comparison the ladder makes visible is the TRADE the study turned
on — "certainty over speed". The wheel pays roughly a factor of six
against the corpus's fastest published class (watch trace) and a factor
of four against watch-QWERTY tap, and buys the property no measured
method has: a double-entry that is structurally impossible. The three
SHIP additions sit on the trade's slope, each trading a different piece
of it (multi-tap keeps the target but drops the live preview; dwell
keeps the preview but pays the CHI 2025 ceiling; auto-scan keeps the
certainty but serialises on the scan period).

Every bar parsed at figure time (the house pattern); the parsers are
the docs-side twins of three tracked sources and SystemExit when a
number, a name, or a verdict drifts between them:

  docs/research/circular-keyboard-alternatives.md (the study record)
      "0.7–1.5 s per sweep → ~4–8 CPM → a 16-character password in
      roughly 30–60 s" — the baseline's back-of-envelope, asserted
      INCONSISTENT by construction: 60/1.5 = 40 and 60/0.7 = 86 CPM
      (motion alone), and 16 chars in 30–60 s is 16–32 CPM (wall
      clock); "4–8 CPM" follows from neither (it implies 7.5–15 s per
      commit), so the ladder draws the arithmetic, not the gloss, and
      the gloss is printed as the report's own open-circle slip.
      "18–20 WPM with 14–15 % WER" and "trace 29–33 WPM" and
      "handwriting 18 WPM with 25 % WER" (SAGE 2019); "2.3 % (manual
      vs automated scan) and 0.53 % (shortest-path vs clockwise
      scan)" (Crownboard, CHI 2023); "36.2 WPM / 2.3 % errors"
      (37,370-volunteer phone study); "~20 WPM" dwell ceiling (CHI
      2025); "> 94 % for all methods" (in-car, CHI 2017 — drawn as
      the right panel's footer note, not a row: accuracy was never
      the differentiator); "~1 s/char floor" (scan), "~1–1.5 s per
      multi-tap character", "30°" targets, "0.75 mm" 4×4 keys,
      "8–10 mm" fingertip; and the comparison table's thirteen A–M
      rows, whose first cells and verdict cells are parsed name for
      name and cross-checked against the lab's verdict chips.
  docs/_static/keyboard/round-keyboard-lab.html (the operable record)
      the nine CARDS (rank, chip class + label, name, Speed row,
      Verdict row) — asserted equal to the report's thirteen rows'
      verdict classes; the three OFF cards + the T9 card (13 total);
      the device timing constants the widgets actually run:
      "const T = 700;" (multi-tap window), the dwell checks "< 400"
      and "/ 400", the scan step "Math.floor((t - t0) / 1000) % 12",
      the ring geometry "const C = 233, LARC = 360 / 26;" (the
      27.7°/41.5° sector spans re-derived, not recalled); the mock's
      frozen bottom band "⊗ commit" + "trash-can discard" + "🗑
      discards"; and card #3's own "LONG_PRESSED = 400 ms" citation.
  components/ui/screens.c (the shipped wheel)
      #define WHEEL_SHARE (360.0f / 26.0f), WHEEL_N_SECTORS 12,
      WHEEL_PW_MAX 63; the twelve s_wheel_sector_chars groups
      (ab cd ef gh il jkq mn op rs tu vxz wy) name-for-name; the
      digit/symbol registers at strlen 25/25; and the pairing record
      — "the earlier xmark-on-commit / trash-on-discard pairing is"
      is gone, the keys are the "house circle-check commit key" and
      the "circle-xmark discard key" (2026-11-08). The two operable
      mocks never followed the pairing: the ladder SystemExits if the
      firmware reverts the pairing while the docs' fidelity notes
      (keyboard.rst "shows the original proposal's keys",
      keyboard-lab.rst "the mock's pairing, frozen") still claim it
      — those notes and the firmware comment are asserted together.
  build/config/sdkconfig.h + the vendored LVGL indev
      #define CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME 400 seeding
      "indev->long_press_time = LV_INDEV_DEF_LONG_PRESS_TIME"
      (lv_indev.c:112) — the in-tree 400 ms the dwell row and the
      centre back-hold both run on.
  docs/keyboard-lab.rst (the record page)
      the nine "?only=N" embeds (N = 1..9 — the widgets the page
      actually mounts) and the nine panel headings asserted
      name-for-name equal to the CARDS' names.

Computed at figure time (never hand-copied): WPM → CPM at the 5-char
word convention; s/char → CPM at 60/s; the certainty premium (trace
midpoint ÷ wheel wall-clock midpoint ≈ 6×, tap midpoint ÷ wheel ≈ 4×);
the dwell-ceiling lands INSIDE the watch-tap band (100 vs 90–100 CPM —
asserted); multi-tap's band sits strictly between the wheel's wall
bill and the tap band (asserted); the trace/tap ratio must land inside
the report's own honest "~1.5× headroom" bracket (asserted 1.4..1.9).

Zero rows are drawn as open dotted bars with the parsed reason (the
silicon ladder's precedent), never as missing rows; the ghosts (phone
36.2 WPM, handwriting 25 % WER, the wheel's motion-only floor) are
never summed into a row's bill.

Run:  docs/.venv/bin/python docs/tools/gen_perf_certainty.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

LAB = os.path.join(ROOT, "docs", "_static", "keyboard",
                   "round-keyboard-lab.html")
RPT = os.path.join(ROOT, "docs", "research",
                   "circular-keyboard-alternatives.md")
SCR = os.path.join(ROOT, "components", "ui", "screens.c")
SDKH = os.path.join(ROOT, "build", "config", "sdkconfig.h")
LVINDEV = os.path.join(ROOT, "managed_components", "lvgl__lvgl", "src",
                       "indev", "lv_indev.c")
LABRST = os.path.join(ROOT, "docs", "keyboard-lab.rst")
KBRST = os.path.join(ROOT, "docs", "keyboard.rst")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_certainty.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
# verdict identity (the lab's own chip classes, colour-coded as the
# wheel-commit house palette — one colour per recorded verdict):
VERDICT_C = {"keep": "#2b5fb0", "ship": "#2e7d32", "hold": "#b07d10",
             "rej": "#b91c1c", "exp": "#7c3aed", "v2": "#0e7490",
             "infeas": "#6b7280"}

# the expected method model: lab rank -> (report table letter, verdict
# class, short row label). The lab's chip class must agree.
RANK_MODEL = {
    1: ("A", "keep", "sweep wheel v2 (12-sector hybrid)"),
    2: ("B", "ship", "12-sector DTMF multi-tap"),
    3: ("L", "ship", "hold-to-commit dwell"),
    4: ("F", "ship", "auto-scan, Crownboard-style"),
    5: ("C", "hold", "9×4 swipe sectors"),
    6: ("D", "rej", "fitted QWERTY 6×6"),
    7: ("E", "rej", "ring glide / one-stroke"),
    8: ("K", "exp", "IMU twist"),
    9: ("J", "v2", "physical encoder + haptic detents"),
}
OFF_MODEL = [  # the three off-hardware cards + the T9 card, in page order:
    # (table letter, lab chip class, table verdict class, short label).
    # I is the one vocabulary drift between the two records — the lab cards
    # it "INFEASIBLE NOW", the report writes "rejected now": both spellings
    # are asserted on their own side rather than papered over.
    ("G", "infeas", "infeas", "voice"),
    ("H", "infeas", "infeas", "camera / OCR"),
    ("I", "infeas", "rej", "QR / NFC / BLE handoff"),
    ("M", "rej", "rej", "T9 dictionary decode"),
]
SECTOR_GROUPS = ["ab", "cd", "ef", "gh", "il", "jkq", "mn", "op", "rs",
                 "tu", "vxz", "wy"]


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"missing source for the ladder: {path}")
    with open(path, encoding="utf-8") as f:
        return f.read()


def expect(src: str, pattern: str, what: str, flags=0) -> re.Match:
    m = re.search(pattern, src, flags)
    if not m:
        raise SystemExit(f"{what}: pattern {pattern!r} not found — "
                         "the fact moved; re-read the source before drawing")
    return m


def verdict_class(text: str, what: str) -> str:
    t = text.lower().replace("**", "")
    for key in ("infeas", "v2 upgrade", "experimental", "hold", "reject",
                "ship", "keep"):
        if key in t:
            return {"reject": "rej", "v2 upgrade": "v2",
                    "experimental": "exp"}.get(key, key)
    raise SystemExit(f"{what}: verdict {text!r} matches no known class")


def main() -> None:
    lab, rpt, scr = _read(LAB), _read(RPT), _read(SCR)
    sdkh, lvindev = _read(SDKH), _read(LVINDEV)
    labrst, kbrst = _read(LABRST), _read(KBRST)

    # ---- the report: baseline cost model --------------------------------
    m = expect(rpt, r"([\d.]+)–([\d.]+) s\s+per sweep", "the wheel's "
               "per-sweep seconds (the cost model's motion pair)")
    sec_fast, sec_slow = float(m.group(1)), float(m.group(2))  # 0.7, 1.5
    expect(rpt, r"~4–8 CPM", "the report's own 4–8 CPM gloss (kept as a "
           "slip annotation, never as a bar)")
    m = expect(rpt, r"a (\d+)-character password in roughly (\d+)–(\d+) s",
               "the report's wall-clock pair")
    pw_n, wall_lo_s, wall_hi_s = (int(m.group(1)), int(m.group(2)),
                                  int(m.group(3)))
    wheel_wall = (60.0 * pw_n / wall_hi_s, 60.0 * pw_n / wall_lo_s)   # 16..32
    wheel_motion = (60.0 / sec_slow, 60.0 / sec_fast)                # 40..85.7
    # the slip is structural: the gloss must stay contradicted by its own
    # seconds, or the docs pages that quote the corrected arithmetic drift.
    if not (wheel_motion[0] > 8):
        raise SystemExit("the report's 4–8 CPM gloss is no longer contradicted "
                         "by its own 0.7–1.5 s pair — the slip story (and the "
                         "docs pages' re-derivation) is stale")

    # ---- the report: the corpus's published numbers ----------------------
    m = expect(rpt, r"(\d+)–(\d+) WPM with (\d+)–(\d+) % WER",
               "watch-QWERTY tap WPM + WER (SAGE 2019)")
    tap_wpm, wer_band = (int(m.group(1)), int(m.group(2))), (int(m.group(3)),
                                                             int(m.group(4)))
    m = expect(rpt, r"trace (\d+)–(\d+) WPM", "watch trace WPM (SAGE 2019)")
    trace_wpm = (int(m.group(1)), int(m.group(2)))
    m = expect(rpt, r"handwriting (\d+) WPM with (\d+) % WER",
               "watch handwriting (SAGE, context ghost)")
    hand_wpm, hand_wer = int(m.group(1)), int(m.group(2))
    m = expect(rpt, r"(\d+\.\d)\s+WPM / (\d+\.\d) % errors",
               "the 37,370-volunteer phone baseline (ghost; the source "
               "wraps the pair across a line break)")
    phone_wpm, phone_err = float(m.group(1)), float(m.group(2))
    m = expect(rpt, r"Dwell \(hold-to-commit\) tops out around (\d+) WPM",
               "the dwell ceiling (CHI 2025)")
    dwell_wpm = int(m.group(1))
    m = expect(rpt, r"(\d+\.\d) % \(manual vs\s+automated scan\) and\s+"
               r"(\d+\.\d+) % \(shortest-path vs\s+clockwise scan\)",
               "Crownboard's 2.3 % / 0.53 % pair (the source wraps mid-"
               "sentence)")
    crown_hi, crown_lo = float(m.group(1)), float(m.group(2))
    expect(rpt, r">94 % accuracy for _all_ methods", "the in-car >94 % "
           "accuracy note (right panel's footer claim)")
    expect(rpt, r"~1.5× speed headroom", "the report's honest trace/tap "
           "bracket (the ratio below is asserted inside it)")
    m = expect(rpt, r"~(\d+)–([\d.]+) s per multi-tap character",
               "multi-tap's per-character seconds")
    mt_lo_s, mt_hi_s = float(m.group(1)), float(m.group(2))
    expect(rpt, r"~(\d+) s/char floor", "the scan period's floor")
    scan_s = float(m.group(1))
    expect(rpt, r"\((\d+)° targets", "the multi-tap's 30° target")
    mt_deg = int(m.group(1))
    expect(rpt, r"≈ (\d+\.\d+) mm per key", "the 4×4 QWERTY key size (the "
           "source writes '~20 px ≈ 0.75 mm per key')")
    expect(rpt, r"~(\d+)–(\d+) mm fingertip", "the fingertip band")
    expect(rpt, r"16 characters in|16-character password",
           "the 16-character wall-clock claim")

    # ---- the report: the comparison table, A..M ---------------------------
    table = expect(rpt, r"### Comparison table(.+?)\n## ", "the comparison "
                   "table section", re.S).group(1)
    # matched per line on purpose: with a multiline findall the \s* runs
    # bridge the newline and chain two rows into one match
    rows = []
    for line in table.splitlines():
        mm = re.match(r"^\| ([A-M])\. (.+?)\s*\|(.+)\|\s*(.+?)\s*\|\s*$",
                      line)
        if mm:
            rows.append(mm.groups())
    if len(rows) != 13:
        raise SystemExit(f"the comparison table no longer carries 13 A–M "
                         f"rows (found {len(rows)})")
    table_verdicts = {}
    for letter, name, _mid, verdict in rows:
        table_verdicts[letter] = (name, verdict_class(verdict,
                                                      f"table row {letter}"))

    # ---- the lab: the nine CARDS + four cards -----------------------------
    cards = re.findall(
        r'rank:\s*"(\d+)",\s*chip:\s*\["(\w+)",\s*"([^"]+)"\],\s*'
        r'name:\s*"([^"]+)",[\s\S]*?rows:\s*\[\["Speed",\s*"([^"]*)"', lab)
    if len(cards) != 9:
        raise SystemExit(f"the lab no longer carries nine operable CARDS "
                         f"(found {len(cards)})")
    for rank, chip_cls, chip_lbl, name, speed in cards:
        r = int(rank)
        letter, cls, _label = RANK_MODEL[r]
        if chip_cls != cls:
            raise SystemExit(f"lab card #{r} chip class {chip_cls!r} "
                             f"disagrees with the model ({cls!r})")
        t_name, t_cls = table_verdicts[letter]
        if cls != t_cls:
            raise SystemExit(f"rank {r} <-> table {letter}: lab verdict "
                             f"{cls!r} vs table verdict {t_cls!r}")
    for rank, chip_cls, chip_lbl, name, speed in cards:
        if chip_lbl in ("KEEP · v2",):
            expect(speed, r"0\.7–1\.5 s/char",
                   f"card {rank}'s speed must quote the report's motion pair")
        if chip_lbl == "SHIP · setting":
            expect(speed, r"~1 s/char floor",
                   f"card {rank}'s speed must quote the scan floor")
        if int(rank) == 2:
            expect(speed, rf"~{mt_lo_s:g}–{mt_hi_s:g} s/char",
                   "card #2's speed no longer mirrors the report's "
                   "multi-tap seconds (the pair must agree across records)")

    off = re.findall(r'\{\s*t:\s*"([^"]+)",\s*chip:\s*\["(\w+)",\s*'
                     r'"([^"]+)"\]', lab)
    if len(off) != 3 or [o[0] for o in off] != ["Voice", "Camera / OCR",
                                                 "QR / NFC / BLE handoff"]:
        raise SystemExit(f"the off-hardware cards moved: {off}")
    # the OFF trio + T9 against the comparison table (letter by letter; the
    # lab writes I "INFEASIBLE NOW" where the report writes "rejected now" —
    # both asserted, none harmonised)
    for (letter, lab_cls, tbl_cls, _lbl), (_t, chip_cls, _l) in zip(
            OFF_MODEL[:3], off):
        if chip_cls != lab_cls:
            raise SystemExit(f"off card {letter}: lab chip class "
                             f"{chip_cls!r} != expected {lab_cls!r}")
        if table_verdicts[letter][1] != tbl_cls:
            raise SystemExit(f"table row {letter} verdict "
                             f"{table_verdicts[letter][1]!r} != expected "
                             f"{tbl_cls!r}")
    if table_verdicts["M"][1] != OFF_MODEL[3][2]:
        raise SystemExit(f"table row M verdict {table_verdicts['M'][1]!r} "
                         f"!= expected {OFF_MODEL[3][2]!r}")
    expect(lab, r'class="chip rej">REJECTED · task fit<', "the T9 card's "
           "verdict chip")
    expect(lab, r'const C = 233, LARC = 360 / 26;', "the lab's ring geometry")
    share = 360.0 / 26.0
    if not (abs(round(2 * share, 1) - 27.7) < 0.05
            and abs(round(3 * share, 1) - 41.5) < 0.05):
        raise SystemExit("the 27.7/41.5 sector spans no longer follow from "
                         "LARC = 360/26")
    expect(lab, r"const T = 700;", "the multi-tap window the widget runs")
    expect(lab, r"hold\.t0 < 400", "the dwell's cancel check")
    expect(lab, r"hold\.t0 >= 400", "the dwell's commit check")
    expect(lab, r"Math\.floor\(\(t - t0\) / 1000\) % 12",
           "the scan's 1 s step")
    expect(lab, r"LONG_PRESSED = 400 ms", "card #3's in-tree citation")
    expect(lab, r"⊗ commit", "the mock's frozen ⊗ commit")
    expect(lab, r"trash-can discard", "the mock's frozen trash discard")
    expect(lab, r"🗑 discards", "the mock's frozen discard hint")

    # ---- the shipped wheel: geometry + the pairing record -----------------
    expect(scr, r"#define WHEEL_SHARE \(360\.0f / 26\.0f\)",
           "the wheel's equal-share definition")
    expect(scr, r"#define WHEEL_N_SECTORS 12", "the sector count")
    expect(scr, r"#define WHEEL_PW_MAX 63", "the entry cap")
    m = expect(scr, r"s_wheel_sector_chars\[12\]\s*=\s*\{([^}]+)\}",
               "the twelve sector groups")
    groups = re.findall(r'"([^"]+)"', m.group(1))
    if groups != SECTOR_GROUPS:
        raise SystemExit(f"the sector grouping drifted from the record: "
                         f"{groups}")
    if sorted(len(g) for g in groups) != [2] * 10 + [3] * 2:
        raise SystemExit("the ten 2-letter + two 3-letter group shape "
                         "changed — the lab's 'the two 3-letter sectors "
                         "41.5°' story needs re-reading")
    if sum(len(g) for g in groups) != 26:
        raise SystemExit("the twelve groups no longer tile the 26 letters")
    # (the digit register itself contains ';' — the body must be matched to
    # the closing brace, not to the first semicolon)
    m = expect(scr, r"s_wheel_sets\[4\]\s*=\s*\{(.*?)\};", "the four "
               "registers", re.S)
    sets = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    if len(sets) != 4:
        raise SystemExit(f"the wheel no longer has four registers: {sets}")
    digits, symbols = sets[2], sets[3]
    if len(digits) != 25 or len(symbols) != 25:
        raise SystemExit(f"the digit/symbol registers are no longer 25/25 "
                         f"slots ({len(digits)}/{len(symbols)}) — the tap "
                         f"ladder's strlen counts are stale")
    expect(scr, r"the earlier xmark-on-commit / trash-on-discard pairing is",
           "the firmware's own record of the pairing change (2026-11-08)")
    expect(scr, r"house circle-check commit key", "the check-commit key "
           "comment")
    expect(scr, r"circle-xmark discard key", "the xmark-discard key comment")
    # the fidelity notes the docs pages owe the frozen mocks (asserted with
    # the firmware comment above — pairing changed IN firmware while the
    # notes claim the mocks are frozen is the drift this ladder refuses)
    expect(kbrst, r"shows the original proposal's keys, ⊗ for commit and a "
           r"trash can for discard",
           "keyboard.rst no longer tells the reader the widget keeps the "
           "proposal's ⊗/trash pairing")
    expect(kbrst, r"circle-check COMMIT key", "keyboard.rst's shipped "
           "pairing sentence")
    expect(kbrst, r"circle-xmark DISCARD key", "keyboard.rst's shipped "
           "pairing sentence")
    expect(labrst, r"mock's bottom band shows the proposal's ⊗ commit and "
           r"trash-can discard", "keyboard-lab.rst panel #1 no longer "
           "scopes its ⊗/trash line to the mock")
    expect(labrst, r"16–32 CPM", "keyboard-lab.rst panel #1 no longer "
           "carries the re-derived wall-clock bill")
    expect(labrst, r"40–86 CPM", "keyboard-lab.rst panel #1 no longer "
           "carries the re-derived motion floor")

    # ---- the in-tree dwell constant ---------------------------------------
    expect(sdkh, r"#define CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME 400",
           "the in-tree LONG_PRESSED the dwell row cites")
    expect(lvindev, r"indev->long_press_time\s*= LV_INDEV_DEF_LONG_PRESS_TIME;",
           "the indev seeding of long_press_time")

    # ---- the record page: nine widgets + nine headings --------------------
    only = sorted(int(n) for n in re.findall(r"\?only=(\d)", labrst))
    if only != list(range(1, 10)):
        raise SystemExit(f"keyboard-lab.rst mounts {only}, not the nine "
                         "widgets 1..9")
    heads = dict((int(n), nm.strip()) for n, nm in re.findall(
        r"^(\d) · (.+)$", labrst, re.M))
    if set(heads) != set(range(1, 10)):
        raise SystemExit(f"keyboard-lab.rst panel headings {sorted(heads)} "
                         "are not 1..9")
    for rank, _c, _l, name, _s in cards:
        if heads[int(rank)] != name:
            raise SystemExit(f"panel #{rank} heading {heads[int(rank)]!r} "
                             f"disagrees with the lab card {name!r}")

    # ---- computed model ----------------------------------------------------
    tap_cpm = (tap_wpm[0] * 5, tap_wpm[1] * 5)            # 90..100
    trace_cpm = (trace_wpm[0] * 5, trace_wpm[1] * 5)      # 145..165
    dwell_ceiling = dwell_wpm * 5                          # 100
    scan_floor = 60.0 / scan_s                             # 60
    mt_cpm = (60.0 / mt_hi_s, 60.0 / mt_lo_s)              # 40..60
    phone_cpm = phone_wpm * 5                              # 181
    hand_cpm = hand_wpm * 5                                # 90
    ratio = ((trace_cpm[0] + trace_cpm[1]) / 2) / ((tap_cpm[0] + tap_cpm[1])
                                                    / 2)
    if not 1.4 <= ratio <= 1.9:
        raise SystemExit(f"the trace/tap ratio {ratio:.2f} left the report's "
                         "own ~1.5x bracket")
    if not (tap_cpm[0] <= dwell_ceiling <= tap_cpm[1] + 5):
        raise SystemExit("the dwell ceiling no longer lands in the watch-tap "
                         "band — the ladder's pairing story is stale")
    if not (wheel_wall[1] < mt_cpm[0] and mt_cpm[1] < tap_cpm[0]):
        raise SystemExit("multi-tap's band no longer sits strictly between "
                         "the wheel's wall bill and the tap band")
    prem_trace = ((trace_cpm[0] + trace_cpm[1]) / 2) / ((wheel_wall[0]
                                                         + wheel_wall[1]) / 2)
    prem_tap = ((tap_cpm[0] + tap_cpm[1]) / 2) / ((wheel_wall[0]
                                                   + wheel_wall[1]) / 2)

    # rows: (label, speed (lo,hi,kind) or None, provenance, err value/band or
    # special, err provenance/reason)  kind: band | floor | ceiling | none
    ROWS = [
        ("sweep wheel v2 — 12 hybrid", "keep",
         (wheel_wall[0], wheel_wall[1], "band"),
         "wall est. 16–32 CPM (report's own 16 chars/30–60 s)",
         "structural-zero",
         "release-only commit: double-entry structurally impossible; no "
         "real-glass benchmark yet (pending check)"),
        ("12-sector DTMF multi-tap", "ship",
         (mt_cpm[0], mt_cpm[1], "band"), f"~{mt_lo_s}–{mt_hi_s} s/char "
         "(report)", "open",
         f"{mt_deg}° target · timeout absorbs jitter · no preview between "
         "presses (report's own risk)"),
        ("hold-to-commit dwell", "ship",
         (None, dwell_ceiling, "ceiling"), f"~{dwell_wpm} WPM ceiling "
         "(CHI 2025)", "open", "same target + live preview · no new error "
         "class"),
        ("auto-scan, Crownboard-style", "ship",
         (None, scan_floor, "floor"), f"~{scan_s:.0f} s/char floor "
         "(scan period)", (crown_lo, crown_hi), "Crownboard 0.53–2.3 % "
         "(best in corpus)"),
        ("9×4 swipe sectors", "hold",
         (trace_cpm[0], trace_cpm[1], "band"), f"trace {trace_wpm[0]}–"
         f"{trace_wpm[1]} WPM (SAGE 2019)", "open", "better than QWERTY on "
         "small watches — no % published"),
        ("fitted QWERTY 6×6", "rej",
         (tap_cpm[0], tap_cpm[1], "band"), f"watch tap {tap_wpm[0]}–"
         f"{tap_wpm[1]} WPM (SAGE 2019)", (wer_band[0], wer_band[1]),
         f"{wer_band[0]}–{wer_band[1]} % WER at watch key sizes"),
        ("ring glide / one-stroke", "rej",
         (trace_cpm[0], trace_cpm[1], "band"), "trace on QWERTY + dictionary "
         "(SAGE 2019)", "open", "needs a word model · double-entry risk on "
         "arbitrary strings"),
        ("IMU twist", "exp", None, "no published figure", "open",
         "conflicts with the 90° roll tracker — disqualifying"),
        ("physical encoder + detents", "v2", None, "crown-class rotate+press",
         (crown_hi, crown_hi), "2.3 % class (Crownboard) · glance-free"),
        ("voice", "infeas", None, "no microphone on the board", "none", ""),
        ("camera / OCR", "infeas", None, "no camera on the board", "none", ""),
        ("QR / NFC / BLE handoff", "infeas", None,
         "no NFC/camera; companion-app overkill for once-per-network",
         "none", ""),
        ("T9 dictionary decode", "rej", None,
         "same mechanics as multi-tap — minus the word list it needs",
         "open", "word model against arbitrary passwords buys ~nothing"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(12.4, 6.6),
                             gridspec_kw={"width_ratios": [1.18, 1]})
    AXIS_MIN = 2.5

    # ---- (a) the finger's side: characters per minute ---------------------
    ax = axes[0]
    ys, ylab = [], []
    for i, (label, cls, speed, prov, _e, _er) in enumerate(ROWS):
        ys.append(i * 1.0)
        ylab.append(label)
        color = VERDICT_C[cls]
        y = i * 1.0
        if speed is None:
            ax.barh(y, 3.4, height=0.42, color="white", edgecolor=color,
                    linestyle=":", linewidth=1.1, zorder=3)
            ax.text(4.4, y, prov, va="center", fontsize=5.3, color=MUTED,
                    style="italic", zorder=4)
            continue
        lo, hi, kind = speed
        if kind == "band":
            ax.barh(y, hi - lo, left=lo, height=0.42, color=color,
                    alpha=0.85, zorder=3)
            ax.text(hi * 1.14, y, f"{lo:.0f}–{hi:.0f} · {prov}", va="center",
                    fontsize=5.3, color=MUTED, zorder=4)
        elif kind == "floor":
            ax.barh(y, hi, left=AXIS_MIN, height=0.42, color="white",
                    edgecolor=color, linewidth=1.1, zorder=3)
            ax.barh(y, 3, left=hi - 3, height=0.42, color=color, alpha=0.9,
                    zorder=4)
            ax.text(hi * 1.14, y, f"floor {hi:.0f} · {prov}", va="center",
                    fontsize=5.3, color=MUTED, zorder=4)
        else:  # ceiling
            ax.barh(y, hi, left=AXIS_MIN, height=0.42, color=color,
                    alpha=0.45, zorder=3)
            ax.barh(y, 3, left=hi - 3, height=0.42, color=color, alpha=0.95,
                    zorder=4)
            ax.text(hi * 1.14, y, f"ceiling {hi:.0f} · {prov}", va="center",
                    fontsize=5.3, color=MUTED, zorder=4)
    # the wheel's motion-only floor: hatched ghost below row 0 (never
    # summed into the wall bill)
    ax.barh(1.28, wheel_motion[1] - wheel_motion[0], left=wheel_motion[0],
            height=0.28, color="white", edgecolor=VERDICT_C["keep"],
            linestyle=":", hatch="///", linewidth=1.0, zorder=2)
    ax.text(wheel_motion[1] * 1.14, 1.28, f"{wheel_motion[0]:.0f}–"
            f"{wheel_motion[1]:.0f} · motion floor alone (0.7–1.5 s/commit, "
            "before aim + settle)", va="center", fontsize=5.3,
            color=VERDICT_C["keep"], zorder=4)
    # the report's 4–8 CPM slip: an open circle left of row 0's bar
    ax.scatter([8], [0.0], marker="o", s=34, facecolor="white",
               edgecolor=VERDICT_C["keep"], linewidth=1.2, zorder=4)
    ax.text(8.6, -0.42, "4–8 CPM — the report's own gloss: implies "
            "7.5–15 s/commit and follows from neither pair", va="center",
            fontsize=5.2, color=VERDICT_C["keep"], style="italic", zorder=4)
    # the phone ghost (2D pointing when the panel CAN host a QWERTY) —
    # offset 0.3 below the glide row so neither its bar nor its text
    # rides the row above (a 0.15 offset collided on the first render)
    ax.barh(6.3, phone_cpm, left=AXIS_MIN, height=0.28, color="white",
            edgecolor=INK, linestyle=":", hatch="..", linewidth=0.9,
            zorder=2)
    ax.text(phone_cpm * 1.06, 6.3, f"{phone_cpm:.0f} · phone baseline "
            "36.2 WPM (37,370 volunteers) — the 2D ceiling off this glass",
            va="center", fontsize=5.3, color=MUTED, zorder=4)
    ax.set_yticks(ys)
    ax.set_yticklabels(ylab, fontsize=6.4, color=INK)
    ax.set_xscale("log")
    ax.set_xlim(AXIS_MIN, 2600)
    ax.set_ylim(len(ROWS) - 0.45, -1.0)
    ax.set_xlabel("characters per minute (log) — published WPM × 5 (the "
                  "5-char word) and the report's own seconds, computed at "
                  "figure time", fontsize=6.6, color=MUTED)
    ax.tick_params(axis="x", labelsize=6, colors=MUTED)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.grid(axis="x", color=HAIR, linewidth=0.5, zorder=0)
    # no legend: it collided with the italic notes on every corner tried.
    # The row labels themselves carry the verdict colour instead (the
    # caption names the mapping), so the panel stays self-describing.
    for tick, (_l, cls, *_rest) in zip(ax.get_yticklabels(), ROWS):
        tick.set_color(VERDICT_C[cls])
    # the headline comparison lives INSIDE the panel (a two-line title
    # collided with the right panel's title on the first render)
    ax.text(AXIS_MIN * 1.05, -0.72,
            f"certainty premium (midpoints): the wheel pays "
            f"{prem_trace:.1f}× against the trace class and {prem_tap:.1f}× "
            "against watch-QWERTY tap",
            va="center", fontsize=5.6, color=INK, zorder=4)
    ax.set_title("the finger's side — how fast each method is", fontsize=8.5)

    # ---- (b) the error side ------------------------------------------------
    ax = axes[1]
    for i, (label, cls, _speed, _prov, err, reason) in enumerate(ROWS):
        y = i * 1.0
        color = VERDICT_C[cls]
        if err == "structural-zero":
            ax.scatter([0.335], [y], marker="D", s=46, color=color,
                       edgecolor=INK, linewidth=0.6, zorder=4)
            ax.text(0.47, y, "STRUCTURAL ZERO — double-entry impossible "
                    "(release-only commit) · unbenchmarked on glass",
                    va="center", fontsize=5.3, color=color, zorder=4)
        elif err == "none":
            ax.scatter([0.335], [y], marker="x", s=30, color=HAIR, zorder=3)
        elif err == "open":
            ax.scatter([0.335], [y], marker="o", s=52, facecolor="white",
                       edgecolor=color, linewidth=1.3, zorder=4)
            # 0.46 not 0.40: the open marker is ~3.5% of the log width — a
            # closer start crossed the first character on the first render
            ax.text(0.46, y, reason, va="center", fontsize=5.3, color=MUTED,
                    style="italic", zorder=4)
        else:
            lo, hi = err
            if hi > lo:
                ax.plot([lo, hi], [y, y], color=color, linewidth=2.6,
                        solid_capstyle="round", zorder=4)
                ax.text(hi * 1.25, y, f"{lo:g}–{hi:g} %", va="center",
                        fontsize=5.6, color=INK, zorder=4)
            else:
                ax.scatter([lo], [y], marker="o", s=52, color=color,
                           edgecolor=INK, linewidth=0.6, zorder=4)
                ax.text(lo * 1.35, y, f"{lo:g} %", va="center", fontsize=5.6,
                        color=INK, zorder=4)
    # the two context ghosts (never summed into a row's evidence)
    ax.scatter([phone_err], [5.62], marker=".", s=44, color=MUTED, zorder=3)
    ax.text(phone_err * 1.25, 5.62, f"{phone_err:g} % — the phone baseline's "
            "error rate (context)", va="center", fontsize=5.3, color=MUTED,
            zorder=4)
    ax.scatter([hand_wer], [4.28], marker=".", s=44, color=MUTED, zorder=3)
    ax.text(hand_wer * 1.15, 4.28, f"{hand_wer} % WER — watch handwriting "
            "(SAGE), the worst class (context)", va="center", fontsize=5.3,
            color=MUTED, zorder=4)
    ax.set_yticks([i * 1.0 for i in range(len(ROWS))])
    ax.set_yticklabels([r[0] for r in ROWS], fontsize=6.4, color=INK)
    ax.tick_params(axis="x", labelsize=6, colors=MUTED)
    ax.set_xscale("log")
    ax.set_xlim(0.29, 90)
    ax.set_ylim(len(ROWS) - 0.45, -0.8)
    ax.set_xlabel("published accuracy evidence (% — word error / character "
                  "error, log)", fontsize=6.6, color=MUTED)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.grid(axis="x", color=HAIR, linewidth=0.5, zorder=0)
    ax.text(0.29, len(ROWS) - 0.15,
            "open dots = no published figure, with the parsed reason · the "
            "in-car study's >94 % accuracy holds for ALL methods (CHI 2017) "
            "— accuracy was never the differentiator; the dial's case is "
            "glance/distraction",
            fontsize=5.5, color=MUTED, style="italic", va="bottom")
    ax.set_title("the error side — what the corpus measured", fontsize=8.5)

    fig.tight_layout()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fig.savefig(OUT, dpi=200, bbox_inches="tight")
    print(f"wrote {os.path.relpath(OUT, ROOT)}  (wheel wall "
          f"{wheel_wall[0]:.0f}–{wheel_wall[1]:.0f} CPM, motion floor "
          f"{wheel_motion[0]:.0f}–{wheel_motion[1]:.0f}; multi-tap "
          f"{mt_cpm[0]:.0f}–{mt_cpm[1]:.0f}; dwell ceiling {dwell_ceiling} "
          f"in tap band {tap_cpm[0]}–{tap_cpm[1]}; scan floor {scan_floor}; "
          f"trace {trace_cpm[0]}–{trace_cpm[1]}; phone {phone_cpm:.0f}; "
          f"premiums trace {prem_trace:.2f}x tap {prem_tap:.2f}x; "
          f"trace/tap {ratio:.2f}; groups {len(groups)} registers "
          f"{len(digits)}/{len(symbols)})")


if __name__ == "__main__":
    main()
