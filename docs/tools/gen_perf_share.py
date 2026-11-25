#!/usr/bin/env python3
"""Generate the sharing ladder for docs/verification/performance.rst — the
counting axis the build census left unpriced: the census counts PARTS per
image (how many components each board's image assembles from); this ladder
counts IMAGES per part — how many of the twelve link images compile each
repo-owned source, and how many sources each image compiles. Twelve
images: three device firmwares, three per-target gate simulators, one
moon_bench in each of those same three target build dirs (one
add_executable pair, three configure targets), and three uisim panel
builds.

Nothing is recalled. Every number is parsed at figure time with
SystemExit-failing parsers, and the parsers double as docs-side twins of the
family's founding invariant — the figure REFUSES to draw the moment the core
stops being shared verbatim between the device component and the host tools:

  core set-equality (the founding invariant)
      components/moon/CMakeLists.txt SRCS == host_sim/CMakeLists.txt
      MOON_CORE_SRCS == host_sim/uisim/build.sh's explicit core list — the
      six math TUs and the eleven ERFA TUs, name-for-name on all three
      sides. "shared verbatim" (CLAUDE.md) becomes a machine-checked set
      equality: the day someone forks the core for the host, the figure dies.
  the moon component's 19 = 17 core + 2 device glue
      esp/moon_esp_assets.c (albedo from the moon_assets raw partition) +
      esp/moon_esp_movie.c (the boot movie's flash reader) are the only
      non-core SRCS entries; the host side
      carries host/moon_host_assets.c + the generated per-target asset table
  the twelve images                     exactly two add_executable targets in
      host_sim/CMakeLists.txt (moon_sim, moon_bench — dark_test.c,
      newmoon_test.c, viewport_test.c and phase_dummy_test.c are asserted
      present and ABSENT from the build file: throwaway probes, drawn at
      zero, each rides its own wave's commit — viewport_test the tz wave's,
      phase_dummy_test the compute-progress wave's — and the zero row must
      stay tracked in git), configured once per
      target build dir so each executable exists three times; host_sim/
      pole_test is asserted to be a stale gitignored ELF with no source
      behind it
  uisim/build.sh                         the explicit TU list is parsed and the
      ABSENCES asserted with it: screens.c/styles.c/images.c yes, ui.c /
      actions.c / vars.c / the four board TUs / moon_esp_assets.c / clock /
      wifi NO; the three-panel case map picks the font tier + asset target;
      the vendored-LVGL row is the find's count, drivers/ excluded, cited
      because the device side is LVGL-Kconfig-gated (hatched = cited)
  components/ui                          UI_CORE_SRCS six, UI_BOARD_SRCS four
      (the amber seam price — compiled for two boards, never the knob), the
      KNOB branch empties the four, FONT_DIR maps the three tiers, the
      images/*.c glob is asserted EMPTY (the images/ dir does not exist —
      the masks are hand-rasterised into images.c)
  clock / wifi / apps                    clock's eleven TUs by name, wifi.c,
      main/main.cpp (two boards), knob/main/main.c (one)
  knob/components/                       per-component SRCS parsed into an exact
      dict (board 1, imu 1, esp_codec_dev 0 — the register() call must be
      EMPTY — battery 1, board 3, moon 1, power 1, time 1, ui 5); the knob's
      fifteen TUs compile into exactly ONE image each
  knob/CMakeLists.txt                    EXTRA_COMPONENT_DIRS still the four dirs
      name-for-name, parent components/ still absent (the census's rule,
      re-asserted here so both twins die together)

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob violet;
TEAL is the host world (the gate's own images), GREY hatch the vendored
recompile (cited), pale dotted the tracked-but-never-linked probes.

Run:  docs/.venv/bin/python docs/tools/gen_perf_share.py
"""
from __future__ import annotations

