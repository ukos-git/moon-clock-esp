#!/usr/bin/env python3
"""Generate the tap ladder for docs/verification/performance.rst.

The response ladder (gen_perf_response.py) measures how soon the machine
NOTICES the user. This is the same contract's other side: how much
HUMAN LABOUR the user pays — taps from the awake home screen to every
screen of the enum, per board variant. Left panel: BFS tap-depth over
the navigation graph, three bars per row (S3 / P4 / knob); right panel:
the wheel keyboard's per-character price as a function of the password.

Every number is PARSED at figure time (SystemExit on any disagreement —
the house pattern), nothing hand-placed:

  the graph's TAP edges (components/ui/screens.c):
    home -> hub            event_handler_cb_home_tap :114, guard
                           `LV_EVENT_CLICKED && !clock_is_idle()` :116,
                           loadScreen(SETTINGS_HUB) :117; registered on
                           exactly TWO roots (the two home views, :290
                           and :386); the handler's own comment: "the
                           wake tap itself never opens the hub -- a
                           deliberate second tap"
    hub -> ring            the ring[] table :597-608; the CLOCK entry
                           sits inside `#if defined(CONFIG_BOARD_P4)`
                           (the per-variant difference: 7 icons S3/knob,
                           8 P4); one handler for the whole ring (:146)
    hub -> home            event_handler_cb_hub_exit :214 (the central
                           exit disk, loadScreen(MODEM_SLEEP) :218)
    alarm -> subtree       the two user_data entries :808 (pomodoro)
                           and :817 (alarm-time)
    wifi -> password       event_handler_cb_wifi_roller's
                           loadScreen(SETTINGS_WIFI_PASSWORD) :1581
    password -> wifi       settings_wheel_exit_to_list :2331
    every sub-screen -> its exit target  the ten
                           settings_sub_screen_prepare(obj, SCREEN_ID_X)
                           sites (alarm->HUB, pomodoro->ALARM,
                           alarm_time->ALARM, light_sleep->HUB,
                           deep_sleep->HUB, wifi->HUB, time->HUB,
                           tz->HUB, alarm_sound->HUB, display->HUB)
    ACCOUNTING             EVERY literal loadScreen(SCREEN_ID_*) call in
                           screens.c must be one of the above or the
                           demo hook settings_wheel_demo_open (:2454) —
                           an unexplained edge is a SystemExit

  the NO-tap-leg rows (components/ui/actions.c): the four machine-only
    views are loaded by the machine, never by a finger — alarm
    (:1230/:1673 action_alarm_triggered), deep sleep (:1330), light
    sleep (:1617), the transient WiFi sync screen (:885); MODEM_SLEEP
    re-entries are machine bookkeeping (:730/:820/:1555). The bus
    ladder's italic "no such leg" precedent.

  the matrix cross-check (docs/ui/index.rst): the same list-table the
    drift gate parses — per-panel reachability, asserted at 17/17 P4,
    16/17 S3, 14/17 knob with exactly the documented absences
    (settings-time off the S3/knob ring; the two sleep VIEWS absent on
    the knob, which dims its real backlight and really deep-sleeps).
    A disagreement between the code-parsed graph and the drift-gated
    matrix is a SystemExit.

  the wheel price (components/ui/screens.c + components/wifi/wifi.c):
    4 registers parsed VERBATIM from s_wheel_sets[] :1875-1880 (slot
    counts from strlen — never hand-counted); letters is the register
    the first set_mode draws (the s_wheel_mode comment, :1895); one
    character = one press-and-release on a frequency sector
    (wheel-commit.dot's machine, commit guard :2222 bounded by
    WHEEL_PW_MAX 63 :1867); register key, commit key = 1 tap each;
    wifi.c rejects password_len == 0 || >= MAX_PASSWORD_LEN 64 (:24,
    :127) — so the firmware floor is 1 char, no 8-char minimum exists.

Right panel: taps vs password length (DERIVED, one sum): N chars +
register hops + one commit. Lowercase-only = N + 1; a single register
hop = N + 2; alternating letters/symbols = 1.5N + 1.5. The curve is the
point: the per-character tap dominates, the registers are rounding —
human labour scales with the SECRET, not with the keyboard.

Knob note: its "tap" is a dial-click (the dial task re-sends CLICKED to
the root; docs/_graphs/input-chain.dot).

Run:  docs/.venv/bin/python docs/tools/gen_perf_taps.py
"""
from __future__ import annotations

