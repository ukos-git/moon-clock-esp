#!/usr/bin/env python3
"""Generate the write-ladder comparison figure for
docs/verification/performance.rst — the axis none of the other ladders
prices: the storage ladder counts bytes that *lie* in the flash, the
hour-cache footprint counts *capacity*, the bus ladder counts bytes the
display moves — this counts how OFTEN the media are WRITTEN, and how
big one write is. Every number is parsed from the live sources or
arithmetic on parsed numbers; nothing is measured on a card or an NVS
partition (the figure bounds, never touches).

Parsed at figure time (a drifted source kills the run):

  components/ui/moon_tfcache.c
    #define TF_MAX_HOURS 709  + its "ceil(29.53 d / 1 h) + slack" comment
    SYNODIC_S ((int64_t)(29.53 * 86400))      — the eviction window
    s_enc_out_cap = S*S + S*S/2 + 1024        — the encoder out buffer:
                                               the bound on one stored file
    META_KEYS 6 + the exact six-key list; the meta_write fprintf format
                                               (7 lines: six keys + the
                                               non-authoritative new_moon)
    eviction gate "s_max_hour - s_last_evict_hour > 24" + its "one scan
                                               per day of new hours" comment
    the new-moon floor guard comment "the meta rewrite only ever fires on
                                               the new-moon crossing"
    meta_write call sites == 2 (fresh-folder init + floor crossing)
    store() body: fopen "wb" / fwrite / fclose / unlink-on-failure
  components/ui/Kconfig
    UI_MOON_HOURLY_ENABLE default y; UI_MOON_HOURLY_TFCACHE default y +
    its depends line; UI_MOON_TFCACHE default y; JPEG_Q default 90 + range
  components/ui/moon_ui.c
    MOON_PX pair (P4 650 / S3 400); exactly ONE tfcache_store call site
    (the hourly write-through) under the TFCACHE && HOURLY_TFCACHE guard;
    four tfcache_evict call sites; two set_new_moon_floor call sites
  components/ui/CMakeLists.txt
    moon_tfcache.c is a UI_BOARD_SRCS member and the KNOB branch empties
    the list — the knob's TF leg is NOT COMPILED, not merely unwritten
  components/clock/clock_sntp.c
    #if !CONFIG_CLOCK_USE_PCF85063A wrapping the NVS epoch family;
    CLOCK_NVS_SAVE_INTERVAL_SEC 900 + its "one commit per quarter hour"
    comment; the nvs_open -> nvs_set_i64 -> nvs_commit -> nvs_close order;
    the forced save inside sntp_sync_time_cb under the same guard
  components/clock/clock_interrupt.c
    the minute-tick call sits INSIDE the RTC-less #if block (before the
    #else) — on the S3 the calling code itself is not compiled;
    repo-wide call sites of maybe_save == exactly 1
  sdkconfig state (per board, three independent layers):
    S3  build/config/sdkconfig.h        #define CONFIG_CLOCK_USE_PCF85063A 1
    P4  sdkconfig.defaults.esp32p4      CONFIG_CLOCK_USE_PCF85063A=n
    knob knob/sdkconfig (fallback: ../build/knob/config/sdkconfig.h
        absence)  "# CONFIG_CLOCK_USE_PCF85063A is not set"
  components/clock/settings_store.c
    SETTINGS_PATH; the 11 fprintf lines asserted verbatim (2 comment
    + 9 keys — the two counters + their power-off kill switches + the
    five remaining) -> byte size computed from the asserted literals
  settings_store_save call sites == exactly 6 total: screens.c's five
    writers (the shared sleep-roller commit + the shared sleep kill-switch
    toggle handler — one handler each serves BOTH sleep screens — / tz /
    alarm-sound / display-trim) + main.cpp's TEST settings demo (line
    must say so)
  components/wifi/wifi.c
    WIFI_CREDENTIALS_FILE; the exact fprintf template; call sites == 1
    (the provisioning wheel commit)
  components/clock/clock_alarm_scheduler.c
    the runtime read fopen("r") exists; a writer fopen("w") exists, but
    clock_alarm_scheduler_write has ZERO call sites repo-wide — alarm.txt
    is written by the spiffs image at flash time, never at runtime
  partitions CSVs: storage rows (S3/P4 64 K, knob 512 K) and the knob's
    64 K coredump row; S3/P4 carry NO coredump row
  knob/main/main.c
    the legacy-snapshot promotion (one forced clock_nvs_save_epoch)

Left panel: scheduled flash writes PER DAY per board — stacked by leg
(the hourly store 24/d, eviction unlinks ~24/d at window steady state,
meta 1/synodic, NVS 86400/900 = 96/d); settings/wifi/coredump are
human-paced and contribute nothing to the day bar. Right panel: BYTES
per single write (log) — the blast radius of one event.

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet. Hatched = a bound or a lazy/derived quantity, not a measurement.

Run:  docs/.venv/bin/python docs/tools/gen_perf_writes.py
"""
from __future__ import annotations

