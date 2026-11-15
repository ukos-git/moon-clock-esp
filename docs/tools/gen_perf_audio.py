#!/usr/bin/env python3
"""Generate the alarm-loudness figure for docs/verification/performance.rst.

The perf family covers WORK (gen_perf_hourly.py, gen_perf_boot.py,
gen_perf_variants.py), SELF-WAKE FREQUENCY (gen_perf_pacing.py),
BRIGHTNESS (gen_perf_dim.py) and BYTES (gen_perf_mem.py). This one
measures the fifth axis, the one the alarm screen lives on: HOW LOUD,
and with what rhythm — the ES8311 ring/preview chain of
``components/ui/alarm_audio.c``. Every value is PARSED from the live
sources at figure time (the house pattern — nothing hand-placed, no
fabricated bench measurements; dB and ms labels are DERIVED from the
parsed constants, and the two annotated pacing numbers quote the
code's own comments):

  #define RING_SAMPLE_RATE 22050 / LOOP_MS 1000 / CHUNK_SAMPLES 1024
          RING_VOL_FULL 75 / RING_RAMP_MS 2500 / PREV_VOL_FULL 75 /
          PREV_RAMP_MS 300 / RING_FADE_STEP_MS 10      alarm_audio.c
  #define ALARM_SOUND_COUNT 4                           alarm_audio.h
  the stop fade's step: the ``vol -= 5`` of play_task's fade loop
  ALARM_NAMES + the four loop_gen_all frequency arrays
  the codec curve: the RING_VOL_FULL comment's own "(0 -> -50 dB,
          100 -> 0 dB)" — the dB tick labels are that line, linear
  chunk duration 1024/22050 s and ring level spacing 2500/75 ms are
          DERIVED (division of parsed constants), not measured.

Left panel: the volume timeline — codec open at 0, the ring ramp
0 -> RING_VOL_FULL over RING_RAMP_MS (dots: the CHUNK_SAMPLES
blocking writes that PACE the stream — the ramp's wall-clock formula
is computed from xTaskGetTickCount() exactly because the write is the
metronome), the dashed "until touch" hold (one LOOP_MS lap per pass),
the stop fade as its real -5-per-10 ms staircase, codec close; then
the preview as the same machine with its parsed 300 ms ramp and
exactly ONE pass. Right panel: the four loops as parsed scores over
one 1000 ms lap — frequencies and timings read out of loop_gen_all.

Colours follow gen_perf_dim.py: blue = pure tones / ring, amber = the
square beep, violet = the bell's partials, gray = the chunk ticks.

Run:  docs/.venv/bin/python docs/tools/gen_perf_audio.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
AUDIO_C = os.path.join(ROOT, "components", "ui", "alarm_audio.c")
AUDIO_H = os.path.join(ROOT, "components", "ui", "alarm_audio.h")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_audio.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # pure tones / the ring
AMBER = "#b07d10"    # the square beep
VIOLET = "#7c3aed"   # the bell's partials / the preview
GRAY = "#787878"     # the chunk writes (the stream's metronome)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def c_define(path: str, symbol: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.basename(path)}")
    return int(m.group(1))


def c_floats(path: str, pattern: str) -> list[float]:
    """One ``{a, b, ...}`` array of doubles, parsed straight out of the
    bake code (loop_gen_all)."""
    m = re.search(pattern, _read(path))
    if not m:
        raise SystemExit(f"array not found: {pattern[:48]}...")
    return [float(s) for s in m.group(1).split(",")]


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    rate = c_define(AUDIO_C, "RING_SAMPLE_RATE")
    loop_ms = c_define(AUDIO_C, "LOOP_MS")
    chunk = c_define(AUDIO_C, "CHUNK_SAMPLES")
    ring_full = c_define(AUDIO_C, "RING_VOL_FULL")
    ring_ramp = c_define(AUDIO_C, "RING_RAMP_MS")
    prev_full = c_define(AUDIO_C, "PREV_VOL_FULL")
    prev_ramp = c_define(AUDIO_C, "PREV_RAMP_MS")
    fade_step_ms = c_define(AUDIO_C, "RING_FADE_STEP_MS")
    count = c_define(AUDIO_H, "ALARM_SOUND_COUNT")
    m = re.search(r"for \(int vol = last_vol; vol > 0; vol -= (\d+)\)",
                  _read(AUDIO_C))
    if not m:
        raise SystemExit("fade-loop step not found in alarm_audio.c")
    fade_step = int(m.group(1))
    names = re.findall(r'"([^"]+)"', re.search(
        r"ALARM_NAMES\[ALARM_SOUND_COUNT\] = \{([^}]+)\}",
        _read(AUDIO_C)).group(1))
    if len(names) != count:
        raise SystemExit(f"ALARM_NAMES has {len(names)}, ALARM_SOUND_COUNT is {count}")

    # The two DERIVED pacing constants — divisions of parsed values.
    chunk_ms = chunk / rate * 1000.0          # ~46.44 ms per blocking write
    level_ms = ring_ramp / ring_full          # ~33.3 ms per integer level
    fade_n = ring_full // fade_step           # 15 delays -> 150 ms total
    psram_kib = count * (rate * loop_ms // 1000) * 2 // 1024

    # ---- (a) the volume timeline ---------------------------------------
    fig, (ax, axb) = plt.subplots(1, 2, figsize=(11.6, 4.4),
                                  gridspec_kw={"width_ratios": [1.5, 1]})

    def ramp(x0, dur, p1, color, label, dot_every=None):
        ax.plot([x0, x0 + dur], [0, p1], "-", color=color, linewidth=2.6,
                zorder=3)
        if dot_every:
            xd = 0.0
            while xd <= dur:
                ax.plot([x0 + xd], [p1 * xd / dur], ".", color=GRAY,
                        markersize=2.6, alpha=0.85, zorder=4)
                xd += dot_every
        ax.text(x0 + dur / 2, p1 / 2 - 9, label, fontsize=5.9, color=MUTED,
                ha="center", va="top", zorder=5)

    def fade(x0, p0, color):
        # exact staircase: fade_step of volume every fade_step_ms ms, then 0
        xs, ys = [x0], [p0]
        t = x0
        for v in range(p0, 0, -fade_step):
            xs += [t, t + fade_step_ms]
            ys += [v, v]
            t += fade_step_ms
        xs.append(t)
        ys.append(0)
        ax.plot(xs, ys, "-", color=color, linewidth=2.6, zorder=3)
        return t

    x = 0.0
    ax.plot([0, 0], [0, 0], "o", color=GRAY, markersize=4, zorder=4)
    ax.text(0, ring_full / 2 - 9, "codec open\nvol 0", fontsize=5.9,
            color=MUTED, ha="left", va="top")
    ramp(x, ring_ramp, ring_full, BLUE,
         f"ring ramp 0 → {ring_full} in {ring_ramp} ms\n(volume = wall clock × "
         f"{ring_full}/{ring_ramp}; new level ~every\n{level_ms:.0f} ms)",
         dot_every=chunk_ms)
    x += ring_ramp
    ax.plot([x, x + 1500], [ring_full, ring_full], "--", color=BLUE,
            linewidth=2.2, zorder=3)
    ax.text(x + 750, ring_full + 3,
            f"ring loops until touch (schematic)\n— one {loop_ms} ms PSRAM lap "
            f"per pass, {chunk} samples per\nblocking write ≈ {chunk_ms:.1f} ms",
            fontsize=5.9, color=MUTED, ha="center", va="bottom")
    x += 1500
    x = fade(x, ring_full, BLUE)
    ax.text(x + 8, 40, f"stop fade:\n−{fade_step} every {fade_step_ms} ms\n"
            f"= {fade_n} steps ≈ {fade_n * fade_step_ms} ms,\nthen close",
            fontsize=5.9, color=MUTED, ha="left", va="center")
    x += 700

    # the preview: the same machine, parsed 300 ms ramp, ONE pass
    ramp(x, prev_ramp, prev_full, VIOLET,
         f"preview ramp {prev_ramp} ms", dot_every=None)
    x += prev_ramp
    ax.plot([x, x + loop_ms], [prev_full, prev_full], "-", color=VIOLET,
            linewidth=2.6, zorder=3)
    ax.text(x + loop_ms / 2, prev_full + 3,
            f"exactly ONE {loop_ms} ms pass\n(looping is the ring's only extra)",
            fontsize=5.9, color=MUTED, ha="center", va="bottom")
    x += loop_ms
    x = fade(x, prev_full, VIOLET)

    ax.set_xlim(-80, x + 480)
    ax.set_ylim(-10, 108)
    ax.set_yticks([0, prev_full, 100])
    ax.set_yticklabels(["0  (−50 dB)", f"{ring_full}  (−12.5 dB)",
                        "100  (0 dB)"], fontsize=6.5)
    ax.set_xlabel("real ms — gray dots: the chunk boundaries of the "
                  f"BLOCKING esp_codec_dev_write\n({chunk} samples ÷ {rate} Hz "
                  f"= {chunk_ms:.1f} ms; the write paces the stream, so the "
                  "ramp needs no timer)")
    ax.set_ylabel("ES8311 out volume (0..100; dB: the RING_VOL_FULL "
                  "comment's own curve)")
    ax.set_title("the ring chain: one player task, two flows — the volume is "
                 "COMPUTED from wall time,\nthe blocking I2S writes are the "
                 "metronome (and the fade rides on the drained DMA, streaming "
                 "silence)")
    ax.grid(axis="y", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    # ---- (b) the four loops, parsed from loop_gen_all -------------------
    two = c_floats(AUDIO_C, r"const double freqs\[2\] = \{([^}]+)\}")
    beep_hz = float(re.search(r"2\.0 \* M_PI \* ([\d.]+) / \(double\)RING_SAMPLE_RATE",
                              _read(AUDIO_C)).group(1))
    burst_ms = int(re.search(r"const int burst = RING_SAMPLE_RATE \* (\d+) / 10",
                             _read(AUDIO_C)).group(1)) * 100
    bell_f = c_floats(AUDIO_C, r"const double f\[4\] = \{([^}]+)\}")
    bell_a = c_floats(AUDIO_C, r"const double a\[4\] = \{([^}]+)\}")
    bell_tau = float(re.search(r"const double tau = ([\d.]+)", _read(AUDIO_C)).group(1))
    arp = c_floats(AUDIO_C, r"const double freqs\[4\] = \{([^}]+)\}")

    def hz(v):
        return f"{v:g}" if v == int(v) else f"{v:.1f}"

    lanes = {names[0]: 0, names[1]: 1, names[2]: 2, names[3]: 3}
    seg = loop_ms / 4
    for h in range(4):  # two-tone: four 250 ms segments, alternating
        f = two[h & 1]
        axb.barh(lanes[names[0]], seg, left=h * seg, height=0.55,
                 color=BLUE, alpha=0.75, edgecolor=BLUE)
        axb.text(h * seg + seg / 2, lanes[names[0]], f"{hz(f)}", ha="center",
                 va="center", fontsize=5.8, color=INK)
    for h in range(3):  # beep: three bursts + equal gaps
        axb.barh(lanes[names[1]], burst_ms, left=h * 2 * burst_ms, height=0.55,
                 color=AMBER, alpha=0.75, edgecolor=AMBER)
    axb.text(burst_ms * 5, lanes[names[1]],
             f"square {hz(beep_hz)} Hz, {burst_ms} ms on / {burst_ms} ms off",
             ha="center", va="center", fontsize=5.8, color=MUTED)
    for p, (f, a) in enumerate(zip(bell_f, bell_a)):  # bell: 4 decaying partials
        axb.barh(lanes[names[2]] + (p - 1.5) * 0.19, loop_ms, height=0.14,
                 color=VIOLET, alpha=0.25 + 0.65 * a, edgecolor=VIOLET,
                 linewidth=0.5)
        axb.text(6, lanes[names[2]] + (p - 1.5) * 0.19,
                 f"{hz(f)} × {a:g}", ha="left", va="center", fontsize=5.2,
                 color=INK)
    axb.text(loop_ms - 6, lanes[names[2]],
             f"exp decay τ = {bell_tau * 1000:.0f} ms", ha="right",
             va="center", fontsize=5.8, color=MUTED)
    for h, f in enumerate(arp):  # arpeggio: four 250 ms segments
        axb.barh(lanes[names[3]], seg, left=h * seg, height=0.55,
                 color=BLUE, alpha=0.45, edgecolor=BLUE)
        axb.text(h * seg + seg / 2, lanes[names[3]], f"{hz(f)}", ha="center",
                 va="center", fontsize=5.8, color=INK)

    axb.set_yticks(list(lanes.values()),
                   [f"{n}" for n in names], fontsize=6.8)
    axb.invert_yaxis()
    axb.set_ylim(3.6, -0.6)
    axb.set_xlim(0, loop_ms)
    axb.set_xticks([0, 250, 500, 750, 1000])
    axb.set_xlabel(f"one {loop_ms} ms lap (LOOP_MS), ms — frequencies and "
                   f"timings parsed from loop_gen_all;\nbaked ONCE at init, "
                   f"{count} loops × {rate * loop_ms // 1000} samples × 2 B "
                   f"= {psram_kib} KiB PSRAM")
    axb.set_title("the four baked loops as scores (Hz labels are the parsed\n"
                  "arrays; the whole 1 s passes the player streams verbatim)")
    axb.grid(axis="x", color=HAIR, linewidth=0.6)
    axb.set_axisbelow(True)
    axb.spines["top"].set_visible(False)
    axb.spines["right"].set_visible(False)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(rate={rate} chunk_ms={chunk_ms:.2f} ramp={ring_ramp}/{ring_full} "
          f"prev={prev_ramp}/{prev_full} fade={fade_step}/{fade_step_ms}ms "
          f"names={names} psram={psram_kib}KiB)")


if __name__ == "__main__":
    main()