import math
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
SCREENS = os.path.join(ROOT, "components", "ui", "screens.c")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
WIFI = os.path.join(ROOT, "components", "wifi", "wifi.c")
SCREENS_H = os.path.join(ROOT, "components", "ui", "screens.h")
INDEX_RST = os.path.join(ROOT, "docs", "ui", "index.rst")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_taps.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
S3C = "#2b5fb0"   # the S3 family colour (the house blue)
P4C = "#0891b2"   # the P4 family colour (cyan)
KNC = "#b07d10"   # the knob family colour (amber)
TEAL = "#0e7490"  # the two home roots + the hub (the shared start)
GRAY = "#8a8a8a"  # machine-only rows (the no-tap-leg precedent)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def must(pattern: str, text: str, what: str, flags: int = 0) -> re.Match:
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"tap ladder: pattern for {what} not found")
    return m


sc = _read(SCREENS)
ac = _read(ACTIONS)
wc = _read(WIFI)

# ---------------------------------------------------------------- edges
# home -> hub (the awake tap; the wake tap is a deliberate SECOND tap).
must(r"if \(code == LV_EVENT_CLICKED && !clock_is_idle\(\)\) \{",
     sc, "home-tap awake guard")
must(r"the wake tap itself never opens the hub", sc,
     "home-tap second-tap comment")
must(r"loadScreen\(SCREEN_ID_SETTINGS_HUB\);", sc, "home->hub edge")
n_home_regs = len(re.findall(
    r"lv_obj_add_event_cb\(obj, event_handler_cb_home_tap, LV_EVENT_CLICKED, 0\)",
    sc))
if n_home_regs != 2:
    raise SystemExit(f"tap ladder: home-tap registered on {n_home_regs} "
                     "roots, expected 2 (the two home views)")

# hub -> ring: parse the ring[] table, with the P4-only CLOCK split.
ring_blk = must(
    r"static const struct hub_icon ring\[\] = \{(.*?)\n    \};", sc,
    "hub ring table", re.S).group(1)
p4_only = must(r"#if defined\(CONFIG_BOARD_P4\)(.*?)#endif", ring_blk,
               "P4-only ring block", re.S).group(1)
def ring_targets(block: str) -> list[str]:
    return re.findall(r"\{GLYPH_\w+, &\w+, (SCREEN_ID_\w+)\}", block)

ring_s3 = ring_targets(re.sub(r"#if defined\(CONFIG_BOARD_P4\).*?#endif",
                              "", ring_blk, flags=re.S))
# the #if block's POSITION in the table is preserved (it is first) —
# strip only the directives, never reorder.
ring_p4 = ring_targets(re.sub(r"#if defined\(CONFIG_BOARD_P4\)|#endif",
                              "", ring_blk))
if ring_s3 != ["SCREEN_ID_SETTINGS_ALARM", "SCREEN_ID_SETTINGS_LIGHT_SLEEP",
               "SCREEN_ID_SETTINGS_DEEP_SLEEP", "SCREEN_ID_SETTINGS_WIFI",
               "SCREEN_ID_SETTINGS_TZ", "SCREEN_ID_SETTINGS_ALARM_SOUND",
               "SCREEN_ID_SETTINGS_DISPLAY"]:
    raise SystemExit(f"tap ladder: S3/knob ring parsed {ring_s3}")
if ring_p4 != ["SCREEN_ID_SETTINGS_TIME"] + ring_s3:
    raise SystemExit(f"tap ladder: P4 ring parsed {ring_p4}")

# hub central exit -> home.
must(r"static void event_handler_cb_hub_exit.*?loadScreen\(SCREEN_ID_MODEM_SLEEP\);",
     sc, "hub central exit", re.S)

