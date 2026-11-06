"""Generate the explanatory sketches for book Chapter 6 (the lighting model).

Five figures, one per formula the chapter derives, saved to
docs/book/_static/figs/:

  ortho_view.png        the orthographic pixel -> direction mapping (u, v, w, r_hat)
  terminator_ridge.png  why the flat model fails at the terrain (the jagged boundary)
  displaced_normal.png  the displaced-normal construction N = (1+s) r_hat - G
  earthshine.png        the fitted earthshine cubic (the device constants)
  tone_lut.png          the real 256-entry tone LUT (assets/images/moon/tone_lut.bin)

Run:  docs/book/.venv/bin/python docs/book/tools/gen_lighting_figs.py
(needs matplotlib in the book venv)
"""
from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.abspath(os.path.join(HERE, "..", "_static", "figs"))

# --- house style -----------------------------------------------------------
INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
AMBER = "#b07d10"   # lit / the light's role
BLUE = "#2b5fb0"    # camera / geometry
RED = "#a63d2a"     # the flat model's error
GREEN = "#3e7d4f"   # the true (terrain) model

plt.rcParams.update({
    "font.size": 9.5,
    "font.family": "DejaVu Sans",
    "text.color": INK,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.linewidth": 0.8,
    "lines.linewidth": 1.8,
    "figure.dpi": 150,
    "savefig.dpi": 150,
})


def arrow(ax, p0, p1, color, lw=2.0, ls="-", mut=14, zorder=5, alpha=1.0):
    # head_width/head_length are FRACTIONS of the arrow length (arrowstyle
    # units), not points — keep them small or the head swallows the shaft.
    ax.annotate(
        "", xy=p1, xytext=p0,
        arrowprops=dict(arrowstyle="-|>,head_width=.32,head_length=.18",
                        color=color, lw=lw, linestyle=ls,
                        mutation_scale=mut, alpha=alpha),
        zorder=zorder,
    )


def vec_label(ax, p, s, color=INK, fs=10.5, offset=(3, 3)):
    ax.text(p[0] + offset[0] / 100, p[1] + offset[1] / 100, s,
            color=color, fontsize=fs, zorder=7)


def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", path)