import os
import re
import subprocess

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_share.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity — and here: the portable core itself
VERM = "#c2410c"      # P4 identity
VIOLET = "#7c3aed"    # knob identity
TEAL = "#0f766e"      # the host world (the gate's own executables)
SLATE = "#4b5563"     # clock-side device sources (clock/wifi/apps/glue)
STEEL = "#5c7fb8"     # shared UI TUs (the six core + the rendered three + fonts)
AMBER = "#b07d10"     # the seam price: the four board-only UI TUs
GREY = "#9ca3af"      # vendored (the registry's problem — cited hatched)
GHOST = "#9aa0aa"     # tracked, never linked


def _read(rel: str) -> str:
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        raise SystemExit(f"{rel}: not found — the sharing ladder reads the real "
                         f"build files, not a memory of them")
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


# ------------------------------------------------------------- the core truth
CORE_MATH = ["moon_astro.c", "moon_viewport.c", "moon_bake.c", "moon_render.c",
             "moon_fixp.c", "moon_event.c"]
ERFA = ["moon98.c", "epv00.c", "s2pv.c", "pfw06.c", "obl06.c", "rx.c", "rz.c",
        "cp.c", "ir.c", "rxp.c", "rxpv.c"]

moon_cm = _read(os.path.join("components", "moon", "CMakeLists.txt"))
m = re.search(r'idf_component_register\(\s*SRCS(.*?)INCLUDE_DIRS', moon_cm, re.S)
if not m:
    raise SystemExit("components/moon: the SRCS block vanished — the 19-file "
                     "inventory (17 core + 2 device glue) cannot be parsed")
moon_srcs = re.findall(r'"?([A-Za-z_0-9/.]+\.c)"?', m.group(1))
core_dev = [s for s in moon_srcs if s.startswith("core/") and "erfa" not in s]
erfa_dev = [s for s in moon_srcs if "core/erfa/" in s]
glue_dev = [s for s in moon_srcs if s.startswith("esp/")]
if sorted(os.path.basename(s) for s in core_dev) != sorted(CORE_MATH):
    raise SystemExit(f"components/moon core set drifted {core_dev} — the six "
                     f"math TUs are the figure's top bar")
if sorted(os.path.basename(s) for s in erfa_dev) != sorted(ERFA):
    raise SystemExit(f"components/moon ERFA set drifted {erfa_dev}")
if glue_dev != ["esp/moon_esp_assets.c", "esp/moon_esp_movie.c"]:
    raise SystemExit(f"components/moon device glue drifted {glue_dev} — the "
                     f"esp/ entries are the only non-core SRCS")

host_cm = _read(os.path.join("host_sim", "CMakeLists.txt"))
m = re.search(r'set\(MOON_CORE_SRCS(.*?)\)\s*\n', host_cm, re.S)
if not m:
    raise SystemExit("host_sim: MOON_CORE_SRCS gone — the verbatim-sharing claim "
                     "has nothing to be checked against")
host_srcs = re.findall(r'"?\$?\{?MOON_CORE\}?/?([A-Za-z_0-9/.]+\.c)"?', m.group(1))
core_host = [os.path.basename(s) for s in host_srcs
             if "erfa" not in s and s.startswith("core/")]
erfa_host = [os.path.basename(s) for s in host_srcs if "erfa" in s]
if sorted(core_host) != sorted(CORE_MATH) or sorted(erfa_host) != sorted(ERFA):
    raise SystemExit("the host no longer links the EXACT device core set — "
                     "'shared verbatim' (CLAUDE.md) has broken: "
                     f"{sorted(set(core_dev) ^ set(core_host))}")
exes = re.findall(r'add_executable\((\w+)', host_cm)
if sorted(exes) != ["moon_bench", "moon_sim"]:
    raise SystemExit(f"host_sim links {exes} — the figure models exactly two "
                     f"linked executables (moon_sim, moon_bench)")
