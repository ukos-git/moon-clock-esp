#!/usr/bin/env python3
"""Generate the configuration ladder for docs/verification/performance.rst —
the counting axis next to the build census (which counts PARTS and which of
them are silent): **how many choices each layer hands over, and how many of
them read no C file at all**. The census counts the components an image
assembles from; the storage ladder counts the bytes that lie in the flash;
nobody has yet counted the KNOBS — the entries each project's menuconfig
defines, which of them any compiled file ever reads, and the seven keys the
user actually owns on the card.

Nothing is recalled. Every number is parsed at figure time with
SystemExit-failing parsers (the house pattern), and the parsers double as
docs-side twins of the drift gate — the figure refuses to draw when a
documented option vanishes or a new one appears unread:

  the Kconfig sources              the first-party inventory by NAME:
      components/{ui,clock,moon}/Kconfig + components/board/Kconfig.projbuild
      + the six knob/components/*/Kconfig — every `config` symbol parsed,
      each file's set asserted name-for-name (a new or renamed option
      refuses to draw until this figure is re-read)
  the kconfig-*.inc fragments      every parsed name must appear backticked
      in the fragment that covers its source (the drift gate's coverage
      rule, re-asserted at figure time); the twelve vendored BSP options
      are asserted absent from every fragment (the "documented nowhere"
      claim the BSP bar makes)
  the three guard keys             still `int` with `default 0`, spelled as
      the registry needs ("They must be INT, not bool") — the figure dies
      if someone boolifies them (the eval_bool("1") fatal)
  the reader scan                  every name's CONFIG_<NAME> token grep-walked
      over components/ + main/ + knob/ (.c/.h/.cpp, the vendored BSP dir
      only for BSP names); each name's category (alive / silent-on-knob /
      dead letter / configure-only / registry-only / S3-only) asserted
      against the expected dict — if a silent option gains a shared reader
      the figure refuses to keep claiming it silent
  the DROPPED set                  the four UI_BOARD_SRCS + main/main.cpp +
      the host assets TU — exactly what the knob build never compiles;
      the classification's yardstick, re-parsed from components/ui/CMakeLists.txt
  settings_store.c                 exactly the seven settings.txt keys (the
      parse_kv set == the save block's set); the two #ifndef Kconfig seeds,
      the three constant seeds, the two LEGACY ghost seeds; the store's own
      "LEGACY no-ops" + "stale card ... must not break parsing" comments;
      the two timeouts' 60..1800 s clamp; the sound range twin (store's
      SETTINGS_ALARM_SOUND_MAX == alarm_audio.h ALARM_SOUND_COUNT == 4);
      the rotation key's 0..359 clamp
  screens.c                        the five writer sites by token (the sleep
      roller's RELEASED handler writes both timeouts, the tz commit, the
      sound commit, the display-trim commit); ZERO callers of
      settings_set_boot/dusk_anim_enable repo-wide (the ghost claim)
  getter readers                   settings_boot/dusk_anim_enable read by
      nobody (the ghosts), the five live keys read at least once outside
      the store
  the live sdkconfigs              every parsed name present in sdkconfig,
      sdkconfig.p4, or knob/sdkconfig (value or "is not set") — the menu's
      offer checked against what a real configure produced

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob violet.
Hatched = vendored or registry-consumed (the bus ladder's cited precedent);
grey = the dead letters, italic *no such reader*.

Run:  docs/.venv/bin/python docs/tools/gen_perf_config.py
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
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_config.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity / alive
VERM = "#c2410c"     # P4 identity
VIOLET = "#7c3aed"   # knob identity
AMBER = "#b07d10"    # the seam's price — reused for silent-on-knob knobs
STEEL = "#5c7fb8"    # UI menu
TEAL = "#0f766e"     # CLOCK menu
SLATE = "#4b5563"    # MOON menu
INDIGO = "#4338ca"   # BOARD menus
GREY = "#9ca3af"     # vendored / dead letters
LILAC = "#a78bfa"    # knob-native shims

MENU_COL = {
    "UI": STEEL, "CLOCK": TEAL, "MOON": SLATE, "BOARD": INDIGO,
    "BSP (vendored)": GREY,
    "board shim": INDIGO, "battery": LILAC, "power": LILAC,
    "ui shim": LILAC, "knob moon": LILAC, "knob time": LILAC,
}


def _read(path: str) -> str:
    p = os.path.join(ROOT, path)
    if not os.path.exists(p):
        raise SystemExit(f"{path}: not found — this figure reads the "
                         f"Kconfig sources, the fragments, the component "
                         f"sources, settings_store.c, screens.c and the "
                         f"live sdkconfigs")
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()


def _configs(path: str) -> list:
    return re.findall(r"^\s*config\s+(\w+)", _read(path), re.M)


# ------------------------------------------------------- the Kconfig sources
SOURCES = {  # menu label -> source file
    "UI": "components/ui/Kconfig",
    "CLOCK": "components/clock/Kconfig",
    "MOON": "components/moon/Kconfig",
    "BOARD": "components/board/Kconfig.projbuild",
}
KNOB_SOURCES = {  # menu label -> knob shim source
    "board shim": "knob/components/board/Kconfig",
    "battery": "knob/components/knob_battery/Kconfig",
    "power": "knob/components/knob_power/Kconfig",
    "knob time": "knob/components/knob_time/Kconfig",
    "ui shim": "knob/components/knob_ui/Kconfig",
    "knob moon": "knob/components/knob_moon/Kconfig",
}
BSP_SOURCE = "BSP (vendored)"

EXPECTED = {
    "UI": ["UI_DEEP_SLEEP_COUNTDOWN_SEC", "UI_LIGHT_SLEEP_TICK_MS",
           "UI_LIGHT_SLEEP_DIM_PCT", "UI_CLOCK_ALPHA_PCT", "UI_CLOCK_ROLL_ENABLE", "UI_ALARM_RING_MAX_MIN",
           "UI_DEMO_MODE", "UI_DEMO_SCREEN_SEC", "UI_DEMO_FULL_TOUR",
           "UI_MOON_HOURLY_ENABLE", "UI_MOON_HOURLY_FADE_MS", "UI_MOON_HOURLY_TFCACHE",
           "UI_MOON_BAKE_REVEAL", "TEST_HOURLY_FORCE_S", "UI_MOON_BOOT_REPLAY",
           "UI_MOON_REPLAY_FPS", "UI_MOON_REPLAY_BUDGET_MS", "UI_MOON_TFCACHE",
           "UI_MOON_TFCACHE_JPEG_Q", "TEST_AUTO_WAKE_SEC", "TEST_SETTINGS_DEMO_SEC",
           "TEST_SETTINGS_RETURN_SEC", "TEST_WIFI_SCAN_SEC", "TEST_CPU_STATS_SEC",
           "TEST_ROLL_TILT_SEC", "TEST_ALARM_RING_SEC", "TEST_HANG_WATCH",
           "TEST_BAKE_NO_RELIEF", "TEST_BAKE_REVEAL_TRACE", "UI_IMU_ENABLE",
           "UI_IMU_ACTIVITY_DEG",
           "UI_IMU_WAKE_DEG", "UI_IMU_ANGLE_SIGN", "UI_IMU_ROLL_OFFSET_DEG",
           "UI_CLOCK_ROLL_SIGN", "UI_ORI_BASE_V10", "UI_MOON_ORBIT_ENABLE",
           "UI_MOON_ORBIT_MAX_DEG", "UI_MOON_ORBIT_STEP_DEG",
           "UI_MOON_ORBIT_DEADZONE_DEG", "UI_MOON_ORBIT_LPF_ALPHA",
           "UI_MOON_OBSERVER_ENABLE", "UI_MOON_OBSERVER_SIGN",
           "UI_MOON_OBSERVER_DEADBAND_DEG",
           "UI_SCREEN_SWITCH_FADE"],
    "CLOCK": ["CLOCK_USE_PCF85063A", "CLOCK_TIME_FORMAT_24H", "CLOCK_TIME_FORMAT_12H",
              "CLOCK_DEFAULT_TIMEZONE", "CLOCK_NTP_SERVER",
              "CLOCK_NTP_SYNC_INTERVAL_HOURS", "CLOCK_NTP_TIMEOUT_MS",
              "CLOCK_IDLE_TIMEOUT_SEC", "CLOCK_IDLE_CHECK_INTERVAL_MS"],
    "MOON": ["MOON_ALBEDO_W"],
    "BOARD": ["BOARD_S3", "BOARD_P4", "ESP_HOSTED_HOST_USB_CDC_ACM_MANIFEST",
              "ESP_HOSTED_HOST_USB_CHERRYUSB_MANIFEST",
              "ESP_HOSTED_HOST_USB_RNDIS_MANIFEST"],
    "board shim": ["BOARD_KNOB", "BOARD_S3", "BOARD_P4"],
    "battery": ["KNOB_BATTERY_GAUGE", "KNOB_BATTERY_ADC_CHANNEL",
                "KNOB_BATTERY_DIV_NUM", "KNOB_BATTERY_DIV_DEN",
                "KNOB_BATTERY_MV_EMPTY", "KNOB_BATTERY_MV_FULL",
                "KNOB_BATTERY_CHARGING_MV", "KNOB_BATTERY_LOW_PCT"],
    "power": ["KNOB_DIM_AFTER_S", "KNOB_DIM_DUTY", "KNOB_ALARM_TIMER_WAKE",
              "KNOB_ALARM_GRACE_S", "KNOB_ALARM_LATE_MAX_S",
              "KNOB_ALARM_MAX_SLEEP_GAP_S", "KNOB_SLEEP_TIMEOUT_S",
              "KNOB_TOUCH_WAKE", "KNOB_SLEEP_ALLOW"],
    "knob time": ["KNOB_TIME_BOOT_SYNC_MS", "KNOB_TZ", "KNOB_TIME_USE_TEST_EPOCH"],
    "ui shim": ["KNOB_UI_ENABLE", "KNOB_UI_BAKE_REVEAL", "KNOB_DIAG_FRAME_TRACE",
                "KNOB_DIAG_PANIC_AT_FRAME", "KNOB_TEST_CLASSIFY_STRESS"],
    "knob moon": ["KNOB_MOON_TEST_EPOCH", "KNOB_DEMO_MODE", "KNOB_DEMO_PHASE_SEC"],
}

parsed: dict[str, list] = {}
for label, src in list(SOURCES.items()) + list(KNOB_SOURCES.items()):
    names = _configs(src)
    if names != EXPECTED[label]:
        raise SystemExit(f"{src}: config set drifted — {len(names)} entries, "
                         f"expected {len(EXPECTED[label])} name-for-name "
                         f"({sorted(set(names) ^ set(EXPECTED[label]))}); this "
                         f"figure prices the exact menu it enumerates")
    parsed[label] = names

# the vendored BSP menu: parsed but NOT expected-name-for-name pinned beyond
# the count + the undocumented claim (upstream's file, ours to count)
bsp_names = _configs("components/esp32_s3_touch_amoled_1_75/Kconfig")
if len(bsp_names) != 12:
    raise SystemExit(f"vendored BSP Kconfig: {len(bsp_names)} configs — the "
                     f"figure's BSP bar states twelve")
parsed[BSP_SOURCE] = bsp_names

# the three registry guard keys must stay INT defaults (bool -> eval_bool("1")
# hard error in the esp_hosted if-clause parser, per the source comment)
board_txt = _read(SOURCES["BOARD"])
for g in ("ESP_HOSTED_HOST_USB_CDC_ACM_MANIFEST",
          "ESP_HOSTED_HOST_USB_CHERRYUSB_MANIFEST",
          "ESP_HOSTED_HOST_USB_RNDIS_MANIFEST"):
    if not re.search(rf"config {g}\n\s*int\s*\n\s*default\s+0", board_txt):
        raise SystemExit(f"{g} is no longer `int` / `default 0` — the "
                         f"registry if-clauses fatal again (the comment's "
                         f"'They must be INT, not bool')")
if "They must be INT, not bool" not in board_txt:
    raise SystemExit("board Kconfig.projbuild lost the INT-not-bool comment — "
                     "the guard-key annotation cites it verbatim")

# --------------------------------------------------------------- the fragments
FRAG_DIR = os.path.join(ROOT, "docs", "reference", "_fragments")
frag_txt = ""
for fn in sorted(os.listdir(FRAG_DIR)):
    if fn.startswith("kconfig-") and fn.endswith(".inc"):
        with open(os.path.join(FRAG_DIR, fn), encoding="utf-8") as f:
            frag_txt += f.read() + "\n"
backticked = set(re.findall(r"``([A-Z][A-Z0-9_]+)``", frag_txt))
# the fragments abbreviate shared prefixes: ``KNOB_BATTERY_DIV_NUM`` / ``_DEN``
for line in frag_txt.splitlines():
    toks = re.findall(r"``([A-Z_][A-Z0-9_]*)``", line)
    last_full = None
    for t in toks:
        if t.startswith("_") and last_full:
            backticked.add(last_full.rsplit("_", 1)[0] + t)
        elif re.fullmatch(r"[A-Z][A-Z0-9_]+", t):
            last_full = t
backticked |= {b[len("CONFIG_"):] for b in backticked if b.startswith("CONFIG_")}
for label, src in list(SOURCES.items()) + list(KNOB_SOURCES.items()):
    missing = [n for n in parsed[label] if n not in backticked]
    if missing:
        raise SystemExit(f"{src}: {missing} no longer backticked in any "
                         f"kconfig-*.inc fragment — the reference tables are "
                         f"single-sourced; fix the fragment, not this figure")
if any(n.startswith("BSP_") for n in backticked):
    raise SystemExit("a BSP_* name appeared in the kconfig fragments — the "
                     "BSP bar's 'documented in no table' claim is dead")

# --------------------------------------------------------- reader classification
DROPPED = {  # files the KNOB build never compiles
    "components/ui/moon_ui.c", "components/ui/demo.c",
    "components/ui/moon_tfcache.c", "components/ui/alarm_audio.c",
    "main/main.cpp", "components/moon/host/moon_host_assets.c",
}
for f in DROPPED:
    if not os.path.exists(os.path.join(ROOT, f)):
        raise SystemExit(f"{f}: missing — the DROPPED yardstick must name the "
                         f"four UI_BOARD_SRCS + the clock main + the host TU")
ui_cm = _read("components/ui/CMakeLists.txt")
m = re.search(r'set\(UI_BOARD_SRCS((?:\s*"[A-Za-z_0-9.]+"\s*)+)\)', ui_cm)
dropped_tus = set(f"components/ui/{f}" for f in re.findall(r'"([A-Za-z_0-9.]+)"', m.group(1)))
if dropped_tus != DROPPED - {"main/main.cpp", "components/moon/host/moon_host_assets.c"}:
    raise SystemExit(f"UI_BOARD_SRCS drifted: {sorted(dropped_tus)} — the "
                     f"silent-on-knob classification is computed from it")

C_SRC = []  # (repo-relative path, text) of every scannable source
SCAN_DIRS = ["components", "main", "knob"]
for base in SCAN_DIRS:
    for dp, _, fs in os.walk(os.path.join(ROOT, base)):
        rel_dp = os.path.relpath(dp, ROOT).replace(os.sep, "/")
        if any(x in rel_dp for x in ("build", "managed_components", "managed_components")):
            continue
        for f in fs:
            if f.endswith((".c", ".h", ".cpp")):
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
                with open(p, encoding="utf-8", errors="replace") as fh:
                    C_SRC.append((rel, fh.read()))
cm_txt = _read("CMakeLists.txt") + _read(os.path.join("knob", "CMakeLists.txt"))


def readers(name: str, only_in: str | None = None) -> set:
    """Files whose text contains the CONFIG_<name> token."""
    tok = f"CONFIG_{name}"
    hits = set()
    for rel, txt in C_SRC:
        if only_in and not rel.startswith(only_in):
            continue
        if not only_in and rel.startswith("components/esp32_s3_touch_amoled_1_75/"):
            continue  # the vendored dir only counts for BSP names
        if re.search(rf"\b{tok}\b", txt):
            hits.add(rel)
    return hits


def category(name: str, menu: str) -> str:
    if name.startswith("ESP_HOSTED_HOST_USB_"):
        if readers(name):
            raise SystemExit(f"{name} suddenly has a C reader — the "
                             f"'read by no C, only by the registry' claim died")
        return "registry"
    if name in ("MOON_ALBEDO_W", "BOARD_S3") and menu not in KNOB_SOURCES:
        if readers(name):
            raise SystemExit(f"{name} suddenly has a C reader — the "
                             f"configure-only claim died")
        if f"CONFIG_{name}" not in cm_txt:
            raise SystemExit(f"{name} no longer named in any CMakeLists — "
                             f"the configure-time reader is gone")
        return "configure"
    # a knob-shim option only exists in the knob build, so only knob/ files
    # can honour it; the vendored BSP dir only counts for BSP names
    if menu == BSP_SOURCE:
        rs = readers(name, "components/esp32_s3_touch_amoled_1_75/")
    elif menu in KNOB_SOURCES:
        rs = readers(name, "knob/")
    else:
        rs = readers(name)
    if not rs:
        return "dead"
    if menu == BSP_SOURCE:
        return "s3-only"
    if menu in KNOB_SOURCES:
        return "alive"
    if rs <= DROPPED:
        return "clock-only"
    return "alive"


CAT = {"alive": "alive", "clock-only": "silent", "dead": "dead",
       "configure": "configure", "registry": "registry", "s3-only": "s3-only"}
EXPECTED_CAT = {
    # 45 UI: twelve are read by files every build compiles (actions.c,
    # screens.c, settings_store.c, and — fade — even the knob's own
    # knob_moon_render.c); the moon/orbit/reveal block and the main-only
    # TEST hooks are silent on the knob; two are read nowhere.
    "UI_DEEP_SLEEP_COUNTDOWN_SEC": "alive", "UI_LIGHT_SLEEP_TICK_MS": "alive",
    "UI_LIGHT_SLEEP_DIM_PCT": "alive", "UI_CLOCK_ALPHA_PCT": "alive",
    "UI_CLOCK_ROLL_ENABLE": "alive", "UI_DEMO_MODE": "alive",
    # the ring cap self-silences in the dropped alarm_audio.c play_task, but
    # the UI-tick auto-dismiss in the shared actions.c keeps it alive
    "UI_ALARM_RING_MAX_MIN": "alive",
    "UI_CLOCK_ROLL_SIGN": "alive", "UI_ORI_BASE_V10": "alive",
    "UI_IMU_ENABLE": "alive", "TEST_ROLL_TILT_SEC": "alive",
    # the knob reads the fade flag too: knob_moon_render.c gates its own
    # fade-to-black on it (shared-home parity)
    "UI_SCREEN_SWITCH_FADE": "alive",
    "UI_DEMO_FULL_TOUR": "dead", "UI_IMU_ROLL_OFFSET_DEG": "dead",
    "MOON_ALBEDO_W": "configure", "BOARD_S3": "configure",
    "ESP_HOSTED_HOST_USB_CDC_ACM_MANIFEST": "registry",
    "ESP_HOSTED_HOST_USB_CHERRYUSB_MANIFEST": "registry",
    "ESP_HOSTED_HOST_USB_RNDIS_MANIFEST": "registry",
    "BSP_I2C_FAST_MODE": "dead",
    # the CLOCK menu's own secrets: the time-format pair is read by NO file
    # repo-wide (the 12/24 h choice is runtime), and the idle-check period
    # is read only by the clock main — silent on the knob
    "CLOCK_TIME_FORMAT_24H": "dead", "CLOCK_TIME_FORMAT_12H": "dead",
    "CLOCK_IDLE_CHECK_INTERVAL_MS": "clock-only",
}
for n in parsed["UI"]:
    if n not in EXPECTED_CAT:
        EXPECTED_CAT[n] = "clock-only"
for label in ("CLOCK", "board shim", "battery", "power", "knob time", "ui shim", "knob moon"):
    for n in parsed[label]:
        EXPECTED_CAT.setdefault(n, "alive")
EXPECTED_CAT["BOARD_P4"] = "alive"
EXPECTED_CAT["BSP_ERROR_CHECK"] = "s3-only"  # rest of BSP handled below
# the knob's choice BOARD must carry all three members, but its two foreign
# legs are read by no knob file (BOARD_P4's readers are clock-side files the
# knob never compiles) — unselected legs, not broken options
EXP_OVERRIDE = {("board shim", "BOARD_S3"): "dead",
                ("board shim", "BOARD_P4"): "dead"}

counts: dict[str, dict[str, int]] = {}
for label in list(SOURCES) + list(KNOB_SOURCES) + [BSP_SOURCE]:
    c = {"alive": 0, "clock-only": 0, "dead": 0, "configure": 0,
         "registry": 0, "s3-only": 0}
    for n in parsed[label]:
        cat = category(n, label)
        exp = EXP_OVERRIDE.get((label, n),
                               EXPECTED_CAT.get(n, "s3-only" if label == BSP_SOURCE else "alive"))
        if cat != exp:
            raise SystemExit(f"{n}: computed category {cat!r} != expected "
                             f"{exp!r} — a reader appeared or vanished; "
                             f"re-read the machine before drawing it")
        c[cat] += 1
    counts[label] = c

# ------------------------------------------------------------------ live sdkconfigs
cfg_blobs = {}
for f in ("sdkconfig", "sdkconfig.p4", os.path.join("knob", "sdkconfig")):
    if not os.path.exists(os.path.join(ROOT, f)):
        raise SystemExit(f"{f}: missing — build the targets first; the "
                         f"menu's offer is checked against a real configure")
    cfg_blobs[f] = _read(f)


def dep_on(src: str, name: str) -> str | None:
    """The optional `depends on PARENT` of a config block (single level)."""
    txt = _read(src)
    i = txt.find(f"config {name}\n")
    j = txt.find("\n    config ", i + 1)
    block = txt[i:j if j > 0 else len(txt)]
    m = re.search(r"depends on (\w+)", block)
    return m.group(1) if m else None


def cfg_false(b: str, name: str) -> bool:
    # a symbol of ANOTHER project never appears in this project's configure
    # at all — absence is off, exactly as an explicit "is not set"
    if f"CONFIG_{name}" not in b:
        return True
    return (f"# CONFIG_{name} is not set" in b
            or re.search(rf"^CONFIG_{name}=n$", b, re.M) is not None)


for label in list(SOURCES) + list(KNOB_SOURCES) + [BSP_SOURCE]:
    if label == BSP_SOURCE:
        continue  # the BSP menu only exists on S3 — checked there below
    src = SOURCES.get(label) or KNOB_SOURCES[label]
    for n in parsed[label]:
        if any(f"CONFIG_{n}" in b for b in cfg_blobs.values()):
            continue
        # absent from every configure: only acceptable as a hidden leg of a
        # depends-on gate that is switched off everywhere (UI_DEMO_FULL_TOUR
        # behind UI_DEMO_MODE) — the parent must be provably off
        p = dep_on(src, n)
        if p and any(f"CONFIG_{p}" in b for b in cfg_blobs.values()) and \
           all(cfg_false(b, p) for b in cfg_blobs.values()):
            continue
        raise SystemExit(f"CONFIG_{n}: in no live sdkconfig (neither "
                         f"sdkconfig, sdkconfig.p4, nor knob/sdkconfig)"
                         f"{' and its gate ' + p + ' is not off everywhere' if p else ''}"
                         f" — the menu claims an entry no configure ever made")
for n in parsed[BSP_SOURCE]:
    if f"CONFIG_{n}" not in cfg_blobs["sdkconfig"]:
        raise SystemExit(f"CONFIG_{n}: absent from the S3 sdkconfig — the "
                         f"vendored bar counts a menuconfig entry that is not there")

# ------------------------------------------------------------- the runtime layer
store = _read("components/clock/settings_store.c")
parse_keys = set(re.findall(r'parse_kv(?:_str)?\(line,\s*"([a-z_]+)"', store))
save_keys = set(re.findall(r'"([a-z_]+) = %', store))
KEYS = ["idle_timeout_sec", "deep_sleep_countdown_sec", "boot_anim_enable",
        "dusk_anim_enable", "timezone", "alarm_sound_id",
        "display_rotation_deg"]
# the parser additionally accepts the RETIRED numeric timezone_idx (the
# pre-2026-11-17 key, migrated once at load and never re-saved)
if parse_keys != set(KEYS) | {"timezone_idx"} or save_keys != set(KEYS):
    raise SystemExit(f"settings.txt keys drifted: parse={sorted(parse_keys)} "
                     f"save={sorted(save_keys)} — the right panel draws "
                     f"exactly these seven")
SEEDS = {
    "idle_timeout_sec": ("#ifndef CONFIG_CLOCK_IDLE_TIMEOUT_SEC", "kconfig"),
    "deep_sleep_countdown_sec": ("#ifndef CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC", "kconfig"),
    "boot_anim_enable": ("#ifndef CONFIG_UI_MOON_ANIM_ENABLE", "ghost"),
    "dusk_anim_enable": ("#ifndef CONFIG_UI_MOON_DUSK_ENABLE", "ghost"),
    "timezone": ('static char s_timezone[40] = CONFIG_CLOCK_DEFAULT_TIMEZONE', "kconfig"),
    "alarm_sound_id": ("static int s_alarm_sound_id = SETTINGS_ALARM_SOUND_DEFAULT", "const"),
    "display_rotation_deg": ("static int s_display_rotation_deg = SETTINGS_DISPLAY_ROT_DEFAULT",
                             "const"),
}
for key, (tok, kind) in SEEDS.items():
    if tok not in store:
        raise SystemExit(f"settings_store.c lost the seed '{tok}' for "
                         f"{key} — the seed column states it verbatim")
if "LEGACY no-ops" not in store or "must not break parsing" not in store:
    raise SystemExit("the LEGACY ghost comments vanished from "
                     "settings_store.c — the ghost rows quote them")
if not re.search(r"#define SETTINGS_TIMEOUT_MIN_SEC 60", store) or \
   not re.search(r"#define SETTINGS_TIMEOUT_MAX_SEC 1800", store):
    raise SystemExit("the two timeouts' 60..1800 s clamp changed — the range "
                     "column states it")
if not re.search(r"#define SETTINGS_ALARM_SOUND_MAX 4", store):
    raise SystemExit("SETTINGS_ALARM_SOUND_MAX is no longer 4")
if not re.search(r'"display_rotation_deg", &v\) && v >= 0 && v <= 359', store):
    raise SystemExit("the rotation key's 0..359 parse clamp changed — the "
                     "range column states it")
aa_h = _read("components/ui/alarm_audio.h")
if not re.search(r"#define ALARM_SOUND_COUNT 4", aa_h):
    raise SystemExit("alarm_audio.h's ALARM_SOUND_COUNT is no longer 4 — the "
                     "store's own copy of the range must equal it (the "
                     "range-twin comment)")

screens_txt = _read("components/ui/screens.c")
WRITERS = {
    "idle_timeout_sec": "settings_set_idle_timeout_sec(sec)",
    "deep_sleep_countdown_sec": "settings_set_deep_sleep_countdown_sec(sec)",
    "timezone": "settings_set_timezone(z->name)",
    "alarm_sound_id": "settings_set_alarm_sound_id((int)sel)",
    "display_rotation_deg": "settings_set_display_rotation_deg(s_display_deg)",
}
for key, tok in WRITERS.items():
    if tok not in screens_txt:
        raise SystemExit(f"screens.c lost the writer '{tok}' for {key} — the "
                         f"matrix's screen column states it")
# "no screen writes them" is the honest claim: the ONLY writers repo-wide
# are the TEST settings demo in main.cpp, which stamps them onto the card to
# prove persistence. Any screen-side writer kills the ghost row.
if "TEST settings demo" not in _read("main/main.cpp"):
    raise SystemExit("main.cpp lost the TEST settings demo — the ghost "
                     "row's sole writer is gone")
for ghost in ("settings_set_boot_anim_enable", "settings_set_dusk_anim_enable"):
    callers = {rel for rel, txt in C_SRC
               if re.search(rf"\b{ghost}\(", txt)
               and "settings_store" not in rel}
    if callers - {"main/main.cpp"}:
        raise SystemExit(f"{ghost} suddenly has callers {sorted(callers)} — "
                         f"the ghost row's 'no screen writes it' is dead")
for ghost in ("settings_boot_anim_enable", "settings_dusk_anim_enable"):
    rs = {rel for rel, txt in C_SRC
          if re.search(rf"\b{ghost}\(\)", txt) and "settings_store" not in rel}
    if rs:
        raise SystemExit(f"{ghost}() is suddenly read by {sorted(rs)} — the "
                         f"ghost row's 'nothing reads them' is dead")
for live in ("settings_idle_timeout_sec", "settings_deep_sleep_countdown_sec",
             "settings_timezone", "settings_alarm_sound_id",
             "settings_display_rotation_deg"):
    rs = {rel for rel, txt in C_SRC
          if re.search(rf"\b{live}\(\)", txt) and "settings_store" not in rel}
    if not rs:
        raise SystemExit(f"{live}() is read nowhere — the live dot is a lie")

# ------------------------------------------------------------------- assembly
plt.rcParams["font.family"] = "DejaVu Sans"
fig, (ax_l, ax_m, ax_r) = plt.subplots(1, 3, figsize=(13.4, 5.4))

# ------------------------------------------------- left: what each menu defines
rows = [("S3 AMOLED\n(466, default target)", BLUE,
         [("UI", "UI"), ("CLOCK", "CLOCK"), ("MOON", "MOON"),
          ("BOARD", "BOARD"), ("BSP (vendored)", BSP_SOURCE)]),
        ("P4 4C\n(720, sibling build)", VERM,
         [("UI", "UI"), ("CLOCK", "CLOCK"), ("MOON", "MOON"), ("BOARD", "BOARD")]),
        ("knob\n(sibling PROJECT)", VIOLET,
         [("UI", "UI"), ("CLOCK", "CLOCK"), ("MOON", "MOON"),
          ("board shim", "board shim"), ("battery", "battery"),
          ("power", "power"), ("knob time", "knob time"),
          ("ui shim", "ui shim"), ("knob moon", "knob moon")])]
for y, (label, idc, segs) in enumerate(rows):
    left = 0
    for name, key in segs:
        n = len(parsed[key])
        hatched = key == BSP_SOURCE
        ax_l.barh(y, n, left=left, height=0.55, color=MENU_COL[name],
                  edgecolor="white", linewidth=0.7, zorder=3,
                  hatch="///" if hatched else None)
        if n >= 3:
            ax_l.text(left + n / 2, y, str(n), ha="center", va="center",
                      fontsize=7, color="white", zorder=4, fontweight="bold")
        left += n
    ax_l.text(left + 0.5, y, f"{left} knobs", fontsize=8, color=idc,
              va="center", fontweight="bold")
ax_l.set_yticks(range(3))
ax_l.set_yticklabels([r[0] for r in rows], fontsize=7.6)
for lab, (_, idc, _) in zip(ax_l.get_yticklabels(), rows):
    lab.set_color(idc)
ax_l.invert_yaxis()
ax_l.set_xlim(0, 95)
ax_l.set_xlabel("menuconfig entries DEFINED (project menus solid; the "
                "vendored BSP hatched — upstream's file, counted not curated)",
                fontsize=6.8)
shared_n = len(parsed["UI"]) + len(parsed["CLOCK"]) + len(parsed["MOON"])
proj_n = shared_n + len(parsed["BOARD"])
knob_native_n = sum(len(parsed[k]) for k in KNOB_SOURCES)
ax_l.set_title("left: how many choices each menu hands over\n"
               f"(the shared {shared_n} sit under every bar)",
               loc="left", fontsize=8.6)
ax_l.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_l.set_axisbelow(True)
for s in ("top", "right"):
    ax_l.spines[s].set_visible(False)
ax_l.set_ylim(3.05, -0.6)
notes = [f"+{len(parsed[BSP_SOURCE])} vendored BSP options ride ONLY here;\nthe P4 excludes the BSP by name",
         f"same {proj_n} project entries as the S3;\nno vendored menu at all",
         f"{shared_n} shared + {knob_native_n} knob-native (six shims) —\nthe SMALLEST screen carries the BIGGEST menu"]
for y, (_, _, _) in enumerate(rows):
    ax_l.text(0.5, y + 0.32, notes[y], fontsize=5.8, color=MUTED, va="top")

# -------------------------------------- middle: who reads it where
MID_ROWS = ["UI", "CLOCK", "MOON", "BOARD", BSP_SOURCE] + list(KNOB_SOURCES)
CATS = [("alive", BLUE, "read by code every build compiles"),
        ("clock-only", AMBER, "read ONLY by the four dropped TUs + clock main"),
        ("configure", STEEL, "read ONLY by a CMakeLists guard"),
        ("registry", MUTED, "read by NO C at all — the registry's if-clauses"),
        ("s3-only", GREY, "vendored — read only by the in-tree BSP (S3)"),
        ("dead", "#d3d7de", "read by nobody anywhere (dead letters)")]
ysm = list(range(len(MID_ROWS)))
for y, label in zip(ysm, MID_ROWS):
    left = 0
    for cat, col, _ in CATS:
        n = counts[label][cat]
        if n:
            hatch = "///" if cat == "registry" else (".." if cat == "dead" else None)
            ax_m.barh(y, n, left=left, height=0.55, color=col,
                      edgecolor="white", linewidth=0.7, zorder=3, hatch=hatch)
            if n >= 2:
                ax_m.text(left + n / 2, y, str(n), ha="center", va="center",
                          fontsize=6.6, color="white" if cat != "dead" else INK,
                          zorder=4, fontweight="bold")
            left += n
ax_m.set_yticks(ysm)
ax_m.set_yticklabels(MID_ROWS, fontsize=7)
ax_m.invert_yaxis()
ax_m.set_xlim(0, 46)
ax_m.set_ylim(len(MID_ROWS) - 0.3, -0.55)
ax_m.set_xlabel("entries by who reads them", fontsize=6.8)
ax_m.set_title(f"middle: the silent-knob ledger — the census's silence,\n"
               f"counted for the menu (amber: {counts['UI']['clock-only']} of "
               f"the {len(parsed['UI'])} UI entries are knob-inert)",
               loc="left", fontsize=8.6)
ax_m.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_m.set_axisbelow(True)
for s in ("top", "right"):
    ax_m.spines[s].set_visible(False)

# ------------------------------------------------ right: the runtime layer
def line_of(tok, what):
    """1-based line of a writer token in screens.c — the matrix quotes the
    live position, never a remembered one."""
    if tok not in screens_txt:
        raise SystemExit(f"screens.c lost the writer '{tok}' ({what})")
    return screens_txt.count("\n", 0, screens_txt.index(tok)) + 1


KEY_ROWS = [
    ("idle_timeout_sec", "settings light-sleep", "kconfig", True, True,
     f"screens.c:{line_of(WRITERS['idle_timeout_sec'], 'light-sleep timeout')} "
     "· clamp 60..1800 s"),
    ("deep_sleep_countdown_sec", "settings deep-sleep", "kconfig", True, True,
     f"screens.c:{line_of(WRITERS['deep_sleep_countdown_sec'], 'deep-sleep timeout')} "
     "· clamp 60..1800 s"),
    ("timezone", "settings-tz", "kconfig", True, True,
     f"screens.c:{line_of(WRITERS['timezone'], 'timezone commit')} "
     "· IANA name (clock_tz table)"),
    ("alarm_sound_id", "settings-alarm-sound", "const", True, True,
     f"screens.c:{line_of(WRITERS['alarm_sound_id'], 'alarm-sound commit')} "
     "· < ALARM_SOUND_COUNT 4"),
    ("display_rotation_deg", "settings-display", "const", True, True,
     f"screens.c:{line_of(WRITERS['display_rotation_deg'], 'display-trim commit')} "
     "· clamp 0..359°"),
    ("boot_anim_enable", None, "ghost", False, False,
     "SAVED for stale cards · the TEST settings\ndemo is its only writer · no reader"),
    ("dusk_anim_enable", None, "ghost", False, False,
     "SAVED for stale cards · the TEST settings\ndemo is its only writer · no reader"),
]
if {k for k, *_ in KEY_ROWS} != set(KEYS):
    raise SystemExit("the drawn matrix and the store's key set drifted apart — "
                     "the right panel draws exactly the parsed keys")
COLS = ["writer", "seed", "range", "reader"]
seed_sym = {"kconfig": BLUE, "const": INDIGO, "ghost": HAIR}
for r, (key, screen, seed, rng, read, note) in enumerate(KEY_ROWS):
    ghost = screen is None
    for c, on in enumerate((bool(screen), True, rng, read)):
        col = (BLUE if c == 0 else seed_sym[seed] if c == 1 else TEAL if c == 2
               else BLUE) if on else "none"
        ax_r.scatter([c], [r], s=190 if on else 120, marker="o",
                     color=col if on else "none",
                     edgecolor=col if on else HAIR,
                     linewidth=1.4 if on else 1.0, zorder=3)
    ax_r.text(4.15, r, note, fontsize=5.9,
              color=MUTED if ghost else INK, va="center",
              style="italic" if ghost else "normal")
ax_r.set_xticks(range(4))
ax_r.set_xticklabels(COLS, fontsize=6.4)
ax_r.set_yticks(range(len(KEY_ROWS)))
ax_r.set_yticklabels([k for k, _, _, _, _, _ in KEY_ROWS], fontsize=6.6)
for lab in ax_r.get_yticklabels():
    if "anim" in lab.get_text():
        lab.set_style("italic")
        lab.set_color(MUTED)
ax_r.invert_yaxis()
ax_r.set_xlim(-0.5, 8.6)
ax_r.set_ylim(len(KEY_ROWS) - 0.3, -0.6)
ax_r.set_title(f"right: the user's whole layer — {len(KEY_ROWS)} keys on\n"
               "settings.txt; the two ghosts stay for the card",
               loc="left", fontsize=8.6)
ax_r.set_xlabel("seed: blue = #ifndef Kconfig default · indigo = in-code "
                "constant · grey = retired macro",
                fontsize=6.0)
ax_r.grid(color=HAIR, linewidth=0.5, zorder=0)
ax_r.set_axisbelow(True)
for s in ("top", "right", "left", "bottom"):
    ax_r.spines[s].set_visible(False)

fig.suptitle("The configuration ladder: how many choices each layer hands over — and how many read no C file at all",
             fontsize=9.5, color=INK, y=0.985)
hs = []
for cat, col, lab in CATS:
    hs.append(Patch(facecolor=col, label=lab,
                    hatch="///" if cat == "registry" else (".." if cat == "dead" else None),
                    edgecolor=col))
fig.legend(handles=hs, fontsize=5.8, loc="lower center", frameon=False,
           ncol=3, bbox_to_anchor=(0.5, 0.002))
fig.tight_layout(rect=(0, 0.115, 1, 0.955))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=150)
print(f"wrote {OUT}")
print("menu totals:", {k: len(v) for k, v in parsed.items()})
print("categories:", {k: {c: counts[k][c] for c in counts[k] if counts[k][c]}
                      for k in counts})