# --- 1. the orthographic view ---------------------------------------------
def fig_ortho():
    fig, (a1, a2) = plt.subplots(
        1, 2, figsize=(7.2, 3.9), gridspec_kw={"width_ratios": [1, 1.15]})

    # (a) front view: pixel (u,v) -> r_hat = u*right + v*up + w*E
    a1.set_xlim(-1.35, 1.35)
    a1.set_ylim(-1.62, 1.5)
    a1.set_aspect("equal")
    a1.add_patch(plt.Circle((0, 0), 1.0, fill=False, ec=INK, lw=1.6))
    a1.axhline(0, color=HAIR, lw=0.8)
    a1.axvline(0, color=HAIR, lw=0.8)
    for t in (0.5, -0.5):
        a1.text(t, -0.10, f"{t:+.1f}", color=MUTED, fontsize=7.5, ha="center")
        a1.text(-0.10, t, f"{t:+.1f}", color=MUTED, fontsize=7.5, ha="right", va="center")
    u, v = 0.55, 0.38
    P = (u, v)
    arrow(a1, (0, 0), (u, 0), BLUE, lw=1.5, ls="--")   # u component
    arrow(a1, (u, 0), P, BLUE, lw=1.5, ls="--")        # v component
    arrow(a1, (0, 0), P, INK, lw=2.4)                  # r_hat
    # w*E points at the viewer: out-of-page symbol (circle + dot) at the tip
    a1.add_patch(plt.Circle(P, 0.05, fc="white", ec=INK, lw=1.2, zorder=6))
    a1.plot([P[0]], [P[1]], marker=".", ms=6, color=INK, zorder=7)
    a1.text(0.14, 0.44, r"$\hat{\mathbf{r}}$", color=INK, fontsize=10.5)
    a1.text(0.26, -0.10, r"$u$", color=BLUE, fontsize=10)
    a1.text(0.63, 0.17, r"$v$", color=BLUE, fontsize=10)
    a1.text(-1.33, 1.42, r"$\odot$  $= \;w\,\mathbf{E}$: out of the page, toward the viewer",
            color=INK, fontsize=8.4)
    a1.set_title("(a) the pixel's screen offset builds the base direction",
                 fontsize=9.5, pad=6)
    a1.text(0.0, -1.30, r"$\hat{\mathbf{r}} = u\,\hat{\mathbf{right}} + v\,\hat{\mathbf{up}} + w\,\mathbf{E}$,    "
                        r"$w = \sqrt{1 - u^2 - v^2}$", color=MUTED, fontsize=8.6, ha="center")
    a1.text(0.0, -1.50, r"$u^2 + v^2 > 1 \;\Rightarrow\;$  off disk, black",
            color=MUTED, fontsize=8.4, ha="center")

    # (b) side profile: the w component = how deep the pixel sits on the sphere
    a2.set_xlim(-1.45, 1.45)
    a2.set_ylim(-1.82, 1.5)
    a2.set_aspect("equal")
    a2.add_patch(plt.Circle((0, 0), 1.0, fill=False, ec=INK, lw=1.6))
    phi = np.deg2rad(52.0)
    P2 = (np.sin(phi), -np.cos(phi))          # pixel on the lower-right of the disk
    arrow(a2, (0, 0), P2, INK, lw=2.4)
    Q2 = (0.0, -np.cos(phi))                  # foot on the line of sight
    arrow(a2, (0, 0), Q2, BLUE, lw=1.6, ls="--")
    a2.plot([Q2[0]], [Q2[1]], "o", ms=3.5, color=BLUE, zorder=6)
    a2.plot([P2[0]], [P2[1]], "o", ms=5, color=INK, zorder=6)
    # right-angle mark at Q (the w vector meets the line of sight at 90 deg)
    a2.add_patch(mpatches.Rectangle(Q2, 0.07, 0.07, fill=False, ec=MUTED, lw=0.9, zorder=5))
    a2.add_patch(mpatches.Arc((0, 0), 0.5, 0.5, theta1=-90, theta2=-90 + 52,
                              color=AMBER, lw=1.6))
    a2.text(0.17, -0.20, r"$\varphi$", color=AMBER, fontsize=10.5)
    a2.text(-0.07, -0.35, r"$w$", color=BLUE, fontsize=10, ha="right")
    a2.text(0.34, -0.58, r"$\hat{\mathbf{r}}$ (in the page plane)", color=INK, fontsize=8.6)
    a2.text(0.12, -1.47, "camera at infinity (384,400 km)", color=MUTED, fontsize=8.6)
    a2.annotate("", xy=(0.0, -1.15), xytext=(0.0, -1.36),
                arrowprops=dict(arrowstyle="-|>,head_width=.3,head_length=.3",
                                color=MUTED, lw=1.0, mutation_scale=12))
    a2.set_title("(b) why the disk edges curve toward the viewer",
                 fontsize=9.5, pad=6)
    a2.text(0.0, -1.74,
            r"the pixel sits off the line of sight by $\varphi$; the $w = \cos\varphi$ "
            r"term pulls $\hat{\mathbf{r}}$ back toward the camera as the pixel moves to the limb",
            color=MUTED, fontsize=8.4, ha="center")
    for a in (a1, a2):
        a.set_xticks([])
        a.set_yticks([])
        for s in a.spines.values():
            s.set_visible(False)
    fig.tight_layout(w_pad=3.0)
    save(fig, "ortho_view.png")