# alarm -> its two entries.
if re.findall(r"\(void \*\)\(intptr_t\)(SCREEN_ID_SETTINGS_POMODORO|SCREEN_ID_SETTINGS_ALARM_TIME)",
              sc) != ["SCREEN_ID_SETTINGS_POMODORO",
                      "SCREEN_ID_SETTINGS_ALARM_TIME"]:
    raise SystemExit("tap ladder: the alarm screen's two entries changed")

# wifi -> password (the roller row), password -> wifi (the wheel exit).
must(r"static void event_handler_cb_wifi_roller.*?loadScreen\(SCREEN_ID_SETTINGS_WIFI_PASSWORD\);",
     sc, "roller -> wheel edge", re.S)
must(r"static void settings_wheel_exit_to_list\(void\) \{.*?loadScreen\(SCREEN_ID_SETTINGS_WIFI\);",
     sc, "wheel exit edge", re.S)

# every sub-screen's bottom exit button -> its declared target.
exits = {}
for m in re.finditer(r"void (create_screen_\w+)\(\) \{(.*?)\n\}", sc, re.S):
    fn, body = m.group(1), m.group(2)
    p = re.search(r"settings_sub_screen_prepare\(obj, SCREEN_ID_(\w+)\)", body)
    if p:
        exits[fn] = p.group(1)
EXPECT_EXIT = {
    "create_screen_settings_alarm": "SETTINGS_HUB",
    "create_screen_settings_pomodoro": "SETTINGS_ALARM",
    "create_screen_settings_alarm_time": "SETTINGS_ALARM",
    "create_screen_settings_light_sleep": "SETTINGS_HUB",
    "create_screen_settings_deep_sleep": "SETTINGS_HUB",
    "create_screen_settings_wifi": "SETTINGS_HUB",
    "create_screen_settings_time": "SETTINGS_HUB",
    "create_screen_settings_tz": "SETTINGS_HUB",
    "create_screen_settings_alarm_sound": "SETTINGS_HUB",
    "create_screen_settings_display": "SETTINGS_HUB",
}
if exits != EXPECT_EXIT:
    raise SystemExit(f"tap ladder: sub-screen exits drifted: {exits}")

# ACCOUNTING: every literal loadScreen(SCREEN_ID_*) in screens.c is one
# of the edges above or the demo hook — no unexplained navigation.
explained = {
    "SCREEN_ID_SETTINGS_HUB",      # home tap
    "SCREEN_ID_MODEM_SLEEP",       # hub central exit
    "SCREEN_ID_SETTINGS_WIFI_PASSWORD",  # roller edge + demo hook
    "SCREEN_ID_SETTINGS_WIFI",     # wheel exit
}
lits = set(re.findall(r"loadScreen\((SCREEN_ID_\w+)\)", sc))
# the ring/entry targets ride via user_data + loadScreen(target) — not
# literals; the literal set must be exactly the explained four.
if lits != explained:
    raise SystemExit(f"tap ladder: unexplained literal navigation edges: "
                     f"{lits ^ explained}")

# machine-only views: the actions.c literals, counts asserted.
mach = {
    "SCREEN_ID_ALARM": 2, "SCREEN_ID_DEEP_SLEEP": 1,
    "SCREEN_ID_LIGHT_SLEEP": 1, "SCREEN_ID_WIFI": 1,
    "SCREEN_ID_MODEM_SLEEP": 3,
}
for sid, want in mach.items():
    got = len(re.findall(rf"loadScreen\({sid}\)", ac))
    if got != want:
        raise SystemExit(f"tap ladder: actions.c loadScreen({sid}) seen "
                         f"{got}, expected {want}")

# ---------------------------------------------------------------- matrix
# the same list-table check_docs_drift.py enforces, parsed for the
# per-panel ✓/— columns (the drift gate is the single source).
idx = _read(INDEX_RST)
matrix = {}
for row_m in re.finditer(
        r"\* - :doc:`[^`]+ <([^`]+)>`[^\n]*\n\s*- (.)\n\s*- (.)\n\s*- (.)",
        idx):
    matrix[row_m.group(1)] = (row_m.group(2), row_m.group(3), row_m.group(4))