for probe in ("dark_test", "newmoon_test", "viewport_test", "phase_dummy_test"):
    if probe in host_cm:
        raise SystemExit(f"{probe} is now linked by CMakeLists — it is no longer "
                         f"a throwaway probe; move it off the zero row")
    if not os.path.exists(os.path.join(ROOT, "host_sim", probe + ".c")):
        raise SystemExit(f"host_sim/{probe}.c is gone — the dashed zero row "
                         f"documents a file that no longer exists")
tracked = subprocess.run(["git", "-C", ROOT, "ls-files", "host_sim"],
                         capture_output=True, text=True).stdout.splitlines()
# dark/newmoon are the proven probes; viewport_test joined the zero row
# with the 2026-11-17 tz wave and phase_dummy_test with the 2026-11-24
# compute-progress wave (each rides its own wave's commit; their
# existence and absence from CMakeLists are asserted above either way)
for probe in ("host_sim/dark_test.c", "host_sim/newmoon_test.c",
              "host_sim/viewport_test.c", "host_sim/phase_dummy_test.c"):
    if probe not in tracked:
        raise SystemExit(f"{probe} is untracked — 'tracked, never linked' would "
                         f"be wrong; the row's claim is git ls-files")
# the stale probe residue: an ELF with no source behind it, gitignored
if os.path.exists(os.path.join(ROOT, "host_sim", "pole_test")):
    if os.path.exists(os.path.join(ROOT, "host_sim", "pole_test.c")):
        raise SystemExit("pole_test.c exists — the stale-ELF note is now wrong")
    ci = subprocess.run(["git", "-C", ROOT, "check-ignore", "host_sim/pole_test"],
                        capture_output=True, text=True)
    if ci.returncode != 0:
        raise SystemExit("host_sim/pole_test is not gitignored — an untracked "
                         "binary would show up in git status, and the note "
                         "documents an ignored residue")
    STALE_ELF = True
else:
    STALE_ELF = False

# --------------------------------------------------------------- uisim build
uisim_sh = _read(os.path.join("host_sim", "uisim", "build.sh"))
for must in ("$UI/screens.c", "$UI/styles.c", "$UI/images.c",
             "moon_astro.c", "moon_viewport.c", "moon_bake.c", "moon_render.c",
             "moon_fixp.c", "moon_event.c", "moon_host_assets.c", "$MOON_GEN_C",
             "$PWD/stubs.c", "$PWD/main.c", "$PWD/stb_impl.c"):
    if must not in uisim_sh:
        raise SystemExit(f"uisim/build.sh no longer compiles {must} — the 32-TU "
                         f"bar was parsed from this list")
for never in ("$UI/ui.c", "$UI/actions.c", "$UI/vars.c", "$UI/moon_ui.c",
              "$UI/demo.c", "$UI/moon_tfcache.c", "$UI/alarm_audio.c",
              "moon_esp_assets.c", "clock.c", "wifi.c"):
    if never in uisim_sh:
        raise SystemExit(f"uisim/build.sh now compiles {never} — the sim has "
                         f"grown a leg the ladder does not model")
if "! -path '*/drivers/*'" not in uisim_sh:
    raise SystemExit("uisim/build.sh: the LVGL find's drivers exclusion is gone — "
                     "the vendored row's count changes meaning")
lv_src = os.path.join(ROOT, "managed_components", "lvgl__lvgl", "src")
if not os.path.isdir(lv_src):
    raise SystemExit("managed_components/lvgl__lvgl/src: missing — the vendored "
                     "row counts the uisim's own find on disk")
LV_TUS = 0
for dirpath, dirs, files in os.walk(lv_src):
    if "drivers" in dirpath[len(lv_src):].split(os.sep):
        continue
    LV_TUS += sum(1 for f in files if f.endswith(".c"))
if LV_TUS < 300:
    raise SystemExit(f"LVGL TU count {LV_TUS} is implausibly small — the vendored "
                     f"bar cites the uisim find; check the walk")