# --- 2. why the flat model fails at the terrain ----------------------------
def fig_terminator():
    fig, ax = plt.subplots(figsize=(6.9, 4.35))
    ax.set_xlim(-1.32, 1.52)
    ax.set_ylim(-1.32, 1.42)
    ax.set_aspect("equal")
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_xticks([])
    ax.set_yticks([])

    # base sphere (upper half is the one with terrain; mirror note below)
    ax.add_patch(plt.Circle((0, 0), 1.0, fill=False, ec=INK, lw=1.8))

    # terrain: a ridge at the terminator (bases 80/100 deg, top at 90 deg,
    # r=1.06) and a notch on the night side (rims 130/150 deg, bottom 140 deg,
    # r=0.88). Sun to the right; rays travel horizontally left.
    d2r = np.deg2rad
    B80 = (np.cos(d2r(80)), np.sin(d2r(80)))
    T = (1.06 * np.cos(d2r(90)), 1.06 * np.sin(d2r(90)))          # (0, 1.06)
    B100 = (np.cos(d2r(100)), np.sin(d2r(100)))
    R1 = (np.cos(d2r(130)), np.sin(d2r(130)))
    BOT = (0.88 * np.cos(d2r(140)), 0.88 * np.sin(d2r(140)))
    R2 = (np.cos(d2r(150)), np.sin(d2r(150)))

    # surface profile (upper half) with terrain
    th = np.linspace(0, 180, 600)
    prof = []
    for t in th:
        r = 1.0
        if 80 <= t <= 100:
            r = 1.0 + 0.06 * min((t - 80) / 10.0, (100 - t) / 10.0, 1.0)
        elif 130 <= t <= 150:
            r = 1.0 - 0.12 * min((t - 130) / 10.0, (150 - t) / 10.0, 1.0)
        prof.append((r * np.cos(d2r(t)), r * np.sin(d2r(t))))
    prof = np.array(prof)
    ax.plot(prof[:, 0], prof[:, 1], color=INK, lw=2.0, zorder=4)
    # fill the ridge and notch against the base circle for legibility
    ax.fill_between(prof[:, 0], prof[:, 1],
                    np.sin(np.arctan2(prof[:, 1], prof[:, 0])) * 1.0,
                    where=(prof[:, 1] > 0), color=HAIR, zorder=3)

    # the flat model's terminator: the straight line d = 0 (x = 0)
    ax.plot([0, 0], [-1.28, 1.30], color=BLUE, ls="--", lw=1.4, zorder=3)
    ax.text(0.03, 1.335, "flat model:  d = 0  (a straight line)", color=BLUE, fontsize=9)

    # the cast shadow of the ridge: the bite between the circle (80-100 deg)
    # and the grazing-ray edge (B80 -> T) plus the shadowed left flank (T -> B100)
    bite = [B80]
    bite.append(T)  # grazing edge (straight)
    for t in np.linspace(90, 100, 20):
        r = 1.0 + 0.06 * (100 - t) / 10.0
        bite.append((r * np.cos(d2r(t)), r * np.sin(d2r(t))))
    bite += [B100]
    for t in np.linspace(80, 100, 40):
        bite.append((np.cos(d2r(t)), np.sin(d2r(t))))
    bite = np.array(bite)
    ax.fill(bite[:, 0], bite[:, 1], color=HAIR, hatch="///", ec=MUTED,
            lw=0.6, zorder=3, alpha=0.9)
    # the true boundary (red): the edge of that bite + the notch's lit wall
    ax.plot([B80[0], T[0]], [B80[1], T[1]], color=RED, lw=2.6, zorder=5)
    flank_l = [((1.0 + 0.06 * (100 - t) / 10.0) * np.cos(d2r(t)),
                (1.0 + 0.06 * (100 - t) / 10.0) * np.sin(d2r(t)))
               for t in np.linspace(90, 100, 20)]
    ax.plot([T[0]] + [p[0] for p in flank_l], [T[1]] + [p[1] for p in flank_l],
            color=RED, lw=2.6, zorder=5)
    ax.plot([B80[0], B100[0]], [B80[1], B100[1]], color=RED, lw=1.0,
            alpha=0.5, zorder=4)
    # the notch's sunlit right wall (lit patch on the night side)
    wall = [( (1.0 - 0.12 * (150 - t) / 10.0) * np.cos(d2r(t)),
              (1.0 - 0.12 * (150 - t) / 10.0) * np.sin(d2r(t)))
            for t in np.linspace(150, 140, 15)]
    ax.plot([p[0] for p in wall], [p[1] for p in wall], color=AMBER,
            lw=3.0, zorder=5, solid_capstyle="round")
    ax.text(-1.30, 0.30, "true boundary:\njagged, hugs the terrain",
            color=RED, fontsize=9, va="center")

    # sun on the right: horizontal rays travelling left
    for y in (0.80, 0.62):
        ax.annotate("", xy=(1.46 - 0.20, y), xytext=(1.46, y),
                    arrowprops=dict(arrowstyle="-|>,head_width=.5,head_length=.8",
                                    color=MUTED, lw=1.1, alpha=0.85))
    ax.text(1.46, 1.10, "Sun  (S $\\to$ right)", color=MUTED, fontsize=9, ha="right", va="top")
    # the ray blocked by the ridge (ends at the left flank)
    ax.annotate("", xy=(0.15, 0.996), xytext=(1.46, 0.996),
                arrowprops=dict(arrowstyle="-|>,head_width=.45,head_length=.7",
                                color=MUTED, lw=1.2, alpha=0.9))

    # counter-example M: the day side of the flat line, inside the cast shadow
    M = (np.cos(d2r(85)), np.sin(d2r(85)))
    ax.plot(*M, "o", ms=5, color=INK, zorder=6)
    ax.text(M[0] + 0.05, M[1] - 0.06, "M", color=RED, fontsize=10, fontweight="bold")

    # counter-example N: the notch's sunlit inner wall (night side)
    tN = 145.0
    rN = 1.0 - 0.12 * (150 - tN) / 10.0
    N = (rN * np.cos(d2r(tN)), rN * np.sin(d2r(tN)))
    ax.plot(*N, "o", ms=5, color=INK, zorder=6)
    ax.text(N[0] - 0.10, N[1] - 0.08, "N", color=AMBER, fontsize=10, fontweight="bold")
    # the ray that lights the notch wall
    ax.annotate("", xy=(N[0] + 0.03, N[1] + 0.02), xytext=(1.46, N[1]),
                arrowprops=dict(arrowstyle="-|>,head_width=.45,head_length=.7",
                                color=AMBER, lw=1.3, alpha=0.9, linestyle=(0, (4, 2))))

    # legend strip in the (empty) mirror half: what M and N actually see
    ax.plot([M[0], 0.15], [M[1] - 0.16, -0.36], color=RED, lw=0.7, ls=":", alpha=0.7)
    ax.plot([N[0], 0.15], [N[1] - 0.16, -0.76], color=AMBER, lw=0.7, ls=":", alpha=0.7)
    ax.text(0.19, -0.34,
            "M:  day side of the flat line, but inside the ridge's cast shadow\n"
            r"     radial  $d = \cos 85° \approx +0.09$  →  flat model: lit,  true surface: dark",
            color=RED, fontsize=8.4, va="top")
    ax.text(0.19, -0.74,
            "N:  night side of the flat line, but the inner wall turns toward the Sun\n"
            r"     radial  $d = \cos 145° \approx -0.82$  →  flat model: black,  true surface: lit",
            color=AMBER, fontsize=8.4, va="top")

    ax.text(0.0, -1.27, "cross-section in the plane containing S — the lower half is the mirror",
            color=MUTED, fontsize=8.3, ha="center")
    fig.tight_layout()
    save(fig, "terminator_ridge.png")