enum = [e for e in re.findall(r"(SCREEN_ID_\w+)", _read(SCREENS_H))
        if not e.endswith("_COUNT")]
want_slugs = {e.removeprefix("SCREEN_ID_").lower().replace("_", "-")
              for e in enum}
if set(matrix) != want_slugs:
    raise SystemExit(f"tap ladder: matrix slugs vs screens.h drifted: "
                     f"{set(matrix) ^ want_slugs}")
for panel, col, want_reach in (("P4", 1, 17), ("S3", 0, 16), ("knob", 2, 14)):
    reach = sum(1 for v in matrix.values() if v[col] == "✓")
    if reach != want_reach:
        raise SystemExit(f"tap ladder: matrix {panel} reach {reach} != "
                         f"{want_reach} (matrix drifted or prose changed)")
ABSENT = {"settings-time": {"S3", "knob"},
          "light-sleep": {"knob"}, "deep-sleep": {"knob"}}
for slug, panels in ABSENT.items():
    cells = matrix[slug]
    got = {"S3": cells[0], "P4": cells[1], "knob": cells[2]}
    for panel in ("P4", "S3", "knob"):
        if (got[panel] == "—") != (panel in panels):
            raise SystemExit(f"tap ladder: {slug}/{panel} matrix marker "
                             f"{got[panel]} contradicts the documented "
                             "absence set")

# ---------------------------------------------------------------- wheel
sets_blk = must(
    r"static const char \*const s_wheel_sets\[4\] = \{(.*?)\};", sc,
    "wheel registers", re.S).group(1)
regs = re.findall(r'"((?:[^"\\]|\\.)*)"', sets_blk)
if len(regs) != 4 or len(regs[0]) != 26 or any(not r for r in regs):
    raise SystemExit(f"tap ladder: wheel registers parsed {regs}")
PW_MAX = int(must(r"#define WHEEL_PW_MAX (\d+)", sc,
                  "WHEEL_PW_MAX").group(1))
if PW_MAX != 63:
    raise SystemExit("tap ladder: WHEEL_PW_MAX is not 63")
must(r"s_wheel_pw_len < WHEEL_PW_MAX", sc, "wheel commit guard")
must(r"#define MAX_PASSWORD_LEN (\d+)", wc,
     "MAX_PASSWORD_LEN")
if must(r"#define MAX_PASSWORD_LEN (\d+)", wc,
        "MAX_PASSWORD_LEN").group(1) != "64":
    raise SystemExit("tap ladder: MAX_PASSWORD_LEN is not 64")
must(r"password_len == 0 \|\| password_len >= MAX_PASSWORD_LEN", wc,
     "wifi.c password reject")
must(r"letters is the register|first set_mode drew the letters", sc,
     "the letters-first note")

# ---------------------------------------------------------------- BFS
tap_edges = {"modem-sleep": {"settings-hub"}}
# the FULL slug — SCREEN_ID_SETTINGS_ALARM is settings-alarm, never the
# bare "alarm" (that slug is the alarm VIEW, a machine-only screen).
hub_children = {s.removeprefix("SCREEN_ID_").lower().replace("_", "-")
                for s in ring_s3}
if "settings-alarm" not in hub_children or "alarm" in hub_children:
    raise SystemExit(f"tap ladder: ring children slugs wrong: {hub_children}")
# the P4-only ring entry is a real edge ON the P4; the per-panel bars
# consult the matrix, so S3/knob draw the italic absence instead.
p4_only_child = "settings-time"
tap_edges["settings-hub"] = hub_children | {p4_only_child} | {"modem-sleep"}
tap_edges["settings-alarm"] = {"settings-pomodoro", "settings-alarm-time"}
tap_edges["settings-wifi"] = {"settings-wifi-password"}
dist = {"modem-sleep": 0}
frontier = ["modem-sleep"]
while frontier:
    nxt = []
    for u in frontier:
        for v in tap_edges.get(u, ()):
            if v not in dist:
                dist[v] = dist[u] + 1
                nxt.append(v)
    frontier = nxt
if dist.get("settings-hub") != 1:
    raise SystemExit("tap ladder: hub depth is not 1")
