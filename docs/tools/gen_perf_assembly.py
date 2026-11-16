#!/usr/bin/env python3
"""Generate the assembly ladder for docs/verification/performance.rst —
the counting axis no other ladder prices: **how many LVGL objects every
screen assembles, and what the build-once model then charges per visit
(zero)**. The tap ladder counts taps (the finger's bill), the static
footprint counts bytes (the RAM's bill), screen-switch.dot draws the
switch's fade — nobody has counted the *population*: the seventeen detached
roots that ``create_screens()`` builds once at boot, before the first
``loadScreen``, and that ``loadScreen`` never destroys (the code's own
comment: "The widgets are created once (loadScreen never destroys them),
so the pointers live here").

Nothing is recalled. Every number is parsed from the live sources at
figure time with SystemExit-failing parsers (the house pattern), and the
parsers double as docs-side twins of the UI's structural rules — the
figure refuses to draw when a screen, a ticker, or a registry entry
appears or vanishes:

  ScreensEnum (screens.h)      the 17 ids, in enum order — the census rows
  create_screen_* bodies       exactly one per id, each rooted at exactly
      ONE detached ``lv_obj_create(0)`` (layout contract 1); bodies are
      brace-matched, not regex-sliced
  object instances             every ``lv_obj_create / lv_label_create /
      lv_arc_create / lv_roller_create`` token plus the moon image
      (moon_screen_prepare -> moon_ui_create_image), counted with the
      create-in-loop multipliers MODELLED from the parsed loop bounds
      (the wheel: 12 sectors x {3 boxes, 2 box dividers} + 12 sector
      dividers + 2 hairlines + 4 mode keys + 5 mode ticks + 3 wedges;
      the hub ring: one icon label per ring entry). Helper bodies are
      resolved transitively (settings_create_roller -> one roller,
      settings_hub_icon -> one label, settings_create_exit_button ->
      container + glyph, settings_sub_screen_prepare -> the exit button,
      create_user_widget_clock -> the ONE shared clock widget, called
      exactly once from the modem-sleep creator); an object inside a loop
      whose bound cannot be parsed is a SystemExit — a loop nobody
      modelled cannot ship a bar
  tick registry                17 ``tick_screen_*`` definitions + the
      ``tick_screen_funcs[]`` table in enum order (ui.c ui_tick dispatches
      ONLY the current screen — the sixteen off-screen roots tick zero
      times)
  create_screens()             calls exactly the 17 creators, in enum order
  ui.c                         ui_init: create_screens() THEN
      loadScreen(SCREEN_ID_MODEM_SLEEP) — every object exists before the
      first frame; loadScreen contains NO creator call (the build-once
      claim is asserted, not quoted)
  hub ring                     7 unconditional entries (each target a
      ScreensEnum member) + exactly one ``#if defined(CONFIG_BOARD_P4)``
      entry (clock icon -> settings-time) — 7/8/7 on S3/P4/knob
  bottom exits                 exactly 10 ``settings_sub_screen_prepare``
      call sites; pomodoro + alarm-time return to the alarm screen, the
      eight hub children to the hub (the tap ladder's exit table, twin)
  entry refresh                ``settings_screen_refresh_for`` dispatches
      exactly the 11 sub-screens — the per-visit cost is a refresh, never
      a rebuild
  moon family                  exactly 4 ``moon_screen_prepare`` call
      sites (airplane / wifi / light / deep — the four roots that share
      the ONE moon texture; the code's own "s_images[] stays at the four
      moon screens"); the alarm and the twelve settings screens are
      moon-less by design
  the knob's P2 dial block     between its ``#ifdef CONFIG_BOARD_KNOB``
      and its #endif: ZERO object creations (the dial cursor restyles
      existing objects — the knob adds no widgets)
  WHEEL_N_SECTORS              parsed == 12

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet; class families keep one colour across both panels.

Run:  docs/.venv/bin/python docs/tools/gen_perf_assembly.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCREENS_C = os.path.join(ROOT, "components", "ui", "screens.c")
SCREENS_H = os.path.join(ROOT, "components", "ui", "screens.h")
UI_C = os.path.join(ROOT, "components", "ui", "ui.c")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_assembly.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity / the label class
VERM = "#c2410c"      # P4 identity / the arc class
VIOLET = "#7c3aed"    # knob identity / the roller class
AMBER = "#b07d10"     # the moon-image class (the shared-disk family)
SLATE = "#64748b"     # the bare-container class

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "text.color": INK,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.grid": True,
    "grid.color": HAIR,
    "grid.linewidth": 0.6,
})


def die(msg: str) -> None:
    raise SystemExit(f"gen_perf_assembly: {msg}")


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def brace_end(src: str, open_idx: int) -> int:
    """Index just past the matching '}' for the '{' at open_idx."""
    depth = 0
    for i in range(open_idx, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    die("unbalanced braces")


# ---------------------------------------------------------------------------
# 1. Enum, creators, tickers, registries
# ---------------------------------------------------------------------------
h_code = strip_comments(open(SCREENS_H).read())
enum_ids: list[str] = []
for e in re.findall(r"\bSCREEN_ID_\w+\b", h_code.split("enum ScreensEnum", 1)[1].split("}", 1)[0]):
    if e not in enum_ids:
        enum_ids.append(e)
if len(enum_ids) != 17:
    die(f"screens.h enum has {len(enum_ids)} ids, expected 17")

c_code = strip_comments(open(SCREENS_C).read())
slug = lambda e: e.removeprefix("SCREEN_ID_").lower()  # noqa: E731

# top-level function map: name -> (body_start, body_end, body)
FUNCS: dict[str, tuple[int, int, str]] = {}
for m in re.finditer(r"^(?:static\s+)?[\w\s\*]*?(\w+)\s*\([^;{)]*\)\s*\{",
                     c_code, flags=re.M):
    start = m.end() - 1
    FUNCS[m.group(1)] = (start, brace_end(c_code, start), c_code[start + 1 : brace_end(c_code, start) - 1])

CREATORS = {e: f"create_screen_{slug(e)}" for e in enum_ids}
for e, fname in CREATORS.items():
    if fname not in FUNCS:
        die(f"missing {fname}")
    body = FUNCS[fname][2]
    if body.count("lv_obj_create(0)") != 1:
        die(f"{e}: expected exactly one detached lv_obj_create(0) root "
            f"(found {body.count('lv_obj_create(0)')})")

cs = FUNCS.get("create_screens", (None, None, ""))[2]
if re.findall(r"\bcreate_screen_\w+\(\)", cs) != [f"{f}()" for f in CREATORS.values()]:
    die("create_screens() does not call the 17 creators in enum order")

tick_defs = sorted(n for n in FUNCS if n.startswith("tick_screen_")
                   and n not in ("tick_screen_by_id",))
if len(tick_defs) != 17:
    die(f"{len(tick_defs)} tick_screen_* definitions, expected 17 "
        f"(the dispatcher tick_screen/tick_screen_by_id are excluded): {tick_defs}")
tbl = re.search(r"tick_screen_func_t tick_screen_funcs\[\]\s*=\s*\{", c_code)
if not tbl:
    die("tick_screen_funcs[] not found")
tbl_end = brace_end(c_code, tbl.end() - 1)
tbl_entries = re.findall(r"\btick_screen_\w+\b", c_code[tbl.end() : tbl_end])
if tbl_entries != [f"tick_screen_{slug(e)}" for e in enum_ids]:
    die("tick_screen_funcs[] is not the 17 tickers in enum order")

ui_code = strip_comments(open(UI_C).read())
# ui.c carries a never-shipped EEZ-generated twin of ui_init/ui_tick behind
# #if defined(EEZ_FOR_LVGL); the figure prices the shipped branch only.
m_eez = list(re.finditer(r"#if defined\(EEZ_FOR_LVGL\)", ui_code))[-1]
m_else = ui_code.index("#else", m_eez.end())
m_end = ui_code.index("#endif", m_else)
ui_code = ui_code[: m_eez.start()] + ui_code[m_else + len("#else") : m_end] + ui_code[m_end + len("#endif") :]
m_init = re.search(r"void ui_init\(\)\s*\{", ui_code)
init_body = ui_code[m_init.end() : brace_end(ui_code, m_init.end() - 1) - 1]
if "create_screens()" not in init_body or "loadScreen(SCREEN_ID_MODEM_SLEEP)" not in init_body:
    die("ui_init no longer builds all screens then loads the airplane home")
m_load = re.search(r"void loadScreen\(enum ScreensEnum screenId\)\s*\{", ui_code)
load_body = ui_code[m_load.end() : brace_end(ui_code, m_load.end() - 1) - 1]
for token in ("lv_scr_load", "ui_clock_attach_to", "moon_ui_screen_switched"):
    if token not in load_body:
        die(f"loadScreen lost its {token} step")
if re.search(r"\b\w*_create\(", load_body):
    die("loadScreen creates objects — the build-once model is broken")

# ---------------------------------------------------------------------------
# 2. Loop model + helper closure
# ---------------------------------------------------------------------------
CLASSES = ["container", "label", "arc", "roller", "moon"]
CREATE_TOKEN = {
    "lv_obj_create": "container",
    "lv_label_create": "label",
    "lv_arc_create": "arc",
    "lv_roller_create": "roller",
}
WHEEL_M = re.search(r"#define WHEEL_N_SECTORS\s+(\d+)", c_code)
if not WHEEL_M or int(WHEEL_M.group(1)) != 12:
    die("WHEEL_N_SECTORS is missing or not 12 — the wheel model must be re-read")
WHEEL_N = int(WHEEL_M.group(1))

ring_block = re.search(r"static const struct hub_icon ring\[\]\s*=\s*\{(.*?)\};", c_code, flags=re.S)
if not ring_block:
    die("hub ring[] table not found")
rb = ring_block.group(1)
p4 = re.search(r"#if defined\(CONFIG_BOARD_P4\)(.*?)#endif", rb, flags=re.S)
if not p4:
    die("the ring's P4-only clock entry vanished — the 7/8 split is a lie")
p4_entries = re.findall(r"\{GLYPH_\w+,\s*&\w+,\s*(SCREEN_ID_\w+)\}", p4.group(1))
base_entries = re.findall(r"\{GLYPH_\w+,\s*&\w+,\s*(SCREEN_ID_\w+)\}",
                          rb[: p4.start(0)] + rb[rb.index("#endif", p4.start(0)) + 6 :])
if len(p4_entries) != 1 or p4_entries[0] != "SCREEN_ID_SETTINGS_TIME":
    die(f"the P4-only ring entry is not exactly the settings-time clock entry: {p4_entries}")
if not all(t in enum_ids for t in p4_entries + base_entries):
    die("a ring target is not a ScreensEnum member")
RING_BASE, RING_P4 = len(base_entries), len(base_entries) + 1
if RING_BASE != 7:
    die(f"hub ring carries {RING_BASE} unconditional entries, expected 7")

FOR_HEAD = re.compile(r"for\s*\(\s*(?:int|size_t)\s+(\w+)\s*=\s*0\s*;\s*\1\s*<\s*"
                      r"(WHEEL_N_SECTORS|\d+|\w+)\s*;\s*[^)]*\)")


def loop_multiplier(body: str, body_start: int, token_off: int, owner: str) -> int:
    """Product of the bounds of every for-loop enclosing the token. The hub
    ring's sizeof-based loop is the one modelled special case (it is the
    ring, and its count is parsed above); any other unparseable bound dies."""
    depth, mult, i = 0, 1, token_off
    while i > 0:
        ch = body[i]
        if ch == "}":
            depth += 1
        elif ch == "{":
            if depth:
                depth -= 1
            else:
                head = body[:i].rstrip()
                head = head[head.rfind("\n") + 1 :].strip()
                if head.endswith(")"):
                    if not re.match(r"(if|while|else if)\b", head):
                        fm = FOR_HEAD.fullmatch(head)
                        if not fm:
                            die(f"object creation inside an unmodelled block "
                                f"('{head[:60]}') in {owner}")
                        bound = fm.group(2)
                        if bound == "WHEEL_N_SECTORS":
                            mult *= WHEEL_N
                        elif bound.isdigit():
                            mult *= int(bound)
                        elif bound == "n" and owner == "create_screen_settings_hub" \
                                and "sizeof(ring)" in body:
                            mult *= RING_BASE  # the hub ring: 7 entries per build
                        else:
                            die(f"unmodelled loop bound '{bound}' around an object "
                                f"creation in {owner}")
        i -= 1
    return mult


# Special-case profiles for the cross-file seam (moon image lives in moon_ui.c).
SPECIAL_PROFILE = {
    "moon_screen_prepare": dict.fromkeys(CLASSES, 0) | {"moon": 1},
}
PROFILE_CACHE: dict[str, dict[str, int]] = {}


def profile(fname: str, _stack: tuple[str, ...] = ()) -> dict[str, int]:
    """Object instances assembled by one call to fname (loops modelled,
    file-local helpers resolved transitively; handlers never count — they
    are registered by name, not called during creation)."""
    if fname in SPECIAL_PROFILE:
        return SPECIAL_PROFILE[fname]
    if fname in PROFILE_CACHE:
        return PROFILE_CACHE[fname]
    if fname in _stack:
        die(f"creation cycle in {fname}")
    if fname not in FUNCS:
        die(f"unknown creation helper: {fname}")
    start, _, body = FUNCS[fname]
    out = dict.fromkeys(CLASSES, 0)
    for tok, cls in CREATE_TOKEN.items():
        for m in re.finditer(rf"\b{tok}\(", body):
            out[cls] += loop_multiplier(body, start, m.start(), fname)
    for callee in FUNCS:
        if callee == fname or callee.startswith("tick_screen_"):
            continue
        for m in re.finditer(rf"\b{callee}\(", body):
            if callee.startswith("create_screen_"):
                die(f"creator {fname} calls creator {callee}")
            sub = profile(callee, _stack + (fname,))
            mult = loop_multiplier(body, start, m.start(), fname)
            for cls, n in sub.items():
                out[cls] += n * mult
    PROFILE_CACHE[fname] = out
    return out


counts = {e: profile(CREATORS[e]) for e in enum_ids}

# every create token lives inside a visited function (nothing unaccounted)
visited = set(PROFILE_CACHE) | set(CREATORS.values()) | {"create_user_widget_clock"}
for m in re.finditer(r"\blv_\w+_create\(", c_code):
    if not any(s <= m.start() < e for n, (s, e, _) in FUNCS.items() if n in visited):
        die(f"unaccounted object creation at offset {m.start()} ({m.group(0)})")

# the clock widget: exactly one call site, owned by the airplane home
cw_sites = [m for m in re.finditer(r"\bcreate_user_widget_clock\(", c_code)
            if any(s <= m.start() < e for n, (s, e, _) in FUNCS.items() if n.startswith("create_screen_"))]
if len(cw_sites) != 1:
    die(f"create_user_widget_clock must be called exactly once (layout contract 2), found {len(cw_sites)}")
ms_body = FUNCS["create_screen_modem_sleep"][2]
if "create_user_widget_clock(" not in ms_body:
    die("the clock widget is not built on the modem-sleep root")

# moon family: the four roots; everything else moon-less
moon_roots = [e for e in enum_ids if "moon_screen_prepare(" in FUNCS[CREATORS[e]][2]]
if moon_roots != ["SCREEN_ID_MODEM_SLEEP", "SCREEN_ID_WIFI",
                  "SCREEN_ID_LIGHT_SLEEP", "SCREEN_ID_DEEP_SLEEP"]:
    die(f"the moon family is {moon_roots}, not the four sleep-state roots")

# bottom exits + entry refresh (the tap ladder's tables, re-derived)
ssp = re.findall(r"settings_sub_screen_prepare\((?:obj|o), (SCREEN_ID_\w+)\)", c_code)
owner_exit: dict[str, str] = {}
for e, fname in CREATORS.items():
    m = re.search(r"settings_sub_screen_prepare\(obj, (SCREEN_ID_\w+)\)", FUNCS[fname][2])
    if m:
        owner_exit[e] = m.group(1)
expected_exit = {
    "SCREEN_ID_SETTINGS_ALARM": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_POMODORO": "SCREEN_ID_SETTINGS_ALARM",
    "SCREEN_ID_SETTINGS_ALARM_TIME": "SCREEN_ID_SETTINGS_ALARM",
    "SCREEN_ID_SETTINGS_LIGHT_SLEEP": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_DEEP_SLEEP": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_WIFI": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_TIME": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_TZ": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_ALARM_SOUND": "SCREEN_ID_SETTINGS_HUB",
    "SCREEN_ID_SETTINGS_DISPLAY": "SCREEN_ID_SETTINGS_HUB",
}
if len(ssp) != 10 or owner_exit != expected_exit:
    die(f"bottom-exit table drifted (10 expected): found {len(ssp)} — {owner_exit}")
rf = FUNCS["settings_screen_refresh_for"][2]
rf_cases = sorted(set(re.findall(r"case (SCREEN_ID_\w+):", rf)))
rf_expected = sorted(e for e in enum_ids if e.startswith("SCREEN_ID_SETTINGS_")
                     and e != "SCREEN_ID_SETTINGS_HUB")
if rf_cases != rf_expected:
    die(f"settings_screen_refresh_for dispatches {rf_cases}, expected the 11 sub-screens {rf_expected}")

# knob dial block creates nothing
c_raw = open(SCREENS_C).read()
k0 = c_raw.rfind("#ifdef CONFIG_BOARD_KNOB", 0, c_raw.index("P2 dial surface"))
k1 = c_raw.index("#endif /* CONFIG_BOARD_KNOB */", k0)
if re.search(r"\blv_\w+_create\(", c_raw[k0:k1]):
    die("the knob's P2 dial block creates objects — the *no widgets* claim broke")

# ---------------------------------------------------------------------------
# 3. The figure
# ---------------------------------------------------------------------------
totals = {e: sum(counts[e].values()) for e in enum_ids}
grand = sum(totals.values())
home = totals["SCREEN_ID_MODEM_SLEEP"]
wheel = totals["SCREEN_ID_SETTINGS_WIFI_PASSWORD"]
board_total = {b: grand - RING_BASE + r
               for b, r in (("S3", RING_BASE), ("P4", RING_P4), ("knob", RING_BASE))}

CLASS_STYLE = {
    "container": (SLATE, "containers (lv_obj)"),
    "label": (BLUE, "labels (lv_label)"),
    "arc": (VERM, "arcs (lv_arc)"),
    "roller": (VIOLET, "rollers (lv_roller)"),
    "moon": (AMBER, "moon image (shared texture)"),
}

fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(13.4, 6.6),
                                 gridspec_kw={"width_ratios": [1.45, 1]})

rows = sorted(enum_ids, key=lambda e: -totals[e])
ys = list(range(len(rows)))[::-1]
left = {e: 0 for e in rows}
for cls in CLASSES:
    col, lab = CLASS_STYLE[cls]
    vals = [counts[e][cls] for e in rows]
    ax_l.barh(ys, vals, left=[left[e] for e in rows], color=col,
              edgecolor="white", linewidth=0.6, height=0.72, label=lab)
    for y, e, v in zip(ys, rows, vals):
        if v:
            ax_l.text(left[e] + v / 2, y, str(v), ha="center", va="center",
                      fontsize=6.6, color="white", fontweight="bold")
    left = {e: left[e] + counts[e][cls] for e in rows}
for y, e in zip(ys, rows):
    ax_l.text(totals[e] + 0.7, y, str(totals[e]), ha="left", va="center",
              fontsize=8, color=INK, fontweight="bold")
ax_l.set_yticks(ys)
ax_l.set_yticklabels([e.removeprefix("SCREEN_ID_").replace("_", "-").lower() for e in rows],
                     fontsize=8)
ax_l.set_xlabel("object instances assembled at boot (create_screen_* + resolved helpers)",
                fontsize=9)
ax_l.set_xlim(0, max(totals.values()) * 1.30)
ax_l.set_title("THE BILL — 17 SCREENS, BUILT ONCE AT BOOT", fontsize=10, color=INK, pad=8)
ax_l.legend(loc="lower right", fontsize=7.2, framealpha=0.95)
ax_l.grid(axis="y", visible=False)

IDENT = {"S3": BLUE, "P4": VERM, "knob": VIOLET}
by = [2, 1, 0]
bx = ["S3 — 7 ring icons", "P4 — 8 ring icons\n(clock icon, CONFIG_BOARD_P4)", "knob — 7 ring icons"]
ax_r.barh(by, [grand - RING_BASE] * 3, color=[IDENT[b] for b in ("S3", "P4", "knob")],
          alpha=0.5, edgecolor="white", height=0.62,
          label="the shared assembly (one screens.c on all three)")
ax_r.barh(by, [RING_BASE, RING_P4, RING_BASE], left=[grand - RING_BASE] * 3,
          color=[IDENT[b] for b in ("S3", "P4", "knob")],
          edgecolor="white", height=0.62, label="hub ring icon labels (parsed ring[])")
for y, b in zip(by, ("S3", "P4", "knob")):
    ax_r.text(board_total[b] + 1, y, f"{board_total[b]}", va="center", fontsize=9,
              color=IDENT[b], fontweight="bold")
ax_r.text(grand - RING_BASE - 2, 1, "identical bars — the same parsed bodies",
          va="center", ha="right", fontsize=7, color=MUTED)
ax_r.set_yticks(by)
ax_r.set_yticklabels(bx, fontsize=8.5)
# Headroom above the top bar for the ledger box, below the bottom bar for
# the legend — a text box drawn OVER a bar is this family's recurring bug.
ax_r.set_ylim(-1.5, 3.8)
ax_r.set_xlim(0, max(board_total.values()) * 1.22)
ax_r.set_xlabel("object instances per boot (create_screens runs once)", fontsize=9)
ax_r.set_title("PER VARIANT — ONE FILE, THREE PANELS, ONE BILL EACH",
               fontsize=10, color=INK, pad=8)
ax_r.text(0.02, 0.985,
          "the build-once ledger:\n"
          f"  the awake home SHOWS {home} objects;\n"
          f"  {grand} EXIST from boot — the hidden city\n"
          f"  the wheel alone: {wheel} ({100.0 * wheel / grand:.0f} % of the bill)\n"
          "  per visit: 0 created, 0 destroyed — loadScreen\n"
          "  swaps roots; the 11 sub-screens pay an entry\n"
          "  REFRESH (rollers re-seed), never a rebuild\n"
          "  ui_tick dispatches ONLY the current screen;\n"
          "  4 roots share the ONE moon image; the clock\n"
          "  widget reparents (ui_clock_attach_to, contract 2)",
          transform=ax_r.transAxes, ha="left", va="top", fontsize=7.2,
          color=INK, bbox=dict(boxstyle="round,pad=0.4", fc="#f6f7f9", ec=HAIR))
ax_r.grid(axis="y", visible=False)
ax_r.legend(loc="lower right", fontsize=7.2, framealpha=0.95)

fig.suptitle("THE ASSEMBLY LADDER — every screen's object budget: built once at boot, visited free",
             fontsize=12, color=INK, y=0.995)
fig.tight_layout(rect=(0, 0, 1, 0.965))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=160)
print(f"wrote {OUT}")
print("per-screen:", ", ".join(f"{slug(e)}={totals[e]}" for e in enum_ids))
print(f"grand={grand} home={home} wheel={wheel} boards={board_total}")