# --- 3. the displaced-normal construction ----------------------------------
def fig_displaced():
    fig, (a1, a2) = plt.subplots(
        1, 2, figsize=(6.9, 3.0), gridspec_kw={"width_ratios": [1.2, 1]})

    # (a) a point on a flank that slopes DOWN to the right: the face turns
    # toward the grazing Sun. The flat model takes the radial (straight-up)
    # normal and reads "barely lit"; the true normal, tilted by the slope,
    # reads "lit" — the ridge-shadow contrast the flat model cannot see.
    a1.set_xlim(-1.4, 1.4)
    a1.set_ylim(-1.4, 1.75)
    a1.set_aspect("equal")
    for s in a1.spines.values():
        s.set_visible(False)
    a1.set_xticks([])
    a1.set_yticks([])
    P = np.array([0.0, 0.15])
    beta = np.deg2rad(9.0)                      # local slope, falling to the right
    N = np.array([np.sin(beta), np.cos(beta)])  # true normal (toward the Sun side)
    Rc = 3.2
    Cc = P - Rc * N                             # centre of the surface arc
    aP = np.arctan2(P[1] - Cc[1], P[0] - Cc[0])
    arc_th = np.linspace(aP - np.deg2rad(55), aP + np.deg2rad(55), 120)
    ax_s = np.column_stack([Cc[0] + Rc * np.cos(arc_th),
                            Cc[1] + Rc * np.sin(arc_th)])
    a1.plot(ax_s[:, 0], ax_s[:, 1], color=INK, lw=2.0, zorder=4)
    # tangent guide at P (dotted)
    t = np.array([np.cos(beta), -np.sin(beta)])
    a1.plot([P[0] - 0.55 * t[0], P[0] + 0.55 * t[0]],
            [P[1] - 0.55 * t[1], P[1] + 0.55 * t[1]],
            color=MUTED, lw=0.9, ls=":", zorder=2)
    # radial (flat-model) normal: straight up
    arrow(a1, P, P + np.array([0.0, 0.58]), MUTED, lw=1.5, ls="--")
    a1.text(0.06, 0.62, r"$\hat{\mathbf{r}}$  (flat model)", color=MUTED, fontsize=9)
    # true normal: tilted by the slope, toward the Sun
    arrow(a1, P, P + 0.62 * N, INK, lw=2.4)
    a1.text(0.12, 0.74, r"$\mathbf{N}$ (the displaced normal)", color=INK, fontsize=9)
    # gradient G: along the tangent, uphill (to the left)
    arrow(a1, P, P + 0.44 * (-t), BLUE, lw=1.8)
    a1.text(-0.52, 0.30, r"$\mathbf{G}$ (uphill)", color=BLUE, fontsize=9)
    # the Sun: to the right, 4 degrees above horizontal (grazing)
    Sd = np.array([np.sin(np.deg2rad(86.0)), np.cos(np.deg2rad(86.0))])  # toward the Sun
    d = -Sd                                                            # ray travel
    perp = np.array([-d[1], d[0]])
    for o in (-0.30, 0.0, 0.30):
        a1.annotate("", xy=P + 0.22 * d + o * perp, xytext=P + 1.25 * d + o * perp,
                    arrowprops=dict(arrowstyle="-|>,head_width=.45,head_length=.7",
                                    color=AMBER, lw=1.2, alpha=0.9))
    a1.text(1.34, -0.55, "Sun 4° above\nthe horizon (grazing)",
            color=AMBER, fontsize=8.4, ha="right")
    # the two answers
    a1.text(0.07, 1.28, r"$d = \hat{\mathbf{r}}\cdot S = \cos 86° \approx 0.07$ — barely lit",
            color=MUTED, fontsize=8.8)
    a1.text(-1.39, 1.58, r"$\mathit{lns} = \mathbf{N}\cdot S = \cos 77° \approx 0.22$ — lit (3× brighter)",
            color=INK, fontsize=8.8, fontweight="bold")

    # (b) the small-slope form as a ratio
    a2.set_xlim(0, 1)
    a2.set_ylim(0, 1)
    a2.axis("off")
    a2.text(0.5, 0.86, "worked out, no normalization needed", fontsize=9.5,
            ha="center", fontweight="bold")
    a2.text(0.5, 0.66, r"$\mathbf{N}\cdot S = \dfrac{\hat{\mathbf{r}}\cdot S - \mathbf{G}\cdot S}{\sqrt{1 + \|\mathbf{G}\|^2}}$",
            fontsize=13, ha="center")
    a2.text(0.5, 0.44, r"$=\ \dfrac{d\;-\;(\mathrm{glon}\cdot\mathrm{east}_S + \mathrm{glat}\cdot\mathrm{north}_S)}{\sqrt{1 + \mathrm{glon}^2 + \mathrm{glat}^2}}$",
            fontsize=9.5, ha="center", color=MUTED)
    a2.text(0.5, 0.27, "the Moon's slopes are ≪ 0.3 rad, so the", fontsize=9, ha="center", color=MUTED)
    a2.text(0.5, 0.19, "small-slope form is accurate to under 0.5 %", fontsize=9, ha="center", color=MUTED)
    a2.text(0.5, 0.07, "lit side:  L = clip(lns, 0, 1)      dark side:  clip(−lns, 0, 1)  (Ch. 7)",
            fontsize=9, ha="center", color=INK)
    fig.tight_layout(w_pad=3.0)
    save(fig, "displaced_normal.png")


