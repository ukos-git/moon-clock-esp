#!/usr/bin/env python3
"""Generate the TF hour-cache footprint comparison figure for
docs/verification/performance.rst — the fourth resource axis next to
perf_hourly (how long), perf_pacing (how often), perf_dim (how bright)
and perf_mem (how much RAM): **how much one card month holds**, per
board. Every number is parsed from the live sources or arithmetic on
parsed numbers; the card itself is never touched (the real JPEG q
sizes are measured nowhere — the figure says so and stops).

Parsed at figure time:

  components/ui/moon_tfcache.c
    #define TF_MAX_HOURS 709                — the file cap
    SYNODIC_S ((int64_t)(29.53 * 86400))    — the eviction window: one
                                             synodic month, in seconds
    s_enc_out_cap = S*S + S*S/2 + 1024      — the encoder out buffer;
                                             its size is the CODE'S OWN
                                             bound on the largest valid
                                             cached file ("the max a
                                             valid cached file can be",
                                             the s_fetch_in comment)
    s_enc_in (GRAY probe): S*S              — fmt-sized encode input
    s_dec_rgb: 3*S*S RGB888                 — decode out, LAZY since
                                             task #38 (the eager 470 KB
                                             starved the S3 codec path;
                                             a cold folder never decodes)
    s_fetch_in: sized s_enc_out_cap, lazy   — the persistent fetch input
  components/ui/moon_ui.c
    #define MOON_PX 650 (P4) / 400 (S3)     — the same pair gen_perf_mem.py parses
  components/ui/Kconfig
    UI_MOON_TFCACHE_JPEG_Q default 90       — the encoder quality knob
    UI_MOON_TFCACHE (default y)             — the figure is the default build

Left panel: the per-frame codec scratch — resident pair (the device's
own boot log prints it: "ready: ... scratch=NN KB") vs the two lazy
allocations, per board. Right panel: what a FULL card month costs —
the raw decoded payload (709 x S^2) against the on-card worst case
(709 x the encoder out bound), on a log MiB axis. The knob rides along
as the violet footnote: no SD socket, never stores.

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet. Hatched = allocated lazily / a bound, not a measurement.

Run:  docs/.venv/bin/python docs/tools/gen_perf_cache.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
TFCACHE = os.path.join(REPO, "components", "ui", "moon_tfcache.c")
MOON_UI = os.path.join(REPO, "components", "ui", "moon_ui.c")
KCONFIG = os.path.join(REPO, "components", "ui", "Kconfig")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_cache.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # S3 identity
VERM = "#c2410c"    # P4 identity
VIOLET = "#7c3aed"  # knob identity
AMBER = "#b07d10"


def must(text: str, pattern: str, what: str, src: str):
    m = re.search(pattern, text)
    if not m:
        raise SystemExit(f"{os.path.basename(src)}: {what} not found — "
                         "the figure refuses to hard-code it")
    return m


def main() -> None:
    tf = open(TFCACHE, encoding="utf-8").read()
    ui = open(MOON_UI, encoding="utf-8").read()
    kc = open(KCONFIG, encoding="utf-8").read()

    cap = int(must(tf, r"#define TF_MAX_HOURS (\d+)", "TF_MAX_HOURS",
                   TFCACHE).group(1))
    syn_d = float(must(tf, r"SYNODIC_S \(\(int64_t\)\((\d+\.\d+) \* 86400\)\)",
                       "SYNODIC_S days", TFCACHE).group(1))
    # the encoder out capacity formula, parsed as its two coefficients
    oc = must(tf, r"s_enc_out_cap = \(size_t\)S \* S \+ \(size_t\)S \* S / (\d+) \+ (\d+);",
              "encoder out-capacity formula", TFCACHE)
    div, tail = int(oc.group(1)), int(oc.group(2))
    dec = must(tf, r"static uint8_t \*s_dec_rgb; /\* (\d+)\*S\*S RGB888",
               "decode out 3*S*S comment", TFCACHE).group(1)
    dec_k = int(dec)
    q_default = must(kc, r"config UI_MOON_TFCACHE_JPEG_Q(?:[\s\S]{0,200}?)default (\d+)",
                     "UI_MOON_TFCACHE_JPEG_Q default", KCONFIG).group(1)
    must(kc, r"config UI_MOON_TFCACHE\b(?:[\s\S]{0,120}?)default y",
         "UI_MOON_TFCACHE default y", KCONFIG)
    px = must(ui, r"#if defined\(CONFIG_BOARD_P4\)\s*\n#define MOON_PX (\d+)\s*\n#else\s*\n#define MOON_PX (\d+)",
              "MOON_PX pair", MOON_UI)
    S_P4, S_S3 = int(px.group(1)), int(px.group(2))

    def boards():
        for name, S, color in (("S3 — 466 px (disk 400)", S_S3, BLUE),
                               ("P4 — 720 px (disk 650)", S_P4, VERM)):
            enc_in = S * S
            out_cap = S * S + S * S // div + tail
            yield name, S, color, enc_in, out_cap, dec_k * S * S

    plt.rcParams.update({
        "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
        "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
        "axes.titlecolor": INK, "figure.facecolor": "white",
    })
    fig, axes = plt.subplots(1, 2, figsize=(11.4, 3.9))

    # ---- (a) per-frame codec scratch: resident vs lazy ------------------
    ax = axes[0]
    rows = []
    for name, S, color, enc_in, out_cap, dec_rgb in boards():
        rows.append((f"{name}\ns_enc_in (GRAY probe)\nS² = {enc_in:,} B",
                     enc_in / 1024, color, 0.85, False))
        rows.append((f"s_enc_out (out cap)\nS²+S²/{div}+{tail} = {out_cap:,} B",
                     out_cap / 1024, color, 0.55, False))
        rows.append((f"s_fetch_in (fetch input)\nsized = out cap, lazy (#38)",
                     out_cap / 1024, color, 0.55, True))
        rows.append((f"s_dec_rgb (decode out)\n{dec_k}·S² = {dec_rgb:,} B, lazy (#38)",
                     dec_rgb / 1024, color, 0.35, True))
    ys = range(len(rows))
    for y, (label, kib, color, alpha, lazy) in enumerate(rows):
        ax.barh(y, kib, color=color, alpha=alpha, height=0.62,
                edgecolor=color, hatch="//" if lazy else None, linewidth=0.8)
        ax.text(kib * 1.03, y, f"{kib:,.0f} KiB", va="center", fontsize=7,
                color=INK)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=6.4)
    ax.invert_yaxis()
    ax.set_xlabel("KiB per frame's codec scratch (linear)")
    ax.set_title("one hour's frame through the codec — the resident pair\n"
                 "the boot log prints ('ready: ... scratch=NN KB'), the hatched\n"
                 "pair allocated lazily since task #38 (470 KB starved the S3)",
                 fontsize=8)
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)
    ax.set_xlim(0, max(r[1] for r in rows) * 1.22)

    # ---- (b) a full card month: raw payload vs on-card bound -------------
    ax = axes[1]
    rows = []
    for name, S, color, enc_in, out_cap, dec_rgb in boards():
        raw = cap * S * S
        bound = cap * out_cap
        rows.append((f"{name}\nraw decoded payload\ncap {cap} × S² = {raw/2**20:.0f} MiB",
                     raw / 2**20, color, 0.35, True,
                     "what 709 frames DECODE to (uncompressed)"))
        rows.append((f"on-card WORST case\ncap {cap} × out-cap = {bound/2**20:.0f} MiB",
                     bound / 2**20, color, 0.85, False,
                     "the encoder out buffer sizes every valid file"))
    ys = range(len(rows))
    for i, (label, mib, color, alpha, hatch, note) in enumerate(rows):
        ax.barh(i, mib, color=color, alpha=alpha, height=0.62,
                edgecolor=color, hatch="//" if hatch else None, linewidth=0.8)
        ax.text(mib * 1.03, i, f"{mib:,.0f} MiB", va="center", fontsize=7,
                color=INK)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=6.4)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(20, 1200)
    ax.set_xlabel("MiB for ONE FULL card month (log)")
    ax.set_title(f"a card month of hours — {cap} files is {syn_d:.2f} d of them\n"
                 f"(SYNODIC_S = ceil + slack), JPEG q{q_default}; the real JPEGs are far "
                 "smaller,\nnever measured here — the card is only bounded",
                 fontsize=8)
    ax.set_ylim(len(rows) - 0.5 + 1.1, -0.5)  # blank band under the bars
    ax.text(0.985, 0.015,
            f"eviction floor: newer of (a) one {syn_d:.2f} d cycle back and\n"
            "(b) the new-moon floor the producers set — the header's\n"
            "\"never store more than one synodic month of hourly frames\".\n"
            "knob: NO SD socket — never stores (always the bake path,\n"
            "see the boot ladder). Subsystem exists only with\n"
            "UI_MOON_TFCACHE=y (default y).",
            transform=ax.transAxes, ha="right", va="bottom", fontsize=6.2,
            color=MUTED)
    ax.grid(axis="x", color=HAIR, linewidth=0.5)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(cap={cap} syn={syn_d}d q={q_default} S3={S_S3} P4={S_P4} "
          f"out-cap={S_S3**2 + S_S3**2//div + tail}/{S_P4**2 + S_P4**2//div + tail})")


if __name__ == "__main__":
    main()