import os
import re
import subprocess

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TFCACHE = os.path.join(REPO, "components", "ui", "moon_tfcache.c")
MOON_UI = os.path.join(REPO, "components", "ui", "moon_ui.c")
KCONFIG = os.path.join(REPO, "components", "ui", "Kconfig")
CMAKE = os.path.join(REPO, "components", "ui", "CMakeLists.txt")
SNTP = os.path.join(REPO, "components", "clock", "clock_sntp.c")
INTERRUPT = os.path.join(REPO, "components", "clock", "clock_interrupt.c")
SETTINGS = os.path.join(REPO, "components", "clock", "settings_store.c")
SCHED = os.path.join(REPO, "components", "clock", "clock_alarm_scheduler.c")
SCHED_H = os.path.join(REPO, "components", "clock", "include",
                       "clock_alarm_scheduler.h")
WIFI = os.path.join(REPO, "components", "wifi", "wifi.c")
SCREENS = os.path.join(REPO, "components", "ui", "screens.c")
MAIN = os.path.join(REPO, "main", "main.cpp")
KNOB_MAIN = os.path.join(REPO, "knob", "main", "main.c")
PART_S3 = os.path.join(REPO, "partitions.csv")
PART_P4 = os.path.join(REPO, "partitions_p4.csv")
PART_KNOB = os.path.join(REPO, "knob", "partitions.csv")
S3_HDR = os.path.join(REPO, "build", "config", "sdkconfig.h")
P4_DEF = os.path.join(REPO, "sdkconfig.defaults.esp32p4")
KNOB_CFG = os.path.join(REPO, "knob", "sdkconfig")
KNOB_HDR = os.path.join(REPO, "..", "build", "knob", "config", "sdkconfig.h")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_writes.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # S3 identity
VERM = "#c2410c"    # P4 identity
VIOLET = "#7c3aed"  # knob identity
AMBER = "#b07d10"   # spiffs text files
STEEL = "#4b6584"   # NVS


def read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def must(text: str, pattern: str, what: str, src: str, flags: int = 0):
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"{os.path.basename(src)}: {what} not found — "
                         "the figure refuses to hard-code it")
    return m


def count(text: str, pattern: str, expected: int, what: str, src: str):
    ms = re.findall(pattern, text)
    if len(ms) != expected:
        raise SystemExit(f"{os.path.basename(src)}: expected {expected} of "
                         f"{what}, found {len(ms)} — {pattern!r}")
    return ms


