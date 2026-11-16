#!/usr/bin/env python3
"""Generate the brightness-ladder figure for docs/verification/performance.rst.

The other perf figures measure WORK (gen_perf_hourly.py, gen_perf_boot.py,
gen_perf_variants.py) or SELF-WAKE FREQUENCY (gen_perf_pacing.py). This
one measures the third axis the sleep machine lives on: the panel's
BRIGHTNESS states and how long each transition takes — the 0x51
hardware-brightness family (the sanctioned ramp exception in
docs/implementation/display.rst). Every value is PARSED from the live
sources at figure time (the house pattern — nothing hand-placed, no
fabricated device measurements):

  UI_LIGHT_SLEEP_DIM_PCT (Kconfig default 25)   components/ui/Kconfig
  UI_CLOCK_ALPHA_PCT     (Kconfig default 20)   components/ui/Kconfig
  UI_SCREEN_SWITCH_FADE  (Kconfig default 240)  components/ui/Kconfig
  UI_MOON_HOURLY_FADE_MS (default UI_SCREEN_SWITCH_FADE — the SYMBOL
      indirection, resolved exactly like Kconfig does)
  #define FADE_STEPS 12                         components/ui/moon_ui.c
  each step = one 0x51 SPI param transaction, fade_ramp's own comment
      puts it at ~100 us — the single writes (deep-sleep backlight
      off actions.c:1347, wake relight :1388) are ONE such step.

Left panel: the brightness timeline — awake 100 %, the light-sleep
entry RAMPING to UI_LIGHT_SLEEP_DIM_PCT (moon_ui_panel_dim_ramp: the
ramp starts from the panel's ACTUAL tracked level, the #34 no-flash
rule), the clock container separately at UI_CLOCK_ALPHA_PCT (an LVGL
style write, no panel-io at all), the deep-sleep entry dropping to 0
with a SINGLE 0x51 write (not a ramp — the screen is already
heading out of sight), the wake ramping back, and the hourly refresh
riding the SAME ramp family: two legs with the produce window in
between (its duration is perf_hourly.png's story, not this one's).
Right panel: time-to-target per transition, log ms — the ramps at
their parsed Kconfig times, the two single writes at the 0x51
transaction floor (~0.1 ms, annotated, not measured).

Colours follow gen_perf_pacing.py: blue = awake, amber = the sleep
ladder, violet = the hourly refresh, gray = the ramp steps.

Run:  docs/.venv/bin/python docs/tools/gen_perf_dim.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
UI_KCONFIG = os.path.join(ROOT, "components", "ui", "Kconfig")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_dim.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # the awake state
AMBER = "#b07d10"    # the sleep ladder
VIOLET = "#7c3aed"   # the hourly refresh
GRAY = "#787878"     # the ramp steps (0x51 transactions)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def kconfig_default(path: str, symbol: str, name: str = "",
                    _depth: int = 0) -> tuple[int, str]:
    """The effective default of an int Kconfig symbol (gen_perf_pacing.py's
    idiom): the first literal ``default <int>``, or — the house idiom for
    coupled timings — the first ``default <SYMBOL>`` followed recursively.
    Returns (value, provenance)."""
    if _depth > 4:
        raise SystemExit(f"{symbol}: default chain deeper than 4 links")
    name = name or os.path.relpath(path, ROOT)
    text = _read(path)
    start = re.search(rf"^[ \t]*config {symbol}[ \t]*$", text, re.M)
    if not start:
        raise SystemExit(f"{symbol}: not found in {name}")
    nxt = re.search(r"^[ \t]*(?:config|menu|endmenu|choice|endchoice)\b",
                    text[start.end():], re.M)
    block = text[start.end():start.end() + (nxt.start() if nxt else len(text))]
    m = re.search(r"^[ \t]+default ([A-Za-z_][A-Za-z0-9_]*|\d+)", block, re.M)
    if not m:
        raise SystemExit(f"{symbol}: no default line in {name}")
    tok = m.group(1)
    if tok.isdigit():
        return int(tok), f"{name}:{tok}"
    val, _ = kconfig_default(path, tok, name, _depth + 1)
    return val, f"via {tok}"


def c_define(path: str, symbol: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.basename(path)}")
    return int(m.group(1))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    dim_pct, dim_prov = kconfig_default(UI_KCONFIG, "UI_LIGHT_SLEEP_DIM_PCT",
                                        "ui/Kconfig")
    alpha_pct, alpha_prov = kconfig_default(UI_KCONFIG, "UI_CLOCK_ALPHA_PCT",
                                            "ui/Kconfig")
    fade_ms, fade_prov = kconfig_default(UI_KCONFIG, "UI_SCREEN_SWITCH_FADE",
                                         "ui/Kconfig")
    hourly_ms, hourly_prov = kconfig_default(UI_KCONFIG,
                                             "UI_MOON_HOURLY_FADE_MS",
                                             "ui/Kconfig")
    steps = c_define(MOON_UI, "FADE_STEPS")
    step_us = 0.1  # ms — one 0x51 SPI param transaction (fade_ramp comment)

    # ---- (a) the brightness timeline ----------------------------------
    fig, (ax, axb) = plt.subplots(1, 2, figsize=(11.6, 4.4),
                                  gridspec_kw={"width_ratios": [1.5, 1]})
    # segments: (kind, x_from, x_to, pct_from, pct_to, color, label)
    # x is schematic timeline units; ramps get width proportional to ms
    w_ramp = fade_ms / 100.0   # same unit scale: 100 ms == 1 unit
    x = 0.0
    segs = []

    def seg(kind, dur, pct0, pct1, color, label):
        nonlocal x
        segs.append((kind, x, x + dur, pct0, pct1, color, label))
        x += dur

    seg("flat", 1.4, 100, 100, BLUE, "awake\n100 %")
    seg("ramp", w_ramp, 100, dim_pct, AMBER, f"light-sleep entry\ndim ramp {fade_ms} ms")
    seg("flat", 1.4, dim_pct, dim_pct, AMBER,
        f"light sleep {dim_pct} %\nclock {alpha_pct} % opa")
    seg("step", 0.12, dim_pct, 0, AMBER, "deep entry\nONE 0x51 ~0.1 ms")
    seg("flat", 1.0, 0, 0, AMBER, "deep sleep\nbacklight OFF")
    seg("ramp", w_ramp, 0, 100, BLUE, f"wake ramp\n{fade_ms} ms")
    seg("ramp", w_ramp, 100, 0, VIOLET, f"hourly leg 1\n{hourly_ms} ms")
    seg("flat", 1.6, 0, 0, VIOLET, "produce window\n(perf_hourly.png)")
    seg("ramp", w_ramp, 0, 100, VIOLET, f"hourly leg 2\n{hourly_ms} ms")

    for kind, x0, x1, p0, p1, color, label in segs:
        if kind == "flat":
            ax.plot([x0, x1], [p0, p1], "-", color=color, linewidth=2.6,
                    solid_capstyle="butt", zorder=3)
            ax.fill_between([x0, x1], 0, p0, color=color, alpha=0.10, zorder=1)
        elif kind == "ramp":
            ax.plot([x0, x1], [p0, p1], "-", color=color, linewidth=2.6,
                    zorder=3)
            # the discrete 0x51 steps the ramp actually writes
            for i in range(steps + 1):
                px = x0 + (x1 - x0) * i / steps
                py = p0 + (p1 - p0) * i / steps
                ax.plot([px], [py], ".", color=GRAY, markersize=2.6,
                        alpha=0.85, zorder=4)
        else:  # single write
            ax.plot([x0, x1], [p0, p1], "-", color=color, linewidth=2.6,
                    solid_capstyle="butt", zorder=3)
        ax.text((x0 + x1) / 2, max((p0 + p1) / 2, 0) + 4, label, fontsize=5.9,
                color=MUTED, ha="center", va="bottom", zorder=5)
    ax.set_xlim(0, x)
    ax.set_ylim(-2, 118)
    ax.set_yticks([0, dim_pct, alpha_pct, 100])
    ax.set_yticklabels([f"0", f"{dim_pct} %", f"{alpha_pct} % (clock)", "100 %"],
                       fontsize=6.5)
    ax.set_xlabel("schematic timeline — transitions carry their real ms\n"
                  "(dots: the ramp's {} 0x51 steps, {} ms apart)".format(
                      steps, round(fade_ms / steps)))
    ax.set_ylabel("panel hardware brightness (%)")
    ax.set_title("the brightness ladder — one 0x51 family writes them all\n"
                 "(moon_ui_panel_dim_ramp starts from the TRACKED level: "
                 "no flash on re-entry)")
    ax.grid(axis="y", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    # ---- (b) time-to-target, log ms ------------------------------------
    rows = [
        (f"light-sleep dim ramp\n{steps} steps × {round(fade_ms/steps)} ms",
         fade_ms, AMBER, fade_prov),
        (f"wake ramp back\n(sleep screens are both-moon:\nthis IS the restore)",
         fade_ms, AMBER, fade_prov),
        (f"hourly fade leg (each)\nUI_MOON_HOURLY_FADE_MS", hourly_ms, VIOLET,
         hourly_prov),
        ("deep-sleep backlight off\nmoon_ui_panel_brightness_set(0)", step_us,
         AMBER, "actions.c:1347"),
        ("alarm/wake relight\nbrightness_set(100)", step_us, BLUE,
         "actions.c:1388"),
    ]
    ys = range(len(rows))
    for y, (label, ms, color, prov) in enumerate(rows):
        axb.barh(y, ms, color=color, alpha=0.6, height=0.6, edgecolor=color)
        axb.text(ms * 1.15, y, f"{ms:g} ms · {prov}", va="center", fontsize=6.2,
                 color=MUTED)
    axb.set_yticks(list(ys), [r[0] for r in rows], fontsize=6.6)
    axb.invert_yaxis()
    axb.set_xscale("log")
    axb.set_xlim(0.06, 4000)
    axb.set_xlabel("time to target, ms (log) — ramps parsed, single writes\n"
                   "at the one-0x51-transaction floor (~100 µs, per fade_ramp's "
                   "own comment)")
    axb.set_title("time to target per transition\n(ramp vs single write)")
    axb.grid(axis="x", color=HAIR, linewidth=0.6)
    axb.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(dim={dim_pct} alpha={alpha_pct} fade={fade_ms} hourly={hourly_ms} "
          f"steps={steps})")


if __name__ == "__main__":
    main()