# ------------------------------------------------------------- shared UI
ui_cm = _read(os.path.join("components", "ui", "CMakeLists.txt"))
m = re.search(r'set\(UI_BOARD_SRCS ((?:"[A-Za-z_.0-9]*"\s*)+)\)', ui_cm)
got = re.findall(r'"([^"]+)"', m.group(1)) if m else []
if got != ["alarm_audio.c", "demo.c", "moon_ui.c", "moon_tfcache.c"]:
    raise SystemExit(f"ui/CMakeLists: UI_BOARD_SRCS drifted {got} — the amber "
                     f"row compiles for exactly two boards by this literal")
if not re.search(r'if\(KNOB_BOARD\)\s*\n\s*set\(UI_BOARD_SRCS ""\)', ui_cm):
    raise SystemExit("ui/CMakeLists: the KNOB branch no longer empties the four "
                     "board TUs — their bar height (2) would be wrong")
if "set(UI_CORE_SRCS \"ui.c\" \"screens.c\" \"images.c\" \"styles.c\" \"actions.c\" \"vars.c\")" not in ui_cm:
    raise SystemExit("ui/CMakeLists: UI_CORE_SRCS drifted — the 6/3 split "
                     "(3 vs 6 images) is parsed from this literal")
if not os.path.isdir(os.path.join(ROOT, "components", "ui", "images")):
    pass  # the images/*.c glob must be EMPTY (hand-rasterised A8 masks)
else:
    raise SystemExit("components/ui/images/ exists again — IMAGE_SOURCES is no "
                     "longer an empty glob; the UI bars change")
for tier in ("fonts", "fonts_p4", "fonts_knob"):
    if f'set(FONT_DIR "{tier}")' not in ui_cm:
        raise SystemExit(f"ui/CMakeLists: FONT_DIR branch for {tier} missing")
tier_names = []
for tier in ("fonts", "fonts_p4", "fonts_knob"):
    p = os.path.join(ROOT, "components", "ui", tier)
    tier_names.append(frozenset(f for f in os.listdir(p) if f.endswith(".c")))
if len(tier_names[0]) != 7 or tier_names[0] != tier_names[1] or tier_names[1] != tier_names[2]:
    raise SystemExit("font tiers drifted apart — each tier compiles into TWO "
                     "images (device + its uisim panel); the row counts 21 files")

# --------------------------------------------------- clock side + knob side
clock_cm = _read(os.path.join("components", "clock", "CMakeLists.txt"))
m = re.search(r'SRCS((.*?)REQUIRES)', clock_cm, re.S)
clock_srcs = re.findall(r'"([A-Za-z_0-9.]+\.c)"', m.group(1)) if m else []
if sorted(clock_srcs) != sorted([
        "clock.c", "clock_timer.c", "clock_minute.c", "clock_interrupt.c",
        "clock_alarm.c", "clock_alarm_scheduler.c", "clock_cb.c", "clock_idle.c",
        "clock_sntp.c", "clock_tz.c", "settings_store.c"]):
    raise SystemExit(f"clock SRCS drifted {clock_srcs}")
wifi_cm = _read(os.path.join("components", "wifi", "CMakeLists.txt"))
if 'SRCS "wifi.c"' not in wifi_cm:
    raise SystemExit("wifi SRCS drifted — one TU, three images")

knob_cm = _read(os.path.join("knob", "CMakeLists.txt"))
m = re.search(r'set\(EXTRA_COMPONENT_DIRS(.*?)\)\n', knob_cm, re.S)
entries = re.findall(r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./components/([A-Za-z_0-9]+)"',
                     m.group(1)) if m else []
if sorted(entries) != ["clock", "moon", "ui", "wifi"]:
    raise SystemExit(f"knob EXTRA_COMPONENT_DIRS drifted {entries}")
if re.search(r'EXTRA_COMPONENT_DIRS[^)]*"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./components"\s*\)',
             knob_cm):
    raise SystemExit("the PARENT components/ was added to EXTRA_COMPONENT_DIRS — "
                     "the census and this ladder die together")