def main() -> None:
    tf = read(TFCACHE)
    ui = read(MOON_UI)
    kc = read(KCONFIG)
    cm = read(CMAKE)
    sntp = read(SNTP)
    irq = read(INTERRUPT)
    st = read(SETTINGS)
    sch = read(SCHED)
    wf = read(WIFI)
    scr = read(SCREENS)
    mn = read(MAIN)
    kmn = read(KNOB_MAIN)

    # ---- the TF hour cache ------------------------------------------------
    must(tf, r'#define TF_MAX_HOURS (\d+)\s+/\* ceil\((\d+\.\d+) d / 1 h\) \+ slack \*/',
         "TF_MAX_HOURS + slack comment", TFCACHE)
    cap = int(must(tf, r"#define TF_MAX_HOURS (\d+)", "TF_MAX_HOURS",
                   TFCACHE).group(1))
    syn_d = float(must(tf, r"#define TF_MAX_HOURS \d+\s+/\* ceil\((\d+\.\d+) d / 1 h\) \+ slack \*/",
                       "TF_MAX_HOURS slack days", TFCACHE).group(1))
    must(tf, r"SYNODIC_S \(\(int64_t\)\((\d+\.\d+) \* 86400\)\)",
         "SYNODIC_S", TFCACHE)
    oc = must(tf, r"s_enc_out_cap = \(size_t\)S \* S \+ \(size_t\)S \* S / (\d+) \+ (\d+);",
              "encoder out-capacity formula", TFCACHE)
    div, tail = int(oc.group(1)), int(oc.group(2))
    must(tf, r'#define META_KEYS 6\nstatic const char \*const META_KEY\[META_KEYS\] = \{"calcver", "target", "bake_px", "moon_albedo_w", "relief", "jpeg_q"\};',
         "META_KEYS == 6 + exact key list", TFCACHE)
    meta_fmt = must(tf, r'fprintf\(f, "calcver=%s((?:\\n\w+=\S+)+\\n)",',
                    "meta.txt fprintf 7-line format", TFCACHE).group(1)
    if meta_fmt.count("\\n") != 7 or "new_moon=%lld" not in meta_fmt:
        raise SystemExit("moon_tfcache.c: meta format must be 7 lines with the "
                         "new_moon ride-along — the figure's byte size depends on it")
    must(tf, r"if \(s_max_hour - s_last_evict_hour > 24\) /\* one scan per day of new hours \*/",
         "eviction >24 h gate + comment", TFCACHE)
    must(tf, r"the meta rewrite only ever fires on\n\s*\*\s*the new-moon crossing",
         "new-moon guard comment", TFCACHE)
    count(tf, r"meta_write\(s_dir", 2, "meta_write call sites", TFCACHE)
    count(tf, r'fopen\(path, "wb"\)', 1, "store() fopen wb", TFCACHE)
    must(tf, r"unlink\(path\); /\* never leave a truncated frame",
         "store() unlink-on-failure comment", TFCACHE)
    must(kc, r"config UI_MOON_HOURLY_ENABLE\s*\n\s*bool[^\n]*\n\s*default y",
         "UI_MOON_HOURLY_ENABLE default y", KCONFIG)
    must(kc, r"config UI_MOON_HOURLY_TFCACHE\s*\n\s*bool[^\n]*\n\s*depends on UI_MOON_HOURLY_ENABLE && UI_MOON_TFCACHE\n\s*default y",
         "UI_MOON_HOURLY_TFCACHE default y + depends", KCONFIG)
    must(kc, r"config UI_MOON_TFCACHE\b(?:[\s\S]{0,120}?)default y",
         "UI_MOON_TFCACHE default y", KCONFIG)
    must(kc, r"config UI_MOON_TFCACHE_JPEG_Q\s*\n\s*int[^\n]*\n\s*depends on UI_MOON_TFCACHE\n\s*default 90\n\s*range 1 100",
         "JPEG_Q default 90 + range", KCONFIG)
    px = must(ui, r"#if defined\(CONFIG_BOARD_P4\)\s*\n#define MOON_PX (\d+)\s*\n#else\s*\n#define MOON_PX (\d+)",
              "MOON_PX pair", MOON_UI)
    S_P4, S_S3 = int(px.group(1)), int(px.group(2))
    count(ui, r"tfcache_store\(hour, s_hourly_gray, MOON_PX\); /\* write-through \*/",
          1, "the single hourly write-through store", MOON_UI)
    count(ui, r"tfcache_evict\(", 4, "tfcache_evict call sites", MOON_UI)
    count(ui, r"tfcache_set_new_moon_floor\(", 2, "set_new_moon_floor call sites", MOON_UI)
    # the store site must live under the TFCACHE && HOURLY_TFCACHE guard
    store_line = ui.index("tfcache_store(hour, s_hourly_gray")
    guard = "#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE"
    last_guard = ui.rindex(guard, 0, store_line)
    next_end = ui.index("#endif", store_line)
    if not (last_guard < store_line < next_end):
        raise SystemExit("moon_ui.c: the hourly store escaped its Kconfig guard")
    must(cm, r'set\(UI_BOARD_SRCS "alarm_audio\.c" "demo\.c" "moon_ui\.c" "moon_tfcache\.c"\)',
         "moon_tfcache.c as a UI_BOARD_SRCS member", CMAKE)
    must(cm, r'if\(KNOB_BOARD\)\n\s*set\(UI_BOARD_SRCS ""\)',
         "KNOB branch emptying UI_BOARD_SRCS", CMAKE)

    # ---- the NVS epoch snapshot (RTC-less boards only) --------------------
    must(sntp, r'#if !CONFIG_CLOCK_USE_PCF85063A\n#include "nvs\.h"',
         "NVS block guard", SNTP)
    intv = int(must(sntp, r"#define CLOCK_NVS_SAVE_INTERVAL_SEC (\d+)",
                    "CLOCK_NVS_SAVE_INTERVAL_SEC", SNTP).group(1))
    must(sntp, r"one commit per quarter hour", "quarter-hour comment", SNTP)
    must(sntp, r"nvs_open\(CLOCK_NVS_NAMESPACE, NVS_READWRITE", "nvs_open", SNTP)
    must(sntp, r"nvs_set_i64\(h, CLOCK_NVS_KEY_EPOCH", "nvs_set_i64", SNTP)
    must(sntp, r"nvs_commit\(h\)", "nvs_commit", SNTP)
    must(sntp, r"nvs_close\(h\)", "nvs_close", SNTP)
    must(sntp, r"\(time\(NULL\) - s_last_nvs_save\) < CLOCK_NVS_SAVE_INTERVAL_SEC",
         "maybe_save interval guard", SNTP)
    must(sntp, r"// it \(force, ignoring the 15 min interval\)\.",
         "forced-save comment in the SNTP callback", SNTP)
    count(sntp, r"#if !CONFIG_CLOCK_USE_PCF85063A", 3, "NVS guard blocks", SNTP)
    call_pos = irq.index("clock_nvs_maybe_save_epoch();")
    blk_if = irq.rindex("#if !CONFIG_CLOCK_USE_PCF85063A", 0, call_pos)
    blk_else = irq.index("#else", call_pos)
    if not (blk_if < call_pos < blk_else):
        raise SystemExit("clock_interrupt.c: the NVS call escaped its RTC-less "
                         "block — the S3's zero would be wrong")
    count(irq, r"clock_nvs_maybe_save_epoch\(\);", 1, "the single maybe_save caller", INTERRUPT)
    # repo-wide: maybe_save is called from nowhere else
    for probe in (scr, mn, kmn):
        if "clock_nvs_maybe_save_epoch" in probe:
            raise SystemExit("maybe_save leaked to a second caller — recount the NVS leg")
    s3_hdr = read(S3_HDR)
    must(s3_hdr, r"#define CONFIG_CLOCK_USE_PCF85063A 1",
         "S3 built with the PCF85063A (NVS family #if'd OUT)", S3_HDR)
    must(read(P4_DEF), r"^CONFIG_CLOCK_USE_PCF85063A=n$",
         "P4 defaults the RTC off (NVS family COMPILED)", P4_DEF, re.M)
    if os.path.exists(KNOB_CFG):
        must(read(KNOB_CFG), r"^# CONFIG_CLOCK_USE_PCF85063A is not set$",
             "knob has no PCF85063A (NVS family COMPILED)", KNOB_CFG, re.M)
    else:
        kh = read(KNOB_HDR)
        if "CONFIG_CLOCK_USE_PCF85063A" in kh:
            raise SystemExit("knob defines the PCF85063A — the knob's 96/day is wrong")

    # ---- the spiffs text files ---------------------------------------------
    must(st, r'#define SETTINGS_PATH "/spiffs/settings\.txt"', "SETTINGS_PATH", SETTINGS)
    st_lines = [
        r'"# Runtime settings (owned by the settings screens; do not\n"',
        r'"# edit by hand -- a full flash resets this file).\n"',
        r'"idle_timeout_sec = %u\n"',
        r'"idle_timeout_enable = %d\n"',
        r'"deep_sleep_countdown_sec = %u\n"',
        r'"deep_sleep_countdown_enable = %d\n"',
        r'"boot_anim_enable = %d\n"',
        r'"dusk_anim_enable = %d\n"',
        r'"timezone = %s\n"',
        r'"alarm_sound_id = %d\n"',
        r'"display_rotation_deg = %d\n"',
    ]
    for line in st_lines:
        if line not in st:
            raise SystemExit(f"settings_store.c: settings template line missing — {line!r}")
    settings_tpl = [
        "# Runtime settings (owned by the settings screens; do not\n",
        "# edit by hand -- a full flash resets this file).\n",
        "idle_timeout_sec = %u\n",
        "idle_timeout_enable = %d\n",
        "deep_sleep_countdown_sec = %u\n",
        "deep_sleep_countdown_enable = %d\n",
        "boot_anim_enable = %d\n",
        "dusk_anim_enable = %d\n",
        "timezone = %s\n",
        "alarm_sound_id = %d\n",
        "display_rotation_deg = %d\n",
    ]
    settings_bytes = len("".join(settings_tpl).replace("%u", "300")
                         .replace("%d", "1").replace("%s", "Europe/Berlin"))
    count(st, r"settings_store_save\(void\)", 1, "one definition", SETTINGS)
    if scr.count("settings_store_save();") != 5:
        raise SystemExit(f"screens.c settings writers: expected 5, found "
                         f"{scr.count('settings_store_save();')}")
    if mn.count("settings_store_save();") != 1:
        raise SystemExit("main.cpp settings writer must be exactly the TEST demo")
    if "TEST settings demo" not in mn.split("settings_store_save();")[0][-400:]:
        raise SystemExit("main.cpp: the settings writer must be the TEST demo")
    must(wf, r'#define WIFI_CREDENTIALS_FILE "/spiffs/wifi_credentials\.txt"',
         "WIFI_CREDENTIALS_FILE", WIFI)
    wf_fmt = must(wf, r'fprintf\(f, "([^"]*)", ssid, password\);',
                  "wifi credentials template", WIFI).group(1)
    if not wf_fmt.startswith("# WiFi Credentials"):
        raise SystemExit("wifi.c: credentials template lost its comment header")
    if wf_fmt.count("\\n") != 5 or wf_fmt.count("%s") != 2:
        raise SystemExit("wifi.c: template must be 3 comment + 2 value lines")
    wifi_base = len(wf_fmt.replace("\\n", "\n").replace("%s", ""))
    wifi_bytes = wifi_base + 9 + 1 + 11 + 1  # sample SSID + newline + pw + newline
    if scr.count("wifi_credentials_write(") != 1:
        raise SystemExit("screens.c must be the ONLY wifi credentials caller")
    must(sch, r'fopen\(ALARM_FILE_PATH, "r"\)', "alarm.txt runtime read", SCHED)
    must(sch, r'fopen\(ALARM_FILE_PATH, "w"\)', "alarm.txt writer", SCHED)
    # the writer must have ZERO call sites anywhere (its own definition and
    # the header declaration are the only mentions allowed)
    hits = subprocess.run(
        ["grep", "-rn", "clock_alarm_scheduler_write",
         os.path.join(REPO, "components"), os.path.join(REPO, "main"),
         os.path.join(REPO, "knob"), "--include=*.c", "--include=*.cpp"],
        capture_output=True, text=True).stdout
    extra = [h for h in hits.splitlines()
             if "clock_alarm_scheduler.c:" not in h]
    if extra:
        raise SystemExit("alarm.txt writer gained callers:\n" + "\n".join(extra))
    must(read(SCHED_H), r"esp_err_t clock_alarm_scheduler_write\(const char \*content\);",
         "writer declaration", SCHED_H)
    must(kmn, r"\(void\)clock_nvs_save_epoch\(\); /\* promote into the clock namespace \*/",
         "knob legacy promotion", KNOB_MAIN)

    # ---- partitions --------------------------------------------------------
    p_s3, p_p4, p_kn = read(PART_S3), read(PART_P4), read(PART_KNOB)

    def parse_size(tok):
        tok = tok.strip()
        if tok.lower().startswith("0x"):
            return int(tok, 16)
        m = re.fullmatch(r"(\d+)([KMG])?", tok)
        if not m:
            raise SystemExit(f"partition size token unparsable: {tok!r}")
        mult = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3}[m.group(2) or ""]
        return int(m.group(1)) * mult

    def row_size(csv, name, src):
        m = must(csv, rf"^{name},\s*data,\s*spiffs,\s*(?:[^,]*),\s*([^,]+),",
                 f"{name} row", src, re.M)
        return parse_size(m.group(1))

    st_s3, st_p4, st_kn = row_size(p_s3, "storage", PART_S3), \
        row_size(p_p4, "storage", PART_P4), row_size(p_kn, "storage", PART_KNOB)
    if (st_s3, st_p4, st_kn) != (0x10000, 0x10000, 0x80000):
        raise SystemExit(f"storage rows drifted: {st_s3}/{st_p4}/{st_kn}")
    cd = must(p_kn, r"^coredump,\s*data,\s*coredump,\s*(?:[^,]*),\s*([^,]+),",
              "knob coredump row", PART_KNOB, re.M)
    cd_bytes = parse_size(cd.group(1))
    if cd_bytes != 0x10000:
        raise SystemExit("knob coredump row is not 64 K anymore")
    if "coredump" in p_s3 or "coredump" in p_p4:
        raise SystemExit("S3/P4 grew a coredump row — the ladder's zero leg is wrong")

    # ---- arithmetic on the parsed numbers -----------------------------------
    puts_day = 24                      # one store per new hour of epoch
    evict_day = 24                     # floor advances one hour per hour
    meta_day = 1.0 / syn_d             # the new-moon crossing rewrite
    nvs_day = 86400 // intv            # 96 at 900 s
    cap_s3 = S_S3 * S_S3 + S_S3 * S_S3 // div + tail
    cap_p4 = S_P4 * S_P4 + S_P4 * S_P4 // div + tail

    def meta_bytes(target, S, albedo):
        s = meta_fmt.replace("\\n", "\n")
        s = s.replace("%s", "unknown", 1).replace("%s", target, 1)
        for spec in (str(S), str(albedo), "1", "90"):
            s = s.replace("%d", spec, 1)
        return len(s.replace("%lld", "490000", 1))

    m_s3 = meta_bytes("esp32s3", S_S3, 864)
    m_p4 = meta_bytes("esp32p4", S_P4, 2048)

    plt.rcParams.update({
        "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
        "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
        "axes.titlecolor": INK, "figure.facecolor": "white",
    })
    fig, axes = plt.subplots(1, 2, figsize=(11.8, 4.3),
                             gridspec_kw={"width_ratios": [1.04, 1.0]})

    # ---- (a) scheduled writes per day, stacked by leg ----------------------
    ax = axes[0]
    legs = [  # (label, value-key, color, hatch)
        ("TF store (hourly write-through)", "put", BLUE, None),
        ("TF eviction unlink (window steady state)", "evict", "#88a7d6", "//"),
        ("TF meta.txt rewrite", "meta", AMBER, None),
        ("NVS epoch commit (15-min interval)", "nvs", STEEL, None),
    ]
    boards = [
        ("S3 — 466 px", dict(put=puts_day, evict=evict_day, meta=meta_day,
                             nvs=0.0), BLUE),
        ("P4 — 720 px", dict(put=puts_day, evict=evict_day, meta=meta_day,
                             nvs=float(nvs_day)), VERM),
        ("knob — 360 px", dict(put=0.0, evict=0.0, meta=0.0,
                               nvs=float(nvs_day)), VIOLET),
    ]
    ys = range(len(boards))
    totals = []
    for y, (name, vals, edge) in enumerate(boards):
        left = 0.0
        for _, key, color, hatch in legs:
            v = vals[key]
            if v <= 0:
                continue
            ax.barh(y, v, left=left, color=color, height=0.56,
                    edgecolor=edge, hatch=hatch,
                    linewidth=0.7 if hatch else 1.2)
            left += v
        totals.append(left)
        ax.text(left + 2.0, y, f"{left:.0f}/day", va="center", fontsize=7.5,
                color=INK, fontweight="bold")
    ax.text(totals[0] + 14.0, 0 + 0.30, "NVS 0 — the RTC #if'd the epoch family out",
            ha="left", va="center", fontsize=6.2, color=STEEL, style="italic")
    ax.text(2.0, 1 + 0.55,
            "knob: TF legs absent — moon_tfcache.c is not compiled for KNOB_BOARD",
            ha="left", va="center", fontsize=6.2, color=MUTED, style="italic")
    ax.set_yticks(list(ys), [b[0] for b in boards], fontsize=7.5)
    for tick, (_, _, edge) in zip(ax.get_yticklabels(), boards):
        tick.set_color(edge)
    ax.invert_yaxis()
    ax.set_xlim(0, max(totals) * 1.55)
    ax.set_ylim(len(boards) - 0.5 + 0.45, -0.5)  # headroom for the zero notes
    ax.set_xlabel("scheduled flash writes PER DAY (linear; human-paced writers add 0)")
    ax.set_title("what a day of desk-clock life writes — the hourly producer\n"
                 "(Kconfig all default y) 24 stores + ~24 evictions + meta at the\n"
                 "new-moon crossing; the RTC-less boards add the 15-min epoch commit",
                 fontsize=8)
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)
    ax.legend([plt.Rectangle((0, 0), 1, 1, facecolor=c, hatch=h,
                             edgecolor=c, linewidth=0.8)
               for _, _, c, h in legs],
              [l for l, _, _, _ in legs], fontsize=6.2, loc="lower right",
              framealpha=0.95)
    ax.text(0.02, 0.02,
            "settings.txt / wifi_credentials.txt / coredump are EVENT\n"
            "writes: 4 screen writers + 1 TEST demo / 1 provisioning\n"
            "commit / 0 bytes until a crash — never the day bar.",
            transform=ax.transAxes, ha="left", va="bottom", fontsize=6.0,
            color=MUTED)

    # ---- (b) bytes per single write (log) -----------------------------------
    ax = axes[1]
    rows = [
        (f"TF hourly frame — P4 cap\nS²+S²/{div}+{tail}", cap_p4, VERM, True, ""),
        (f"TF hourly frame — S3 cap\nS²+S²/{div}+{tail}", cap_s3, BLUE, True, ""),
        ("coredump partition — knob\n(only on a crash)", cd_bytes, VIOLET, False, ""),
        ("settings.txt rewrite\n(2 comments + 7 keys)", settings_bytes, AMBER, False, ""),
        (f"meta.txt rewrite (7 lines)\nS3 {m_s3} B / P4 {m_p4} B", m_p4, AMBER, False, ""),
        ("wifi_credentials.txt\n(3 comments + SSID + pw)", wifi_bytes, AMBER, False, ""),
        ("NVS epoch commit\n(nvs_set_i64 value)", 8, STEEL, False, ""),
        ("alarm.txt — writer exists,\nZERO call sites", 1, MUTED, False, ""),
    ]
    ys = range(len(rows))
    for y, (label, nbytes, color, hatch, _note) in enumerate(rows):
        ax.barh(y, max(nbytes, 1), color=color, height=0.56, alpha=0.85,
                edgecolor=color, hatch="//" if hatch else None, linewidth=0.8)
        txt = f"{nbytes:,} B" if nbytes > 1 else "0 B"
        ax.text(max(nbytes, 1) * 1.18, y, txt, va="center", fontsize=6.8, color=INK)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=6.2)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(0.7, 4.2e6)
    ax.set_xlabel("bytes per ONE write (log)")
    ax.set_title("the blast radius of one write — four media, from an\n"
                 "8-byte NVS value to a 620 KiB encoder bound; the most\n"
                 "frequent write (NVS) is by far the cheapest",
                 fontsize=8)
    ax.grid(axis="x", color=HAIR, linewidth=0.5)
    ax.set_axisbelow(True)
    ax.text(0.985, 0.02,
            "hatched = the encoder's own file bound (the storage ladder's\n"
            "twin), never a measured JPEG — the real q90 file is far smaller,\n"
            "instrumented nowhere. meta/settings/wifi bytes computed from the\n"
            "asserted fprintf templates. alarm.txt: clock_alarm_scheduler.c:504\n"
            "fopen(\"w\") has no caller — the spiffs image owns the file.",
            transform=ax.transAxes, ha="right", va="bottom", fontsize=5.9,
            color=MUTED)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  (days={syn_d} cap={cap} "
          f"nvs={nvs_day}/d caps={cap_s3}/{cap_p4} meta={m_s3}/{m_p4} "
          f"settings={settings_bytes} wifi={wifi_bytes})")


if __name__ == "__main__":
    main()