# --- 4. earthshine ----------------------------------------------------------
def fig_earthshine():
    fig, ax = plt.subplots(figsize=(5.6, 3.3))
    A, B, C = 30.455, -11.733, -2.615   # moon_consts.h
    x = np.linspace(0, 1, 200)
    y = A * x + B * x * x + C * x * x * x
    ax.fill_between(x, y, color=AMBER, alpha=0.18)
    ax.plot(x, y, color=AMBER, lw=2.2)
    ax.plot([1], [A + B + C], "o", ms=5, color=AMBER)
    ax.annotate(f"peak ≈ {A + B + C:.0f} grey levels\n(at x = 1, the dark-side centre)",
                xy=(1.0, A + B + C), xytext=(0.52, 22.5),
                fontsize=9, color=INK,
                arrowprops=dict(arrowstyle="->", color=MUTED, lw=0.9))
    ax.text(0.01, 26.0, r"$\mathrm{earthshine}(d) = 30.455\,x - 11.733\,x^2 - 2.615\,x^3$,   $x = \max(0,-d)$",
            fontsize=9.5, ha="left")
    ax.set_xlim(0, 1.02)
    ax.set_ylim(0, 28)
    ax.set_xlabel("x = max(0, −d):  0 at the terminator  →  1 at the dark-side centre", fontsize=8.8)
    ax.set_ylabel("brightness, 8-bit (0–255)", fontsize=8.8)
    ax.grid(axis="y", color=HAIR, lw=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_title("the validated dark-side gradient (dark_zero = 0)", fontsize=9.5)
    fig.tight_layout()
    save(fig, "earthshine.png")


# --- 5. the tone LUT --------------------------------------------------------
def fig_tonlut():
    lut = np.fromfile(os.path.join(REPO, "assets", "images", "moon", "tone_lut.bin"), dtype=np.uint8)
    fig, ax = plt.subplots(figsize=(5.6, 3.3))
    idx = np.arange(len(lut))
    ax.plot(idx, lut, color=INK, lw=1.8)
    ax.plot([0], [lut[0]], "o", ms=5, color=BLUE)
    ax.annotate("bin 0 = 0:\nno sunlight → the LUT adds nothing\n(only earthshine remains)",
                xy=(0, 0), xytext=(22, 60), fontsize=8.8, color=BLUE,
                arrowprops=dict(arrowstyle="->", color=BLUE, lw=0.9))
    # highlight rolloff: find the last index where the curve is strictly rising
    rise = np.where(np.diff(lut) > 0)[0]
    k = int(rise[-1]) if len(rise) else 220
    ax.plot([k, 255], [lut[k], lut[-1]], color=RED, lw=2.4)
    ax.annotate("tail is not monotonic past ~220:\nthe brightest floors/mare compress\nand can dip rather than pin at 255",
                xy=(k, lut[k]), xytext=(128, 178), fontsize=8.8, color=RED,
                arrowprops=dict(arrowstyle="->", color=RED, lw=0.9))
    ax.set_xlim(0, 255)
    ax.set_ylim(0, 255)
    ax.set_xlabel("raw index = round(albedo × L)   (0–255)", fontsize=8.8)
    ax.set_ylabel("display brightness, 8-bit", fontsize=8.8)
    ax.grid(color=HAIR, lw=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_title("the fitted 256-entry tone LUT (tone_lut.bin, as shipped)", fontsize=9.5)
    fig.tight_layout()
    save(fig, "tone_lut.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    fig_ortho()
    fig_terminator()
    fig_displaced()
    fig_earthshine()
    fig_tonlut()