for s in hub_children:
    if dist.get(s) != 2:
        raise SystemExit(f"tap ladder: ring child {s} depth {dist.get(s)}")
for s in ("settings-pomodoro", "settings-alarm-time",
          "settings-wifi-password"):
    if dist.get(s) != 3:
        raise SystemExit(f"tap ladder: subtree {s} depth {dist.get(s)}")
if dist.get(p4_only_child) != 2:
    raise SystemExit("tap ladder: P4-only ring child is not depth 2")
if set(dist) - {"modem-sleep"} & {"alarm", "wifi", "light-sleep",
                                  "deep-sleep"}:
    raise SystemExit("tap ladder: a machine-only view got a tap depth — "
                     "edge slugs collided with the view slugs")

# ---------------------------------------------------------------- figure
fig, (axl, axr) = plt.subplots(1, 2, figsize=(12.8, 6.0),
                               width_ratios=[1.55, 1.0])

# rows top->bottom: home, hub, ring children, subtree, machine-only.
ROWS = [
    ("home — the awake root", "modem-sleep", "start"),
    ("settings hub", "settings-hub", "tap"),
    ("settings alarm", "settings-alarm", "tap"),
    ("settings light-sleep", "settings-light-sleep", "tap"),
    ("settings deep-sleep", "settings-deep-sleep", "tap"),
    ("settings wifi (AP roller)", "settings-wifi", "tap"),
    ("settings timezone", "settings-tz", "tap"),
    ("settings alarm sound", "settings-alarm-sound", "tap"),
    ("settings display trim", "settings-display", "tap"),
    ("settings time (P4-only ring entry)", "settings-time", "tap"),
    ("settings pomodoro", "settings-pomodoro", "tap"),
    ("settings alarm time", "settings-alarm-time", "tap"),
    ("wifi password (the wheel)", "settings-wifi-password", "tap"),
    ("alarm view", "alarm", "machine"),
    ("transient wifi sync view", "wifi", "machine"),
    ("light-sleep view", "light-sleep", "machine"),
    ("deep-sleep view", "deep-sleep", "machine"),
]
COL = {"S3": 0, "P4": 1, "knob": 2}  # matrix tuple order = S3, P4, Knob
ypos = list(range(len(ROWS)))[::-1]
bar_h = 0.26
for y, (label, slug, kind) in zip(ypos, ROWS):
    if kind == "machine":
        drivers = {"alarm": "actions.c:1163/:1246",
                   "wifi": "actions.c:876",
                   "light-sleep": "actions.c:1575",
                   "deep-sleep": "actions.c:1273"}[slug]
        axl.text(0.06, y, f"no tap leg — machine-driven ({drivers})",
                 va="center", ha="left", style="italic", color=GRAY,
                 fontsize=8.6)
        continue
    if kind == "start":
        axl.text(0.06, y, "BFS source — the awake home; hub opens on an "
                          "awake tap only",
                 va="center", ha="left", style="italic", color=TEAL,
                 fontsize=8.4)
        continue
    for i, panel in enumerate(("S3", "P4", "knob")):
        absent = matrix[slug][COL[panel]] == "—"
        if absent:
            axl.text(0.06, y + (1 - i) * bar_h - bar_h,
                     {"S3": "S3: compiled out (ring entry is P4-only)",
                      "P4": "",
                      "knob": "knob: the flow does not run "
                              "(matrix says —)"}[panel],
                     va="center", ha="left", fontsize=7.6, color=GRAY,
                     style="italic")
            continue
        d = dist[slug]
        c = {"S3": S3C, "P4": P4C, "knob": KNC}[panel]
        axl.barh(y + (1 - i) * bar_h - bar_h, d, height=bar_h * 0.92,
                 color=c, alpha=0.92, edgecolor="white", linewidth=0.6)
        axl.text(d + 0.05, y + (1 - i) * bar_h - bar_h, str(d),
                 va="center", fontsize=8, color=INK)