KNOB_SRCS = {
    "board": ["board_display.c"],
    "esp_codec_dev": [],
    "imu": ["imu_stub.c"],
    "knob_battery": ["knob_battery.c"],
    "knob_board": ["knob_board_display.c", "knob_board_touch.c", "knob_board_encoder.c"],
    "knob_moon": ["knob_moon.c"],
    "knob_power": ["knob_power.c"],
    "knob_time": ["knob_time.c"],
    "knob_ui": ["knob_ui.c", "knob_moon_render.c", "knob_alarm_audio_stub.c",
                "knob_dial.c", "knob_battery_ui.c"],
}
knob_total = 0
for comp, expected in KNOB_SRCS.items():
    cm = _read(os.path.join("knob", "components", comp, "CMakeLists.txt"))
    srcs = re.findall(r'"([A-Za-z_0-9.]+\.c)"', cm)
    if sorted(srcs) != sorted(expected):
        raise SystemExit(f"knob/components/{comp}: SRCS drifted {sorted(srcs)} "
                         f"vs {sorted(expected)}")
    for f in expected:
        if not os.path.exists(os.path.join(ROOT, "knob", "components", comp, f)):
            raise SystemExit(f"knob/components/{comp}/{f} named in SRCS but missing")
    knob_total += len(srcs)
if knob_total != 14:
    raise SystemExit(f"knob-native TU count {knob_total} != 14 — the violet "
                     f"single-build bar and the 58-TU knob bar both move")

# the generated per-target asset tables are real files on disk (gitignored)
for t in ("s3", "p4", "knob"):
    if not os.path.exists(os.path.join(ROOT, "assets", "images", "moon", "out",
                                       t, "moon_assets_gen.c")):
        raise SystemExit(f"assets .../out/{t}/moon_assets_gen.c missing — three "
                         f"of the twelve images link it per target; regenerate "
                         f"the assets")

# ------------------------------------------------------------- the model
# image ids: device firmwares, the three per-target gate sims, the bench,
# the three uisim panel builds. membership derived from the parsed facts.
S3, P4, KN = "s3", "p4", "knob"
SIM = ("sim_s3", "sim_p4", "sim_knob")
BENCHES = ("bench_s3", "bench_p4", "bench_knob")  # one per target build dir
UIS = ("uisim466", "uisim720", "uisim360")
DEV = (S3, P4, KN)
ALL = DEV + SIM + BENCHES + UIS
UISIM_OF = {"s3": "uisim466", "p4": "uisim720", "knob": "uisim360"}
SIM_OF = {"s3": "sim_s3", "p4": "sim_p4", "knob": "sim_knob"}
BENCH_OF = {"s3": "bench_s3", "p4": "bench_p4", "knob": "bench_knob"}

rows = []  # (label, n_files, images-set, colour, hatch?, style, note)

def add(label, n, images, colour, note="", hatch=False, dashed=False):
    rows.append(dict(label=label, n=n, imgs=list(images), colour=colour,
                     note=note, hatch=hatch, dashed=dashed))

add("core math (moon_astro/viewport/bake/render/fixp/event)", 6, ALL, BLUE,
    "the only repo-owned TUs in EVERY image")
add("vendored ERFA (moon98, epv00, ...)", 11, ALL, BLUE)
add("vendored LVGL — 412 TUs, cited", LV_TUS, DEV + UIS, GREY,
    "sim/bench never link LVGL; count = the uisim find (drivers/ excluded)",
    hatch=True)
add("screens.c + styles.c + images.c", 3, DEV + UIS, STEEL,
    "the shared home crosses to the host too")
add("moon_host_assets.c", 1, SIM + BENCHES + UIS, TEAL,
    "the host backend of the seam")
for t in ("s3", "p4", "knob"):
    add(f"moon_assets_gen.c — {t}", 1, [SIM_OF[t], BENCH_OF[t], UISIM_OF[t]], TEAL,
        "generated asset table, 3 images per target")
