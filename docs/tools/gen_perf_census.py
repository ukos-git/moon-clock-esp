#!/usr/bin/env python3
"""Generate the build census for docs/verification/performance.rst — the
counting axis next to perf_storage (how many BYTES lie in the flash): **how
many PARTS make each board's image, and how many of them are silent**. One
project, three builds: the S3 clock (default target), the P4 (sibling
target) and the knob (a third build of the SAME tree on the esp32s3 target,
selected by -D KNOB_BOARD=ON). The binary page counts bytes, the storage
ladder counts bytes lying still, and build-seam.dot draws the SHAPE of the
seam — this figure counts the parts: the four shared components every board
compiles, the board-side pair the shared board/imu components answer with
their KNOB_BOARD rows on the knob, the registry set each build resolves,
and the 5,594 lines the knob build drops instead of compiling.

Nothing is recalled. Every number is parsed at figure time with
SystemExit-failing parsers (the house pattern), and the parsers double as
docs-side twins of the build guards — the figure refuses to draw when the
seam's rules are removed:

  components/ dirs                       the single first-party inventory,
      asserted against the exact expected set (13: the clock's seven —
      board/clock/esp32_s3_touch_amoled_1_75/imu/moon/ui/wifi — plus the
      knob's six knob_* components) — the census is a directory listing,
      not a memory
  the root CMakeLists, three ways         EXCLUDE_COMPONENTS before
      project(moon-phase): the P4 block names the vendored S3 BSP AND
      knob_board, the S3-clock block names knob_board (its knob-only managed
      deps must stay out of the clock locks — a target rule alone cannot,
      the knob IS an esp32s3), the knob block names the vendored S3 BSP;
      both DEPENDENCIES_LOCK pins (p4 + knob) AND idf_build_set_property
      (KNOB_BOARD ON) must predate project() — the figure dies if the lock
      split or the early-pass promotion is undone
  build trees                            build/esp-idf (S3) + build/p4
      (P4) + build/knob (knob) must exist; the vendored S3 BSP must NOT
      link on P4 or knob, and every first-party component must link on all
      three (the knob's board/imu compile their KNOB_BOARD rows)
  the knob's answers, three ways         board: components/board's
      KNOB_BOARD branch dispatches to knob_board + knob_include; imu:
      imu_stub.c (the soft-fail posture); esp_codec_dev: a REAL managed
      dep in knob_board's idf_component.yml under a target == esp32s3 rule
      (prepare_dependencies runs before kconfig, so no $CONFIG rule could
      resolve) — the clocks keep it out of their locks by the knob_board
      exclusion, asserted lock-for-lock (S3/P4 carry NO sh8601)
  UI_BOARD_SRCS / UI_CORE_SRCS (ui)      exactly the four dropped TUs and
      the six unchanged ones; the KNOB branch must empty the four
      (ui :40-42); every named file must exist
  FONT_DIR branch (ui :22-28)            fonts/fonts_p4/fonts_knob map; the
      three tiers carry the SAME seven file names (the identical
      lv_font_t-symbol promise) — each build compiles exactly ONE tier
  REQUIRES names                         parsed from the four shared
      CMakeLists: clock names board, ui names board+imu+esp_codec_dev,
      wifi and moon name NONE of the three — the exact matrix this figure
      draws; the literals are what the EARLY pass sees
  the three dependency locks             top-level keys minus idf, per
      build: S3 21 (carries esp_lcd_co5300 + the CST9217 touch, NO
      esp_hosted), P4 24 (carries the XC BSP + esp_hosted +
      esp_wifi_remote, NO co5300/CST9217 — the target rules hold), knob 7
      (the six registry entries of the clocks' shared world PLUS
      esp_codec_dev — asserted name-for-name); the on-disk
      managed_components/ must hold the S3 set PLUS the P4-only gt911 and
      the knob-only sh8601 — the CI union state (every other knob-lock
      entry sits in the S3 set already) — registry drift fails the figure
  LOC counts                             the four dropped TUs, the clock's
      board_display.c, the knob's knob_board answer (its own sources) and
      the 28-line imu_stub.c; the esp_codec_dev answer is a MANAGED dep —
      zero in-tree lines (drawn italic *no such leg*)
  the knob's honest stubs                knob_alarm_audio_stub.c must still
      return "Silent" (the answer to the dropped alarm_audio.c)

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob violet.
Segment families keep one colour across all three bars; the seam's answers
are the amber/steel of build-seam.dot (the seam's price), managed the
registry grey.
Hatched = cited (the bus ladder's precedent); italic = the honest *no such
leg*.

Run:  docs/.venv/bin/python docs/tools/gen_perf_census.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_census.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity
VERM = "#c2410c"      # P4 identity
VIOLET = "#7c3aed"    # knob identity
AMBER = "#b07d10"     # the seam's price (build-seam.dot)
STEEL = "#5c7fb8"     # shared-4
SLATE = "#4b5563"     # the app/main component
TEAL = "#0f766e"      # board-side (the shared board + imu components)
GREY = "#9ca3af"      # manager-vendored (the registry's problem)

INKC = {t: c for t, c in (("s3", BLUE), ("p4", VERM), ("knob", VIOLET))}


def _read(path: str) -> str:
    if not os.path.isabs(path):
        path = os.path.join(ROOT, path)
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — this "
                         f"figure reads the CMakeLists files, the three "
                         f"dependency locks, the font tiers and the three "
                         f"build trees")
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def _loc(path: str) -> int:
    p = os.path.join(ROOT, path)
    if not os.path.exists(p):
        raise SystemExit(f"{path}: not found — the LOC bars read real files")
    return len(_read(p).splitlines())


def _dirs(path: str) -> set:
    p = os.path.join(ROOT, path)
    if not os.path.isdir(p):
        raise SystemExit(f"{path}/: not a directory — the census is a "
                         f"directory listing, not a memory")
    return {d for d in os.listdir(p) if os.path.isdir(os.path.join(p, d))}


def _strip_comments(text: str) -> str:
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))


# ---------------------------------------------------------------- first-party
# The ONE component inventory, asserted against the exact expected set.
comps = _dirs("components")
CLOCK_COMPS = {"board", "clock", "esp32_s3_touch_amoled_1_75", "imu", "moon",
               "ui", "wifi"}
KNOB_COMPS = {"knob_battery", "knob_board", "knob_moon", "knob_power",
              "knob_time", "knob_ui"}
if comps != CLOCK_COMPS | KNOB_COMPS:
    raise SystemExit(f"components/ inventory drifted: {sorted(comps)} — the "
                     f"census expects the one shared tree: the clock's seven "
                     f"first-party components plus the knob's six knob_* "
                     f"components")
if not os.path.exists(os.path.join(ROOT, "main", "main.cpp")):
    raise SystemExit("main/main.cpp: the clock build's app TU is missing")
if not os.path.exists(os.path.join(ROOT, "main", "main_knob.c")):
    raise SystemExit("main/main_knob.c: the knob build's app TU is missing")
for t, d in (("s3", "build/esp-idf"), ("p4", "build/p4/esp-idf"),
             ("knob", "build/knob/esp-idf")):
    if not os.path.isdir(os.path.join(ROOT, d)):
        raise SystemExit(f"{d}/: missing — build the target first; the census "
                         f"cross-checks the linked component sets on disk")
linked = {t: set(os.listdir(os.path.join(ROOT, "build", *d.split("/")[1:])))
          for t, d in (("s3", "build/esp-idf"), ("p4", "build/p4/esp-idf"),
                       ("knob", "build/knob/esp-idf"))}
# the two exclusions, asserted both ways: the vendored S3 BSP links ONLY
# on the S3 build, and knob_board (whose yml resolves the knob-only
# managed deps) links ONLY on the knob build — everything else links on
# all three (the knob's board/imu compile their KNOB_BOARD rows)
for c in comps - {"esp32_s3_touch_amoled_1_75", "knob_board"}:
    for t in ("s3", "p4", "knob"):
        if c not in linked[t]:
            raise SystemExit(f"{t} build tree lacks {c} — every first-party "
                             f"component but the two exclusions links on all "
                             f"three builds")
if "knob_board" in linked["s3"] or "knob_board" in linked["p4"]:
    raise SystemExit("knob_board linked on a clock build — the knob-only "
                     "managed deps (sh8601, esp_codec_dev) would leak into "
                     "the clock locks")
if "esp32_s3_touch_amoled_1_75" in linked["p4"] or \
        "esp32_s3_touch_amoled_1_75" in linked["knob"]:
    raise SystemExit("the vendored S3 BSP linked on the P4 or knob build — "
                     "the root EXCLUDE_COMPONENTS guards have been undone")
if "esp32_s3_touch_amoled_1_75" not in linked["s3"]:
    raise SystemExit("build/esp-idf lacks the vendored S3 BSP — the S3 bar's "
                     "'IN-TREE' segment is wrong")

# ------------------------------------------------------------- root CMakeLists
root_cm = _read("CMakeLists.txt")
proj_at = root_cm.index("project(moon-phase)")
p4_excl = root_cm.index('set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75;knob_board")')
knob_only_excl = root_cm.index('set(EXCLUDE_COMPONENTS "knob_board")')
s3_excl = root_cm.index('set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75")')
for name, i in (("the P4 block's EXCLUDE (BSP + knob_board)", p4_excl),
                ("the S3-clock block's EXCLUDE (knob_board)", knob_only_excl),
                ("the knob block's EXCLUDE (the vendored BSP)", s3_excl)):
    if i > proj_at:
        raise SystemExit(f"root CMakeLists: {name} moved after project() — the "
                         f"exclusions must predate the component manager")
lock_pins = [m.start() for m in
             re.finditer(r"idf_build_set_property\(DEPENDENCIES_LOCK", root_cm)]
if len(lock_pins) != 2 or any(i > proj_at for i in lock_pins):
    raise SystemExit("root CMakeLists: the two DEPENDENCIES_LOCK pins (p4 + "
                     "knob) must both predate project()")
if root_cm.index("idf_build_set_property(KNOB_BOARD ON)") > proj_at:
    raise SystemExit("root CMakeLists: the KNOB_BOARD promotion moved after "
                     "project() — the component manager's early pass would "
                     "not see the flag")

# ------------------------------------------------- the knob's answers (in-tree)
board_cm = _read(os.path.join("components", "board", "CMakeLists.txt"))
if 'set(BSP_COMPONENT knob_board)' not in board_cm or \
        'set(BOARD_EXTRA_INCLUDES "knob_include")' not in board_cm:
    raise SystemExit("components/board: the KNOB_BOARD branch no longer "
                     "dispatches to knob_board + knob_include")
imu_cm = _read(os.path.join("components", "imu", "CMakeLists.txt"))
if 'SRCS "imu_stub.c"' not in imu_cm:
    raise SystemExit("components/imu: the KNOB_BOARD branch no longer "
                     "compiles imu_stub.c")
knob_yml = _read(os.path.join("components", "knob_board", "idf_component.yml"))
for dep in ("espressif/esp_lcd_sh8601", "espressif/esp_codec_dev"):
    if f"{dep}:" not in knob_yml:
        raise SystemExit(f"knob_board idf_component.yml: {dep} no longer "
                         f"declared — the knob's managed answers changed")
if 'target == "esp32s3"' not in knob_yml:
    raise SystemExit("knob_board idf_component.yml: the target == esp32s3 "
                     "rule is gone — prepare_dependencies runs BEFORE kconfig, "
                     "so a $CONFIG rule could not resolve there")
kb_cs = sorted(f for f in os.listdir(os.path.join(ROOT, "components", "knob_board"))
               if f.endswith(".c"))
if not kb_cs:
    raise SystemExit("components/knob_board: no sources — the knob's board "
                     "answer vanished")
knob_board_loc = sum(_loc(os.path.join("components", "knob_board", f))
                     for f in kb_cs)
imu_stub = os.path.join("components", "imu", "imu_stub.c")
if 'return false' not in _read(imu_stub):
    raise SystemExit("imu_stub.c no longer returns the soft-fail posture — "
                     "the 'ready=false' annotation is dead")
if '"Silent"' not in _read(os.path.join("components", "knob_ui",
                                        "knob_alarm_audio_stub.c")):
    raise SystemExit("knob_alarm_audio_stub.c no longer names the sounds "
                     "'Silent' — the answer to the dropped alarm_audio.c changed")

# ------------------------------------------------------------ ui CMakeLists
ui_cm = _read(os.path.join("components", "ui", "CMakeLists.txt"))
m = re.search(r'set\(UI_BOARD_SRCS((?:\s*"[A-Za-z_0-9.]+"\s*)+)\)', ui_cm)
dropped = re.findall(r'"([A-Za-z_0-9.]+)"', m.group(1)) if m else []
if dropped != ["alarm_audio.c", "demo.c", "moon_ui.c", "moon_tfcache.c"]:
    raise SystemExit(f"ui/CMakeLists: UI_BOARD_SRCS drifted {dropped}")
m = re.search(r'set\(UI_CORE_SRCS((?:\s*"[A-Za-z_0-9.]+"\s*)+)\)', ui_cm)
core = re.findall(r'"([A-Za-z_0-9.]+)"', m.group(1)) if m else []
if core != ["ui.c", "screens.c", "images.c", "styles.c", "actions.c", "vars.c"]:
    raise SystemExit(f"ui/CMakeLists: UI_CORE_SRCS drifted {core}")
if not re.search(r'if\(KNOB_BOARD\)\s*\n\s*set\(UI_BOARD_SRCS ""\)', ui_cm):
    raise SystemExit("ui/CMakeLists: the KNOB_BOARD branch no longer empties "
                     "UI_BOARD_SRCS — the 'four TUs drop out' claim is dead")
for f in dropped + core:
    _loc(os.path.join("components", "ui", f))  # exists-check (SystemExit otherwise)
dropped_loc = {f: _loc(os.path.join("components", "ui", f)) for f in dropped}

# FONT_DIR: the tier map + the identical-name promise across tiers
for tier, d in (("fonts", "fonts"), ("fonts_p4", "fonts_p4"), ("fonts_knob", "fonts_knob")):
    if f'set(FONT_DIR "{tier}")' not in ui_cm:
        raise SystemExit(f"ui/CMakeLists: FONT_DIR branch for {tier} missing")
tiers = {}
for tier in ("fonts", "fonts_p4", "fonts_knob"):
    p = os.path.join(ROOT, "components", "ui", tier)
    tiers[tier] = {f for f in os.listdir(p) if f.endswith(".c")}
if not (tiers["fonts"] == tiers["fonts_p4"] == tiers["fonts_knob"]) or len(tiers["fonts"]) != 7:
    raise SystemExit("font tiers drifted apart — the identical lv_font_t-symbol "
                     "promise requires the same seven file names in all three tiers")

# ---------------------------------------------------- REQUIRES naming matrix
def _requires(path: str, only_public_block: bool = False) -> set:
    text = _strip_comments(_read(path))
    if only_public_block:  # ui: take REQUIRES but stop at PRIV_REQUIRES
        i = text.index("REQUIRES")
        j = text.index("PRIV_REQUIRES", i)
        text = text[i:j]
    else:
        i = text.index("REQUIRES")
        text = text[i:]
    toks = re.split(r"[\s()]+", text)
    return {t for t in toks if t in ("board", "imu", "esp_codec_dev")}

who_names = {
    "clock": _requires(os.path.join("components", "clock", "CMakeLists.txt")),
    "wifi": _requires(os.path.join("components", "wifi", "CMakeLists.txt")),
    "moon": _requires(os.path.join("components", "moon", "CMakeLists.txt")),
    "ui": _requires(os.path.join("components", "ui", "CMakeLists.txt"), True),
}
if who_names != {"clock": {"board"}, "wifi": set(), "moon": set(),
                 "ui": {"board", "imu", "esp_codec_dev"}}:
    raise SystemExit(f"REQUIRES naming drifted: {who_names} — the matrix is drawn "
                     f"from these literals (the early pass sees nothing else)")

# ------------------------------------------------------------------- lock sets
def _lock_keys(path: str) -> set:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: missing — the managed "
                         f"bars read the resolved dependency locks")
    keys = []
    for line in open(path, encoding="utf-8"):
        mm = re.match(r"^  ([A-Za-z0-9_./+-]+):\s*$", line)
        if mm:
            keys.append(mm.group(1))
    return set(keys) - {"idf"}

LOCK = {"s3": os.path.join(ROOT, "dependencies.lock"),
        "p4": os.path.join(ROOT, "dependencies.lock.p4"),
        "knob": os.path.join(ROOT, "dependencies.lock.knob")}
managed = {t: _lock_keys(p) for t, p in LOCK.items()}
for t, names in (("s3", {"espressif/esp_lcd_co5300", "waveshare/esp_lcd_touch_cst9217"}),
                 ("p4", {"waveshare/esp32_p4_wifi6_touch_lcd_xc", "espressif/esp_hosted",
                         "espressif/esp_wifi_remote"}),
                 ("knob", {"espressif/esp_lcd_sh8601", "espressif/esp_codec_dev",
                           "espressif/esp_io_expander", "waveshare/pcf85063a"})):
    missing = names - managed[t]
    if missing:
        raise SystemExit(f"{os.path.basename(LOCK[t])}: missing {sorted(missing)} — "
                         f"the per-build registry set changed shape")
if "espressif/esp_hosted" in managed["s3"] or "espressif/esp_lcd_co5300" in managed["p4"]:
    raise SystemExit("the target rules collapsed — hosted on S3 or co5300 on P4 "
                     "means the locks are no longer per-target")
for t in ("s3", "p4"):
    if "espressif/esp_lcd_sh8601" in managed[t]:
        raise SystemExit(f"{os.path.basename(LOCK[t])}: the knob-only sh8601 "
                         f"leaked into the {t} lock — the knob_board "
                         f"EXCLUDE_COMPONENTS guard is undone")
if managed["knob"] != {"espressif/cmake_utilities", "espressif/esp_codec_dev",
                       "espressif/esp_io_expander", "espressif/esp_lcd_sh8601",
                       "espressif/esp_new_jpeg", "lvgl/lvgl",
                       "waveshare/pcf85063a"}:
    raise SystemExit(f"knob lock set drifted: {sorted(managed['knob'])}")
vendored = {d for d in os.listdir(os.path.join(ROOT, "managed_components"))}
# The CI union state: the S3 set (the S3 build runs LAST) plus the P4-only
# gt911 (gen_perf_silicon) and the knob-only sh8601 (gen_perf_bus). Every
# other knob-lock entry sits in the S3 set already.
expected_union = {n.replace("/", "__") for n in managed["s3"]} | {
    "espressif__esp_lcd_touch_gt911", "espressif__esp_lcd_sh8601"}
if vendored != expected_union:
    raise SystemExit(f"managed_components/ is not the CI union state (S3 set + "
                     f"gt911 + sh8601): {sorted(vendored)}")
# The P4 page states the XC BSP arrives managed — check the linked set on disk:
for n in ("espressif__esp_hosted", "espressif__esp_wifi_remote",
          "waveshare__esp32_p4_wifi6_touch_lcd_xc"):
    if n not in linked["p4"]:
        raise SystemExit(f"build/p4/esp-idf lacks {n} — the P4 managed bar is wrong")
for n in ("espressif__esp_lcd_sh8601", "espressif__esp_codec_dev"):
    if n not in linked["knob"]:
        raise SystemExit(f"build/knob/esp-idf lacks {n} — the knob's managed "
                         f"answers never resolved")
if "espressif__esp_lcd_sh8601" in linked["s3"] or \
        "espressif__esp_lcd_sh8601" in linked["p4"]:
    raise SystemExit("sh8601 linked on a clock build — the knob's managed "
                     "answer leaked into a clock tree")

# ------------------------------------------------------------------- assembly
FAMILIES = [
    ("shared-4", STEEL), ("main", SLATE), ("board-side", TEAL),
    ("vendored BSP", BLUE), ("knob-native", VIOLET),
    ("managed (registry)", GREY),
]
COUNTS = {
    "s3":   {"shared-4": 4, "main": 1, "board-side": 2, "vendored BSP": 1,
             "knob-native": 0},
    "p4":   {"shared-4": 4, "main": 1, "board-side": 2, "vendored BSP": 0,
             "knob-native": 0},
    "knob": {"shared-4": 4, "main": 1, "board-side": 2, "vendored BSP": 0,
             "knob-native": 6},
}
for t in COUNTS:
    COUNTS[t]["managed (registry)"] = len(managed[t])
TOTALS = {t: sum(COUNTS[t].values()) for t in COUNTS}

plt.rcParams["font.family"] = "DejaVu Sans"

fig, (ax_l, ax_m, ax_r) = plt.subplots(1, 3, figsize=(12.8, 5.0))

# --------------------------------------------------------------- left: census
boards = ["s3", "p4", "knob"]
labels = {"s3": "S3 AMOLED\n(466, default target)", "p4": "P4 4C\n(720, sibling build)",
          "knob": "knob\n(360, -D KNOB_BOARD=ON)"}
ys = [0, 1, 2]
for y, t in zip(ys, boards):
    left = 0
    for fam, col in FAMILIES:
        n = COUNTS[t][fam]
        if n:
            ax_l.barh(y, n, left=left, height=0.58, color=col, edgecolor="white",
                      linewidth=0.7, zorder=3)
            ax_l.text(left + n / 2, y, str(n), ha="center", va="center",
                      fontsize=7, color="white", zorder=4, fontweight="bold")
        left += n
    ax_l.text(left + 0.35, y, f"{TOTALS[t]} parts", fontsize=8, color=INKC[t],
              va="center", fontweight="bold")
ax_l.set_yticks(ys)
ax_l.set_yticklabels([labels[b] for b in boards], fontsize=7.6)
for lab, t in zip(ax_l.get_yticklabels(), boards):
    lab.set_color(INKC[t])
ax_l.invert_yaxis()
ax_l.set_xlim(0, 36)
ax_l.set_xlabel("components under project control (IDF's own target-specific "
                "set is the floor, not counted)", fontsize=6.8)
ax_l.set_title("left: how many parts make each image\n"
               "(the same shared-4 under every bar)", loc="left", fontsize=8.6)
ax_l.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_l.set_axisbelow(True)
for s in ("top", "right"):
    ax_l.spines[s].set_visible(False)
notes = {
    "s3": "the vendored AMOLED BSP is IN-TREE (the managed\nname is shadowed; "
          "the 25-row patch lives here)",
    "p4": "EXCLUDE_COMPONENTS removes the vendored BSP\n+ knob_board; the XC "
          "BSP + hosted +\nwifi_remote arrive MANAGED instead",
    "knob": "the shared board/imu answer with their\nKNOB_BOARD rows; the "
            "vendored S3 BSP is\nEXCLUDED from the tree",
}
ax_l.set_ylim(3.05, -0.55)
for y, t in zip(ys, boards):
    ax_l.text(0.5, y + 0.33, notes[t], fontsize=5.8, color=MUTED, va="top")

# --------------------------------------------------- middle: what the seam costs
board_loc = _loc(os.path.join("components", "board", "board_display.c"))
rows = [
    ("moon_ui.c", dropped_loc["moon_ui.c"], AMBER,
     "not compiled for the knob — answered by knob_moon_render.c"),
    ("demo.c", dropped_loc["demo.c"], AMBER,
     "not compiled — 'no knob consumers' (ui comment)"),
    ("moon_tfcache.c", dropped_loc["moon_tfcache.c"], AMBER,
     "not compiled — no SD socket on the knob"),
    ("alarm_audio.c", dropped_loc["alarm_audio.c"], AMBER,
     "not compiled — answered by knob_alarm_audio_stub.c: 'Silent'"),
    ("board answer (clock)", board_loc, STEEL,
     "the clocks' answer: board_display.c over the BSP"),
    ("board answer (knob)", knob_board_loc, VIOLET,
     "the knob's answer: knob_board's own SH8601 bring-up (3 TUs)"),
    ("imu_stub.c (knob)", _loc(imu_stub), VIOLET,
     "the 'imu' answer: 'ready=false, gravity all-zero, roll 0'"),
    ("esp_codec_dev (knob)", 0, GREY,
     "a MANAGED dep in knob_board's yml (target == esp32s3\nrule) — 0 "
     "in-tree lines, its objects never link"),
]
answer_loc = board_loc + knob_board_loc + _loc(imu_stub)
ysm = list(range(len(rows)))
for y, (name, n, col, note) in zip(ysm, rows):
    if n == 0:
        ax_m.text(0.06, y, "0", fontsize=7.5, color=MUTED, style="italic")
    else:
        ax_m.barh(y, n, height=0.55, color=col, edgecolor="white", linewidth=0.7, zorder=3)
        ax_m.text(n + 40, y, f"{n:,}", fontsize=7, color=INK, va="center")
    ax_m.text(60, y + 0.30, note, fontsize=5.4, color=MUTED, va="top")
ax_m.set_yticks(ysm)
ax_m.set_yticklabels([r[0] for r in rows], fontsize=6.9)
ax_m.invert_yaxis()
ax_m.set_ylim(len(rows) - 0.35, -0.65)
ax_m.set_xlim(0, 4000)
ax_m.set_xlabel("lines of code (the four amber bars: the knob build never "
                f"compiles them — sum {sum(dropped_loc.values()):,})", fontsize=6.8)
ax_m.set_title(f"middle: what the seam prices — {sum(dropped_loc.values()):,} dropped,\n"
               f"{answer_loc:,} lines + a managed dep answer them",
               loc="left", fontsize=8.6)
ax_m.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_m.set_axisbelow(True)
for s in ("top", "right"):
    ax_m.spines[s].set_visible(False)

# ------------------------------------------------ right: who names whom + fonts
NAMES = ["board", "imu", "esp_codec_dev"]
COMPS = ["clock", "wifi", "moon", "ui"]
for r, comp in enumerate(COMPS):
    for c, name in enumerate(NAMES):
        hit = name in who_names[comp]
        ax_r.scatter([c], [r], s=190 if hit else 120,
                     marker="o", color=AMBER if hit else "none",
                     edgecolor=MUTED if hit else HAIR,
                     linewidth=1.4 if hit else 1.0, zorder=3)
ax_r.set_xticks(range(len(NAMES)))
ax_r.set_xticklabels(NAMES, fontsize=7, rotation=18, ha="right")
ax_r.set_yticks(range(len(COMPS)))
ax_r.set_yticklabels([f"{c} REQUIRES…" for c in COMPS], fontsize=7)
ax_r.invert_yaxis()
ax_r.set_xlim(-0.5, 2.5)
ax_r.set_ylim(5.9, -0.55)
ax_r.set_title("right: who names the three names", loc="left", fontsize=8.6)
ax_r.grid(color=HAIR, linewidth=0.5, zorder=0)
ax_r.set_axisbelow(True)
for s in ("top", "right", "left", "bottom"):
    ax_r.spines[s].set_visible(False)
# one tier per build, as a text stack below the matrix (three tiers exist,
# exactly one compiles — the FONT_DIR branch); placed below the rotated
# x tick labels, inside the widened ylim:
ax_r.text(-0.42, 3.95, "FONT_DIR (ui :22-28): one tier per build:",
          fontsize=6.4, color=INK, fontweight="bold", va="top")
for i, (lab, d, col) in enumerate((("S3", "fonts", BLUE), ("P4", "fonts_p4", VERM),
                                   ("knob", "fonts_knob", VIOLET))):
    ax_r.text(-0.30, 4.32 + i * 0.26, f"{lab}  →  {d}/  ·  {len(tiers[d])} .c files",
              fontsize=6.2, color=col, va="center")
ax_r.text(-0.30, 5.05, "same seven file names in all three tiers —\n"
                       "identical lv_font_t symbols, fonts.h unchanged",
          fontsize=5.8, color=MUTED, va="top")
ax_r.text(-0.30, 5.55, "filled dot = that component's REQUIRES names it",
          fontsize=5.8, color=MUTED, va="top")

fig.suptitle("The build census: how many parts make each board's image — and how many of them are silent",
             fontsize=9.5, color=INK, y=0.985)
fig.legend(handles=[Patch(facecolor=c, label=f) for f, c in FAMILIES],
           fontsize=6.4, loc="lower center", frameon=False, ncol=6,
           bbox_to_anchor=(0.28, 0.005))
fig.tight_layout(rect=(0, 0.05, 1, 0.96))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=150)
print(f"wrote {OUT}")
