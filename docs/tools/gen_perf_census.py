#!/usr/bin/env python3
"""Generate the build census for docs/verification/performance.rst — the
counting axis next to perf_storage (how many BYTES lie in the flash): **how
many PARTS make each board's image, and how many of them are silent**. The
binary page counts bytes, the storage ladder counts bytes lying still, and
build-seam.dot draws the SHAPE of the three-projects seam — nobody has yet
counted the parts that shape prices: the four shared components every board
carries, the board-side pair the knob never adds, the shims that answer its
names, the registry set each target resolves, and the 5,178 lines the knob
build drops instead of compiling.

Nothing is recalled. Every number is parsed at figure time with
SystemExit-failing parsers (the house pattern), and the parsers double as
docs-side twins of the build guards — the figure refuses to draw when the
seam's rules are removed:

  components/ + knob/components/ dirs    the first-party inventory, asserted
      against the exact expected sets (clock 7, knob 9) — the census is a
      directory listing, not a memory
  EXCLUDE_COMPONENTS (root :20)          must literally name the vendored S3
      BSP and sit BEFORE project() (:31) — assert the exclusion the P4 page
      states; build/p4/esp-idf must NOT contain the dir, and build/esp-idf
      must contain all eight clock-side first-party components
  DEPENDENCIES_LOCK before project()     both per-target pins (root :27-28,
      knob :47) — the figure dies if the lock split is undone
  EXTRA_COMPONENT_DIRS (knob :36-41)     four entries, each an existing
      component dir named clock/wifi/moon/ui; the parent components/ is
      asserted absent from the list (IDF 5.5.1 excludes by NAME only — the
      CLAUDE.md invariant)
  KNOB_BOARD before project(knob)        the PROJECT-variable-before-project
      rule (knob :23 before :49)
  UI_BOARD_SRCS / UI_CORE_SRCS (ui)      exactly the four dropped TUs and
      the six unchanged ones; the KNOB branch must empty the four
      (ui :31-35); every named file must exist
  FONT_DIR branch (ui :14-20)            fonts/fonts_p4/fonts_knob map; the
      three tiers carry the SAME seven file names (the identical
      lv_font_t-symbol promise) — each build compiles exactly ONE tier
  REQUIRES names                         parsed from the four shared
      CMakeLists: clock names board, ui names board+imu+esp_codec_dev,
      wifi and moon name NONE of the three — the exact matrix this figure
      draws; the literals are what the EARLY pass sees
  the three dependency locks             top-level keys minus idf, per
      target: S3 21 (carries esp_lcd_co5300 + the CST9217 touch, NO
      esp_hosted), P4 24 (carries the XC BSP + esp_hosted +
      esp_wifi_remote, NO co5300/CST9217 — the target rules hold), knob 6
      (asserted name-for-name, and the on-disk knob/managed_components/
      must equal the lock set — registry drift fails the figure)
  LOC counts                             the four dropped TUs, the clock's
      own 28-line board shim, the knob's 16-line board shim and 28-line
      imu_stub.c; the esp_codec_dev shim must contain exactly ONE file
      (its CMakeLists — "a CMakeLists file and NOTHING ELSE", zero sources,
      zero headers — drawn italic *no such leg*)
  the knob's honest stubs                knob_alarm_audio_stub.c must still
      return "Silent" (the answer to the dropped alarm_audio.c)

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob violet.
Segment families keep one colour across all three bars; the shims are the
amber of build-seam.dot (the seam's price), managed the registry grey.
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
TEAL = "#0f766e"      # board-side (clock family: real board shim + IMU)
GREY = "#9ca3af"      # manager-vendored (the registry's problem)

INKC = {t: c for t, c in (("s3", BLUE), ("p4", VERM), ("knob", VIOLET))}


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — this "
                         f"figure reads the CMakeLists files, the three "
                         f"dependency locks, the font tiers and the two "
                         f"clock build trees")
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
# The component inventories, asserted against the exact expected sets.
comps = _dirs("components")
if comps != {"board", "clock", "esp32_s3_touch_amoled_1_75", "imu", "moon", "ui", "wifi"}:
    raise SystemExit(f"components/ inventory drifted: {sorted(comps)} — the census "
                     f"expects the clock's seven first-party components")
knob_comps = _dirs(os.path.join("knob", "components"))
if knob_comps != {"board", "esp_codec_dev", "imu", "knob_battery", "knob_board",
                  "knob_moon", "knob_power", "knob_time", "knob_ui"}:
    raise SystemExit(f"knob/components/ inventory drifted: {sorted(knob_comps)} — the "
                     f"census expects six knob-native components plus the three shims")
if not os.path.exists(os.path.join(ROOT, "main", "main.cpp")):
    raise SystemExit("main/main.cpp: the clock project's app TU is missing")
if not os.path.exists(os.path.join(ROOT, "knob", "main", "main.c")):
    raise SystemExit("knob/main/main.c: the knob project's app TU is missing")
for t, d in (("s3", "build/esp-idf"), ("p4", "build/p4/esp-idf")):
    if not os.path.isdir(os.path.join(ROOT, d)):
        raise SystemExit(f"{d}/: missing — build the target first; the census "
                         f"cross-checks the linked component sets on disk")
s3_linked = set(os.listdir(os.path.join(ROOT, "build", "esp-idf")))
p4_linked = set(os.listdir(os.path.join(ROOT, "build", "p4", "esp-idf")))
for c in comps:  # every clock-side first-party component really links on S3
    if c not in s3_linked:
        raise SystemExit(f"build/esp-idf lacks {c} — the S3 census is wrong")
if "esp32_s3_touch_amoled_1_75" in p4_linked:
    raise SystemExit("build/p4/esp-idf contains the vendored S3 BSP — the P4 "
                     "EXCLUDE_COMPONENTS guard has been undone")

# ------------------------------------------------------------- root CMakeLists
root_cm = _read("CMakeLists.txt")
proj_at = root_cm.index("project(moon-phase)")
m = re.search(r'set\(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75"\)', root_cm)
if not m:
    raise SystemExit("root CMakeLists: the EXCLUDE_COMPONENTS literal no longer "
                     "names the vendored S3 BSP — the figure states it verbatim")
if root_cm.index('set(EXCLUDE_COMPONENTS') > proj_at:
    raise SystemExit("root CMakeLists: EXCLUDE_COMPONENTS moved after project() — "
                     "the P4 block must predate the component manager")
if root_cm.index("idf_build_set_property(DEPENDENCIES_LOCK") > proj_at:
    raise SystemExit("root CMakeLists: the P4 DEPENDENCIES_LOCK pin moved after "
                     "project() — 'which runs the component manager' (:24-26)")

# ------------------------------------------------------------- knob CMakeLists
knob_cm = _read(os.path.join("knob", "CMakeLists.txt"))
knob_proj = knob_cm.index("project(knob)")
if knob_cm.index("set(KNOB_BOARD ON)") > knob_proj:
    raise SystemExit("knob/CMakeLists.txt: KNOB_BOARD is set AFTER project() — "
                     "the PROJECT variable must predate the early pass")
if knob_cm.index("idf_build_set_property(DEPENDENCIES_LOCK") > knob_proj:
    raise SystemExit("knob/CMakeLists.txt: the lock pin moved after project()")
m = re.search(r'set\(EXTRA_COMPONENT_DIRS(.*?)\)\n', knob_cm, re.S)
if not m:
    raise SystemExit("knob/CMakeLists.txt: EXTRA_COMPONENT_DIRS block not found")
entries = re.findall(r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./components/([A-Za-z_0-9]+)"',
                     m.group(1))
if sorted(entries) != ["clock", "moon", "ui", "wifi"]:
    raise SystemExit(f"knob/CMakeLists.txt: EXTRA_COMPONENT_DIRS drifted {entries} "
                     f"— the seam is four individually-listed dirs")
if re.search(r'EXTRA_COMPONENT_DIRS[^)]*"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./components"\s*\)',
             knob_cm):
    raise SystemExit("knob/CMakeLists.txt: the PARENT components/ was added — "
                     "IDF excludes by name only; the shims would collide")
for d in entries:
    p = os.path.join(ROOT, "components", d, "CMakeLists.txt")
    if not os.path.exists(p):
        raise SystemExit(f"components/{d}: listed in EXTRA_COMPONENT_DIRS but is no "
                         f"component (no CMakeLists.txt)")

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

# --------------------------------------------------------------- shim anatomy
shim_codec = os.path.join(ROOT, "knob", "components", "esp_codec_dev")
codec_files = [os.path.join(dp, f) for dp, _, fs in os.walk(shim_codec) for f in fs]
if [os.path.basename(f) for f in codec_files] != ["CMakeLists.txt"]:
    raise SystemExit(f"knob esp_codec_dev shim grew files: {codec_files} — the "
                     f"'name only, CMakeLists and NOTHING ELSE' fact is dead")
imu_stub = os.path.join("knob", "components", "imu", "imu_stub.c")
if 'return false' not in _read(imu_stub):
    raise SystemExit("imu_stub.c no longer returns the soft-fail posture — "
                     "the 'ready=false' annotation is dead")
if '"Silent"' not in _read(os.path.join("knob", "components", "knob_ui",
                                        "knob_alarm_audio_stub.c")):
    raise SystemExit("knob_alarm_audio_stub.c no longer names the sounds "
                     "'Silent' — the answer to the dropped alarm_audio.c changed")

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
        "knob": os.path.join(ROOT, "knob", "dependencies.lock.knob")}
managed = {t: _lock_keys(p) for t, p in LOCK.items()}
for t, names in (("s3", {"espressif/esp_lcd_co5300", "waveshare/esp_lcd_touch_cst9217"}),
                 ("p4", {"waveshare/esp32_p4_wifi6_touch_lcd_xc", "espressif/esp_hosted",
                         "espressif/esp_wifi_remote"}),
                 ("knob", {"espressif/esp_lcd_sh8601", "espressif/esp_io_expander",
                           "waveshare/pcf85063a"})):
    missing = names - managed[t]
    if missing:
        raise SystemExit(f"{os.path.basename(LOCK[t])}: missing {sorted(missing)} — "
                         f"the per-target registry set changed shape")
if "espressif/esp_hosted" in managed["s3"] or "espressif/esp_lcd_co5300" in managed["p4"]:
    raise SystemExit("the target rules collapsed — hosted on S3 or co5300 on P4 "
                     "means the locks are no longer per-target")
if managed["knob"] != {"espressif/cmake_utilities", "espressif/esp_io_expander",
                       "espressif/esp_lcd_sh8601", "espressif/esp_new_jpeg",
                       "lvgl/lvgl", "waveshare/pcf85063a"}:
    raise SystemExit(f"knob lock set drifted: {sorted(managed['knob'])}")
vendored = {d for d in os.listdir(os.path.join(ROOT, "knob", "managed_components"))}
if vendored != {n.replace("/", "__") for n in managed["knob"]}:
    raise SystemExit(f"knob/managed_components/ disagrees with the knob lock: "
                     f"{sorted(vendored)}")
# The P4 page states the XC BSP arrives managed — check the linked set on disk:
for n in ("espressif__esp_hosted", "espressif__esp_wifi_remote",
          "waveshare__esp32_p4_wifi6_touch_lcd_xc"):
    if n not in p4_linked:
        raise SystemExit(f"build/p4/esp-idf lacks {n} — the P4 managed bar is wrong")

# ------------------------------------------------------------------- assembly
FAMILIES = [
    ("shared-4", STEEL), ("main", SLATE), ("board-side", TEAL),
    ("vendored BSP", BLUE), ("knob-native", VIOLET), ("shims", AMBER),
    ("managed (registry)", GREY),
]
COUNTS = {
    "s3":   {"shared-4": 4, "main": 1, "board-side": 2, "vendored BSP": 1,
             "knob-native": 0, "shims": 0},
    "p4":   {"shared-4": 4, "main": 1, "board-side": 2, "vendored BSP": 0,
             "knob-native": 0, "shims": 0},
    "knob": {"shared-4": 4, "main": 1, "board-side": 0, "vendored BSP": 0,
             "knob-native": 6, "shims": 3},
}
for t in COUNTS:
    COUNTS[t]["managed (registry)"] = len(managed[t])
TOTALS = {t: sum(COUNTS[t].values()) for t in COUNTS}

plt.rcParams["font.family"] = "DejaVu Sans"

fig, (ax_l, ax_m, ax_r) = plt.subplots(1, 3, figsize=(12.8, 5.0))

# --------------------------------------------------------------- left: census
boards = ["s3", "p4", "knob"]
labels = {"s3": "S3 AMOLED\n(466, default target)", "p4": "P4 4C\n(720, sibling build)",
          "knob": "knob\n(sibling PROJECT)"}
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
    "p4": "EXCLUDE_COMPONENTS removes the vendored BSP (:20);\nthe XC BSP + hosted "
          "+ wifi_remote arrive MANAGED instead",
    "knob": "board/imu/BSP never added — three same-named\nshims answer what "
            "clock/ui REQUIRES",
}
ax_l.set_ylim(3.05, -0.55)
for y, t in zip(ys, boards):
    ax_l.text(0.5, y + 0.33, notes[t], fontsize=5.8, color=MUTED, va="top")

# --------------------------------------------------- middle: what the seam costs
rows = [
    ("moon_ui.c", dropped_loc["moon_ui.c"], AMBER,
     "not compiled for the knob — answered by knob_moon_render.c"),
    ("demo.c", dropped_loc["demo.c"], AMBER,
     "not compiled — 'no knob consumers' (ui comment)"),
    ("moon_tfcache.c", dropped_loc["moon_tfcache.c"], AMBER,
     "not compiled — no SD socket on the knob"),
    ("alarm_audio.c", dropped_loc["alarm_audio.c"], AMBER,
     "not compiled — answered by knob_alarm_audio_stub.c: 'Silent'"),
    ("board shim (clock)", _loc(os.path.join("components", "board", "board_display.c")),
     STEEL, "the whole 'board' the clocks answer: 28 lines over the BSP"),
    ("board shim (knob)", _loc(os.path.join("knob", "components", "board",
                                            "board_display.c")), VIOLET,
     "the knob's answer to the same name: 16 lines"),
    ("imu_stub.c (knob)", _loc(imu_stub), VIOLET,
     "the 'imu' answer: 'ready=false, gravity all-zero, roll 0'"),
    ("esp_codec_dev shim (knob)", 0, AMBER,
     "name only: 'a CMakeLists file and NOTHING ELSE' — 0 sources, 0 headers"),
]
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
ax_m.set_xlim(0, 3450)
ax_m.set_xlabel("lines of code (the four amber bars: the knob build never "
                f"compiles them — sum {sum(dropped_loc.values()):,})", fontsize=6.8)
ax_m.set_title(f"middle: what the seam prices — {sum(dropped_loc.values()):,} dropped,\n"
               f"{_loc(os.path.join('knob', 'components', 'board', 'board_display.c')) + _loc(imu_stub)} "
               "lines + one CMakeLists answer them", loc="left", fontsize=8.6)
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
ax_r.text(-0.42, 3.95, "FONT_DIR (ui :14-20): one tier per build:",
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
           fontsize=6.4, loc="lower center", frameon=False, ncol=7,
           bbox_to_anchor=(0.28, 0.005))
fig.tight_layout(rect=(0, 0.05, 1, 0.96))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=150)
print(f"wrote {OUT}")