add("clock (11 TUs) — re-used whole by the knob", 11, DEV, SLATE)
add("wifi.c", 1, DEV, SLATE)
add("ui.c + actions.c + vars.c", 3, DEV, STEEL)
add("esp/moon_esp_assets.c — device backend", 1, DEV, SLATE)
add("esp/moon_esp_movie.c — the boot movie's flash reader", 1, DEV, SLATE)
add("uisim stubs.c + main.c + stb_impl.c", 3, UIS, TEAL)
add("sim main.c (one source, three sim builds)", 1, SIM, TEAL)
add("UI_BOARD_SRCS — the four the knob drops", 4, (S3, P4), AMBER,
    "compiled for two boards, never the knob")
for tier, dev, panel in (("fonts", S3, "uisim466"), ("fonts_p4", P4, "uisim720"),
                         ("fonts_knob", KN, "uisim360")):
    add(f"{tier}/ — one tier of 7", 7, (dev, panel), STEEL,
        "device + its matching panel, never the other tiers")
add("main.cpp (one source, both clocks)", 1, (S3, P4), SLATE)
add("bench.c (one source, three bench builds)", 1, BENCHES, TEAL,
    "moon_bench is linked into every target build dir")
add("knob main + native 14 (nine components)", 15, (KN,), VIOLET,
    "every knob-owned TU compiles ONCE")
add("throwaway probes dark_test.c / newmoon_test.c / viewport_test.c / "
    "phase_dummy_test.c", 4, (), GHOST,
    "tracked by git, called in code, linked by nobody"
    + (" — a stale pole_test ELF is all that remains of an even earlier probe" if STALE_ELF else ""),
    dashed=True)

# per-image repo-owned TU totals, recomputed from the model (never hand-summed)
per_image = {i: 0 for i in ALL}
for r in rows:
    if r["colour"] is GREY or r["colour"] is GHOST:
        continue  # vendored + never-linked live on their own axis
    for i in r["imgs"]:
        per_image[i] += r["n"]
EXPECTED = {S3: 49, P4: 49, KN: 59, "sim_s3": 20, "sim_p4": 20, "sim_knob": 20,
            "bench_s3": 20, "bench_p4": 20, "bench_knob": 20,
            "uisim466": 32, "uisim720": 32, "uisim360": 32}
for i, want in EXPECTED.items():
    if per_image[i] != want:
        raise SystemExit(f"{i} compiles {per_image[i]} repo-owned TUs, not {want} "
                         f"— the model and the build files disagree; fix the "
                         f"model or the claim, then rerun")

# ------------------------------------------------------------------ the figure
rows_sorted = sorted(rows, key=lambda r: (-len(r["imgs"]), r["label"]))
fig, (axL, axR) = plt.subplots(
    1, 2, figsize=(13.4, 7.4), gridspec_kw=dict(width_ratios=(7, 4.6)))

y = list(range(len(rows_sorted)))[::-1]
for yi, r in zip(y, rows_sorted):
    kw = dict(height=0.66, edgecolor="white", linewidth=0.6)
    if r["dashed"]:
        kw.update(facecolor="#f1f2f4", edgecolor="#8a8a8a", linestyle="--",
                  linewidth=1.2)
    else:
        kw.update(facecolor=r["colour"], linestyle="-")
        if r["hatch"]:
            kw.update(hatch="///", edgecolor=r["colour"])
    axL.barh(yi, max(len(r["imgs"]), 0.06), **kw)
    axL.text(len(r["imgs"]) + 0.16, yi, f"×{r['n']}  {r['note']}", va="center",
             fontsize=7.4, color=MUTED)
axL.set_yticks(y)
axL.set_yticklabels([r["label"] for r in rows_sorted], fontsize=8.6)
axL.set_xlim(0, 16.5)
axL.set_xticks(range(0, 13, 1))
axL.set_xlabel("link images that compile the file (of twelve)", fontsize=9.5, color=INK)
axL.set_title("IMAGES PER PART — one file, how many times the family compiles it",
              fontsize=11, color=INK, weight="bold")