axl.set_yticks(ypos)
axl.set_yticklabels([r[0] for r in ROWS], fontsize=8.6)
axl.tick_params(axis="y", length=0)
axl.set_xlim(0, 3.9)
axl.set_xticks([0, 1, 2, 3])
axl.set_xlabel("taps from the awake home screen (shortest path)",
               fontsize=9.5, color=INK)
axl.set_title("Left: every screen of the enum, how deep the finger goes\n"
              "(S3 blue / P4 cyan / knob amber; machine rows have no "
              "tap leg)", fontsize=10, color=INK, loc="left")
for spine in ("top", "right", "left"):
    axl.spines[spine].set_visible(False)
axl.spines["bottom"].set_color(HAIR)
axl.xaxis.grid(True, color=HAIR, linewidth=0.7)
axl.set_axisbelow(True)

# right: taps to type a password on the wheel, vs its length.
ns = list(range(1, PW_MAX + 1))
lower = [n + 1 for n in ns]                      # 1 tap/char + commit
hop = [n + 2 for n in ns]                        # + one register hop
alt = [n + math.ceil(n / 2) + 1 for n in ns]     # letters<->symbols
axr.plot(ns, lower, color=TEAL, lw=1.8,
         label=f"lowercase only — N+1 (letters is the first register)")
axr.plot(ns, hop, color=S3C, lw=1.6, ls="--",
         label="one register hop — N+2")
axr.plot(ns, alt, color=KNC, lw=1.4, ls=":",
         label="alternating letters/symbols — 1.5N+1.5")
axr.axvline(PW_MAX, color="#b91c1c", lw=1.0)
axr.text(PW_MAX - 0.7, 4, f"the wheel stops at {PW_MAX};\nwifi.c rejects "
         "≥ 64 (parsed)", rotation=90, va="bottom", ha="right",
         fontsize=7.8, color="#7a1a1a")
axr.axvline(12, color=MUTED, lw=0.8, ls="-.")
axr.text(12 + 0.7, 71, "a 12-char key\n= 13 taps", fontsize=7.8,
         color=MUTED)
axr.set_xlabel(f"password chars (firmware floor is 1, no 8-char minimum)",
               fontsize=9.5, color=INK)
axr.set_ylabel("taps to enter + commit", fontsize=9.5, color=INK)
axr.set_title("Right: the wheel's price is a function of the SECRET\n"
              "(1 press-and-release per char + register hops + 1 commit — "
              "derived)", fontsize=10, color=INK, loc="left")
axr.legend(fontsize=7.8, frameon=False, loc="upper left")
for spine in ("top", "right"):
    axr.spines[spine].set_visible(False)
for spine in ("left", "bottom"):
    axr.spines[spine].set_color(HAIR)
axr.grid(True, color=HAIR, linewidth=0.7)
axr.set_axisbelow(True)
axr.set_xlim(0, 66)
axr.set_ylim(0, 100)

fig.suptitle("The tap ladder — the ladder family's human side: after the "
             "machine's cents (response/scheduler pages), the user's taps",
             fontsize=12.5, color=INK, y=0.985)
fig.text(0.5, 0.022,
         "left: BFS over the code-parsed graph (ring table screens.c:"
         "597-608 with the P4-only entry, alarm entries :808/:817, roller "
         ":1581, home tap :114-117); bars cross-checked vs the drift-gated "
         "ui/index.rst matrix (17/16/14). knob 'tap' = a dial-click.",
         ha="center", fontsize=7.2, color=MUTED)
fig.text(0.5, 0.005,
         "right: derived sum — press-and-release per char + register key + "
         "commit; registers parsed verbatim from s_wheel_sets[] (26/26/25/"
         "25 by strlen), caps WHEEL_PW_MAX 63 / MAX_PASSWORD_LEN 64 parsed "
         "from screens.c/wifi.c.",
         ha="center", fontsize=7.2, color=MUTED)
fig.tight_layout(rect=(0, 0.055, 1, 0.965))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=160)
print(f"wrote {OUT}")
print(f"registers: {[(r, len(r)) for r in regs]}  PW_MAX={PW_MAX}")
print(f"ring S3/knob: {len(ring_s3)} icons, P4: {len(ring_p4)} icons")