axL.grid(axis="x", color=HAIR, linewidth=0.8)
axL.set_axisbelow(True)
for s in ("top", "right", "left"):
    axL.spines[s].set_visible(False)
axL.tick_params(colors=MUTED)
axL.axvline(12, color=BLUE, linewidth=0.9, linestyle=":")
axL.text(12, len(rows_sorted) - 0.25, "every image", fontsize=7.6, color=BLUE,
         ha="center")

img_order = list(DEV) + list(SIM) + list(BENCHES) + list(UIS)
nice = {S3: "S3 app", P4: "P4 app", KN: "knob app", "sim_s3": "moon_sim s3",
        "sim_p4": "moon_sim p4", "sim_knob": "moon_sim knob",
        "bench_s3": "moon_bench s3", "bench_p4": "moon_bench p4",
        "bench_knob": "moon_bench knob",
        "uisim466": "uisim 466", "uisim720": "uisim 720", "uisim360": "uisim 360"}
colours = {S3: BLUE, P4: VERM, KN: VIOLET}
ys = list(range(len(img_order)))[::-1]
for yi, i in zip(ys, img_order):
    c = colours.get(i, TEAL)
    axR.barh(yi, per_image[i], height=0.66, color=c, edgecolor="white",
             linewidth=0.6)
    extra = ""
    if i.startswith("uisim"):
        extra = f"  + {LV_TUS} vendored LVGL"
    axR.text(per_image[i] + 1.2, yi, f"{per_image[i]}{extra}", va="center",
             fontsize=8, color=MUTED)
axR.set_yticks(ys)
axR.set_yticklabels([nice[i] for i in img_order], fontsize=8.8)
axR.set_xlim(0, 95)
axR.set_xticks(range(0, 91, 20))
axR.set_xlabel("repo-owned TUs per image (vendored excluded)",
               fontsize=9.5, color=INK)
axR.set_title("PARTS PER IMAGE — the twelve bars",
              fontsize=11, color=INK, weight="bold")
axR.grid(axis="x", color=HAIR, linewidth=0.8)
axR.set_axisbelow(True)
for s in ("top", "right", "left"):
    axR.spines[s].set_visible(False)
axR.tick_params(colors=MUTED)

handles = [
    Patch(facecolor=BLUE, label="portable core (math + ERFA)"),
    Patch(facecolor=STEEL, label="shared UI (core six + rendered three + fonts)"),
    Patch(facecolor=SLATE, label="clock-side device TUs"),
    Patch(facecolor=AMBER, label="board-only UI (the knob drops these)"),
    Patch(facecolor=VIOLET, label="knob-native (main + 14)"),
    Patch(facecolor=TEAL, label="host tools (gate, bench, uisim)"),
    Patch(facecolor=GREY, hatch="///", label="vendored (cited)"),
    Patch(facecolor="none", edgecolor=GHOST, linestyle="--",
          label="tracked, never linked"),
]
fig.legend(handles=handles, loc="lower center", ncol=4, fontsize=8,
           frameon=False, bbox_to_anchor=(0.5, -0.005))
fig.suptitle(
    f"The sharing ladder: {len(CORE_MATH) + len(ERFA)} core TUs compile into all "
    f"{len(ALL)} images; the four board TUs top out at 2, the knob's 15 at 1",
    fontsize=12.5, color=INK, weight="bold", y=0.985)
fig.text(0.5, 0.935,
         "parsed live: core set-equality across the three build systems, the two "
         "linked host executables, the uisim's 32+412 list, the four dropped UI "
         "TUs, the knob's 14+1",
         ha="center", fontsize=8.6, color=MUTED)
fig.tight_layout(rect=(0, 0.055, 1, 0.925))
fig.savefig(OUT, dpi=150)
print(f"wrote {os.path.relpath(OUT, ROOT)} — {len(rows)} rows, "
      f"images={len(ALL)} (core in {len(rows[0]['imgs'])}/{len(ALL)}), "
      f"LVGL TUs={LV_TUS}, per-image totals {EXPECTED}")
