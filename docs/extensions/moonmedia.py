"""moonmedia — the ``media`` builder + screenshot staleness checks.

Two jobs:

* a ``media`` Sphinx builder — ``sphinx-build -b media`` regenerates the
  site's ENTIRE binary media set, all of it gitignored build output
  (2026-11-16: untracked from git + purged from history — the generators
  and the uisim are the sources of truth, never the pixels). It runs the
  scripted figures (every ``docs/tools/gen_*.py`` + the node isomorphic
  pipeline: ``gen_figures.js`` → ``assemble_figures.py``,
  ``gen_bake_intermediates.js`` → ``gen_bake_strip.py``; with
  ``MEDIA_NASA=1`` the NASA answer-key art: gate run → ``nasa_figs.py``
  → ``gen_nasa_quarters.py``, needs the gitignored SVS 5415 key), the
  uisim screenshots for ALL THREE boards (``host_sim/uisim/build.sh run``
  + ``--panel 720`` + ``--panel 360``, the ``--wheel`` capture flow for
  the password keyboard on every panel, the S3's ``--tour`` promo
  frames — encoded into promo.mp4 by ``docs/gen_media.py``, which
  composites every frame into the round product case), and the
  knob's ``--dial`` dial-cursor shot), runs ``docs/tools/enclose.py`` over every
  ``ui/labels/*.json`` spec for both panels (a ``<screen>-p4.json``
  override replaces the shared spec on the 720 pass when present) —
  the annotated callouts
  rendered inside the round product housing, alpha outside —
  ``annotate.py`` still holds the badge/leader drawing primitives it
  imports), rebuilds the device composites
  (``docs/tools/composite_dark.py``), and — with ``MEDIA_VIDEOS=1``
  — the mp4 set (``docs/gen_media.py``). The builder performs ONLY these
  side effects; it reads no doctree and writes no HTML, so a media run
  is fast even on a cold tree.

* a ``builder-inited`` staleness check for the normal builds: every
  generated screenshot (``docs/img/screens{,/p4,/knob}/*.png``), annotated
  callout (``docs/img/screens/annot/*.png``), device composite
  (``docs/_static/photos/sim_*.png``) and film (``docs/img/*.mp4``,
  vs ``docs/gen_media.py`` + the uisim inputs) must EXIST and must not
  be OLDER
  than the tracked sources it was rendered from (the UI sources, the
  font dirs, the uisim build inputs, the per-board moon texture). The
  docs must not silently show a stale UI — a drifted source trips a
  warning at build time.

The two are the same pipeline viewed from two directions: the media
builder RUNS it, the staleness check REFUSES to let a stale output stand
until the builder is run.
"""

from __future__ import annotations

import glob
import os
import shlex
import subprocess
import sys
from pathlib import Path

from sphinx.builders import Builder
from sphinx.util import logging

logger = logging.getLogger(__name__)

# --- paths (the extension lives in docs/extensions/; repo root is one up) ---
_DOCS = Path(__file__).resolve().parent.parent      # docs/
ROOT = _DOCS.parent                                 # repo root
UISIM = ROOT / "host_sim" / "uisim"
OUT = UISIM / "out"
PY = sys.executable                                  # the docs venv python
HOSTPY = ROOT / "host_sim" / ".venv" / "bin" / "python"  # the gate venv

# The films, referenced by the pages via ``.. raw:: html`` ``<video>``
# (the fade by implementation/animations.rst, the promo tour by
# index.rst). Raw HTML never triggers Sphinx's image copy, so each is
# registered as a static file (landed under _static/img/) and the RST
# points at ``_static/img/`` — otherwise the built page's ``../img/``
# is empty and the video 404s. The boot-sweep, spin and dusk films
# retired 2026-11-13 with their subsystems.
VIDEOS = ["fade.mp4", "promo.mp4"]


# The 17 base screen shots uisim names them (the ScreensEnum, underscored).
BASE_SCREENS = [
    "modem_sleep", "wifi", "light_sleep", "deep_sleep", "alarm",
    "settings_hub", "settings_alarm", "settings_pomodoro",
    "settings_alarm_time", "settings_light_sleep", "settings_deep_sleep",
    "settings_wifi", "settings_wifi_password", "settings_time",
    "settings_tz", "settings_alarm_sound", "settings_display",
]
# The --wheel capture flow's wheel_<name>.png -> the committed docs name.
WHEEL_MAP = {
    "rest": "settings_wifi_password",
    "touched": "settings_wifi_password_touched",
    "ABC": "settings_wifi_password_ABC",
    "123": "settings_wifi_password_123",
    "sym": "settings_wifi_password_sym",
}

# The knob's raw uisim set (docs/img/screens/knob/): the 14 screens the
# knob REACHES (ui/index.rst reachability matrix) — only the light-/
# deep-sleep states and the P4-only manual-time screen are not captured
# (the knob dims its real backlight, deep-sleeps for real and has no
# manual time entry). The transient WiFi screen IS reachable there:
# the knob registers action_update_clock_display, so the hourly sync
# gate runs on it too, and the wheel keyboard's commit starts a sync
# unconditionally. Plus the wheel-keyboard rest shot (the committed
# base name) and its touched capture (the 360-px wheel-in-press view
# keyboard.rst embeds — the ABC/123/sym variants are annotation inputs
# for the two clock panels only, no knob page embeds them), and the
# dial-cursor hub shot (--dial capture mode).
KNOB_BASE = [s for s in BASE_SCREENS if s not in ("light_sleep", "deep_sleep", "settings_time")]
KNOB_EXTRA = ["settings_hub_dial"]


# --------------------------------------------------------------------------
# render inputs (the staleness check's "newer than" set)
# --------------------------------------------------------------------------

def _inputs() -> list[Path]:
    """The tracked sources a uisim screenshot or composite is rendered
    from. Any of these newer than a committed output = a stale output."""
    p: list[Path] = []
    for f in ("screens.c", "styles.c", "images.c", "ui_px.h"):
        fp = ROOT / "components" / "ui" / f
        if fp.exists():
            p.append(fp)
    # the font sets (all three boards; uisim compiles them in software)
    for fdir in ("components/ui/fonts", "components/ui/fonts_p4", "components/ui/fonts_knob"):
        d = ROOT / fdir
        if d.is_dir():
            p.extend(sorted(d.glob("*.c")))
    # the uisim build inputs (the scripted indev + the build driver)
    for f in ("main.c", "stubs.c", "stb_impl.c", "build.sh"):
        fp = UISIM / f
        if fp.exists():
            p.append(fp)
    # the uisim stub headers (the knob board blocks in the shared sources
    # compile against the host stand-ins: esp_log, esp_timer, FreeRTOS)
    p.extend(sorted((UISIM / "freertos").glob("*.h")))
    for f in ("esp_err.h", "esp_log.h", "esp_timer.h"):
        fp = UISIM / f
        if fp.exists():
            p.append(fp)
    # the moon core + host assets (the bake path the shots show)
    moon = ROOT / "components" / "moon"
    core = moon / "core"
    if core.is_dir():
        p.extend(sorted(core.glob("*.c")))
    for f in ("host/moon_host_assets.c",):
        fp = moon / f
        if fp.exists():
            p.append(fp)
    # the per-board moon texture gen C (the albedo/DEM the moon shows)
    for t in ("s3", "p4", "knob"):
        fp = ROOT / "assets" / "images" / "moon" / "out" / t / "moon_assets_gen.c"
        if fp.exists():
            p.append(fp)
    return p


def _mtime(path: Path) -> float:
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


# --------------------------------------------------------------------------
# staleness check (builder-inited)
# --------------------------------------------------------------------------

def _stale_check(app):
    """Warn on any generated media file that is missing or older than its
    render inputs — media are gitignored build outputs, so "missing" means
    ``-b media`` was never run on this tree. Skipped for the media builder
    (which regenerates)."""
    if app.builder.name == "media":
        return
    logger.info("moonmedia: checking screenshots for staleness", type="moonmedia")
    inputs = _inputs()
    if not inputs:
        return
    input_max = max(_mtime(x) for x in inputs)

    screens_dir = _DOCS / "img" / "screens"
    annot_dir = screens_dir / "annot"
    labels_dir = _DOCS / "ui" / "labels"
    photos_dir = _DOCS / "_static" / "photos"

    jobs: list[tuple[Path, list[Path]]] = []
    for sub in ("", "p4", "knob"):
        base = screens_dir / sub
        for png in sorted(base.glob("*.png")) if base.is_dir() else []:
            jobs.append((png, inputs))
    # annotated callouts: inputs + the matching label spec (the callout
    # positions are authored data, a distinct input from the raw shot) +
    # the enclosure pipeline (the tool, the shared drawing/glass code,
    # the hero photo + fit it composites into)
    tools = _DOCS / "tools"
    enclose_deps = [
        tools / "enclose.py", tools / "annotate.py", tools / "composite_panel.py",
        tools / "waveshare-hero.webp", tools / "fit.json",
        tools / "waveshare-p4-hero.webp", tools / "fit_p4.json",
    ]
    if annot_dir.is_dir():
        for png in sorted(annot_dir.glob("*.png")):
            stem = png.name.rsplit("_", 1)[0]
            spec = labels_dir / (stem + ".json")
            p4spec = labels_dir / (stem + "-p4.json")
            spec_deps = [p for p in (spec, p4spec) if p.exists()]
            jobs.append((png, inputs + spec_deps + enclose_deps))
    # device composites (the uisim-in-photo frames)
    if photos_dir.is_dir():
        for png in sorted(photos_dir.glob("sim_*.png")):
            jobs.append((png, inputs))
    # the committed films (docs/img/*.mp4): rendered by docs/gen_media.py
    # from the uisim frames — the same staleness rule the screenshots
    # live under. Regression: ISSUE-002 — found by /qa on 2026-11-15:
    # fade.mp4 predated the orientation-pipeline retirement and shipped
    # one end baked in sideways, with no gate noticing (its mtime sat
    # untouched since 2026-11-07 while its inputs moved on).
    gen_media = _DOCS / "gen_media.py"
    # The promo film composites every frame into the round product case
    # (the device glass + the dark backdrop) — composite_panel +
    # composite_dark, the hero photo + fit — so those are inputs too.
    film_deps = [
        gen_media, tools / "composite_panel.py", tools / "composite_dark.py",
        tools / "waveshare-hero.webp", tools / "fit.json",
    ]
    for name in VIDEOS:
        jobs.append((_DOCS / "img" / name, inputs + film_deps))

    problems: list[str] = []
    for out, deps in jobs:
        if not out.exists():
            problems.append(f"missing {out.relative_to(ROOT)}")
            continue
        if _mtime(out) < max(input_max, *(_mtime(d) for d in deps)):
            problems.append(f"stale {out.relative_to(ROOT)}")

    if problems:
        shown = problems[:12]
        more = len(problems) - len(shown)
        logger.warning(
            "moonmedia: %d docs media file(s) are missing or older than "
            "their render inputs — they are gitignored build outputs, run "
            "`sphinx-build -b media docs docs/_build/media` to regenerate "
            "(or `make -C docs media`)\n  %s%s",
            len(problems),
            "\n  ".join(shown),
            "\n  … and %d more" % more if more else "",
            type="moonmedia",
        )


# --------------------------------------------------------------------------
# the media pipeline (the side effects the builder performs)
# --------------------------------------------------------------------------

def _run(cmd: list[str], note: str) -> None:
    print(f"  moonmedia: {note}")
    subprocess.run([str(c) for c in cmd], check=True, cwd=str(ROOT))


def _copy(src: Path, dst: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"uisim did not produce {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(src.read_bytes())


def _copy_panel(panel_dir: Path) -> None:
    """Copy one panel's uisim shots from out/ into the docs tree.

    ``panel_dir`` is docs/img/screens (S3) or docs/img/screens/p4 (P4).
    The 17 base screens are copied by name (out/ also carries
    verification shots that must NOT land in the docs); the password
    keyboard's base shot is the ``--wheel rest`` capture (the prefilled
    SSID), not the plain bare screen."""
    for name in BASE_SCREENS:
        _copy(OUT / f"{name}.png", panel_dir / f"{name}.png")
    for wheel, doc in WHEEL_MAP.items():
        _copy(OUT / f"wheel_{wheel}.png", panel_dir / f"{doc}.png")


def _copy_knob_panel(panel_dir: Path) -> None:
    """The knob set: only the 14 REACHABLE base screens (never the
    light-/deep-sleep states or the P4-only manual-time screen — the
    knob dims its real backlight, deep-sleeps for real and has no
    manual time entry; the transient WiFi screen IS reachable), the
    wheel-keyboard rest shot (the --wheel capture's rest frame is the
    committed base name) plus the touched capture (keyboard.rst's
    360-px wheel-in-press view; the ABC/123/sym variants are S3/P4
    annotation inputs only — the knob set has no annotated views) and
    the dial-cursor hub shot."""
    for name in KNOB_BASE:
        _copy(OUT / f"{name}.png", panel_dir / f"{name}.png")
    _copy(OUT / "wheel_rest.png", panel_dir / "settings_wifi_password.png")
    _copy(OUT / "wheel_touched.png",
          panel_dir / "settings_wifi_password_touched.png")
    for name in KNOB_EXTRA:
        _copy(OUT / f"{name}.png", panel_dir / f"{name}.png")


def _annotate_all() -> None:
    labels = sorted((_DOCS / "ui" / "labels").glob("*.json"))
    annot = _DOCS / "img" / "screens" / "annot"
    annot.mkdir(parents=True, exist_ok=True)
    # *-p4.json stems are per-panel spec OVERRIDES, not screens — they
    # are consumed by the 720 pass below and must never spawn a phantom
    # screen of their own.
    labels = sorted(p for p in (_DOCS / "ui" / "labels").glob("*.json")
                    if not p.stem.endswith("-p4"))
    annot = _DOCS / "img" / "screens" / "annot"
    annot.mkdir(parents=True, exist_ok=True)
    for spec in labels:
        screen = spec.stem                       # e.g. "settings-hub"
        shot = screen.replace("-", "_")          # e.g. "settings_hub"
        for panel, tag in (("466", "s3"), ("720", "p4")):
            base = _DOCS / "img" / "screens" / ("" if tag == "s3" else "p4") / f"{shot}.png"
            out = annot / f"{screen}_{tag}.png"
            # a screen may author <screen>-p4.json (e.g. the 8-icon P4
            # hub ring) — the 720 pass prefers it over the shared spec.
            p4_spec = spec.with_name(spec.stem + "-p4.json")
            use = p4_spec if (tag == "p4" and p4_spec.exists()) else spec
            _run([PY, "docs/tools/enclose.py", base,
                  "--spec", str(use), "--panel", panel, "-o", out],
                 f"enclose {out.relative_to(ROOT)}")


# --------------------------------------------------------------------------
# scripted figures (docs/_static/figs + docs/img/nasa — all build outputs)
# --------------------------------------------------------------------------

# Scratch dirs pinned to the recipe in docs/science/interactive.rst
# (gen_bake_strip.py defaults to /tmp/bake_inter too).
BOOKFIGS = "/tmp/bookfigs"
BAKE_INTER = "/tmp/bake_inter"
# NOT part of the plain figs sweep: gen_moonjs.py regenerates the TRACKED
# docs/_static/moonjs/moonlab_data.js — a source INPUT here, run by hand
# when a C-core constant changed (interactive.rst step 1) — and needs the
# gate venv's astropy. gen_bake_strip.py has an explicit node producer
# step before it. gen_nasa_quarters.py belongs to the MEDIA_NASA slot.
FIGS_SKIP = {"gen_moonjs.py", "gen_nasa_quarters.py", "gen_bake_strip.py"}


def _regen_figs() -> None:
    """Regenerate every scripted figure the pages embed under
    docs/_static/figs — independent of the uisim frames, so it runs FIRST
    in the pipeline. Fail-loud: a broken generator must not silently leave
    yesterday's figure in place (the f8d8506 film lesson, applied to figs)."""
    # The isomorphic moon figures (art_ab/k_ab/tint_ab/phase_strip): the
    # same moonlab.js bake the reader-facing simulator runs — the
    # 3-step recipe of docs/science/interactive.rst, minus its step 1
    # (moonlab_data.js is tracked, regenerated by hand when the core moves).
    _run(["node", "docs/tools/gen_figures.js", BOOKFIGS], "gen_figures.js")
    _run([PY, "docs/tools/assemble_figures.py", BOOKFIGS], "assemble_figures.py")
    # The bake-pipeline intermediates figure (node raw-buffer dump ->
    # numpy labeled strip).
    _run(["node", "docs/tools/gen_bake_intermediates.js", BAKE_INTER],
         "gen_bake_intermediates.js")
    _run([PY, "docs/tools/gen_bake_strip.py", BAKE_INTER], "gen_bake_strip.py")
    # The matplotlib figures: every remaining docs/tools/gen_*.py, in name
    # order (the 27 gen_perf_<topic> + gen_perf_figs, bake/binary/disk/
    # home/hub/lighting/menu/roller/rotate/strip figs).
    for script in sorted((_DOCS / "tools").glob("gen_*.py")):
        if script.name in FIGS_SKIP:
            continue
        _run([PY, f"docs/tools/{script.name}"], script.name)


def _regen_nasa() -> None:
    """The NASA answer-key art (docs/img/nasa/): the gate run writes the
    32 C-runtime frames nasa_figs.py registers against the SVS 5415 crops.
    Needs the gate venv (numpy/PIL + the moon package) AND the gitignored
    answer key under assets/images/moon/final_400x400/ — hence the opt-in
    MEDIA_NASA=1 slot rather than the default path."""
    _run([HOSTPY, "host_sim/check_gate.py", "--target", "s3"],
         "check_gate.py --target s3")
    _run([HOSTPY, "docs/nasa_figs.py"], "nasa_figs.py")
    _run([HOSTPY, "docs/tools/gen_nasa_quarters.py"], "gen_nasa_quarters.py")


def _media_pipeline(app) -> None:
    """Run the full regeneration: the scripted figures (then the NASA art
    if MEDIA_NASA=1), uisim both panels, the --wheel capture for both, the
    annotations, the device composites, and (if MEDIA_VIDEOS=1) the mp4
    set. Sequential — the uisim builds share out/."""
    s3_dir = _DOCS / "img" / "screens"
    p4_dir = s3_dir / "p4"

    print("moonmedia: scripted figures (docs/_static/figs)")
    _regen_figs()

    if os.environ.get("MEDIA_NASA"):
        print("moonmedia: NASA answer-key art (docs/img/nasa)")
        _regen_nasa()
    else:
        print("moonmedia: skipping the NASA art (set MEDIA_NASA=1; needs "
              "the gitignored final_400x400 answer key)")

    print("moonmedia: uisim S3 (466) — base screens")
    _run(["bash", "host_sim/uisim/build.sh", "run"], "build.sh run")
    print("moonmedia: uisim S3 (466) — --wheel capture")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--wheel"], "build.sh run --wheel")
    print("moonmedia: uisim S3 (466) — --tour promo frames")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--tour"], "build.sh run --tour")
    _copy_panel(s3_dir)

    # The device composites read the uisim frames from out/ directly and
    # composite the 466 S3 set into the product photo — so they must run
    # while out/ still holds the S3 frames, BEFORE the P4 run overwrites
    # the shared out/.
    print("moonmedia: device composites (the S3 466 set)")
    _run([PY, "docs/tools/composite_dark.py"], "composite_dark.py")

    # The mp4 set runs HERE, while out/ still holds the S3 466 frames:
    # gen_media reads out/ directly and its film is pinned to that set.
    # The old tail position ran after the P4/knob builds had replaced
    # out/ (/qa 2026-11-15: the canonical builder would have fed the
    # film knob frames; the pinned 466 check now catches that anyway).
    if os.environ.get("MEDIA_VIDEOS"):
        print("moonmedia: mp4 set (the S3 466 frames, before out/ moves on)")
        _run([PY, "docs/gen_media.py"], "gen_media.py")
    else:
        print("moonmedia: skipping the mp4 set (set MEDIA_VIDEOS=1 to include it)")

    print("moonmedia: uisim P4 (720) — base screens")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "720"], "build.sh run --panel 720")
    print("moonmedia: uisim P4 (720) — --wheel capture")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "720", "--wheel"], "build.sh run --panel 720 --wheel")
    _copy_panel(p4_dir)

    print("moonmedia: uisim knob (360) — base screens")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "360"], "build.sh run --panel 360")
    print("moonmedia: uisim knob (360) — --wheel + --dial captures")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "360", "--wheel"], "build.sh run --panel 360 --wheel")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "360", "--dial", "settings_hub"],
         "build.sh run --panel 360 --dial settings_hub")
    _copy_knob_panel(s3_dir / "knob")

    print("moonmedia: annotations (both clock panels)")
    _annotate_all()

    print("moonmedia: done.")


# --------------------------------------------------------------------------
# the media builder
# --------------------------------------------------------------------------

class MoonMediaBuilder(Builder):
    """A side-effect builder: it runs the media pipeline and nothing
    else (no doctree read, no HTML write). Sphinx's ``sphinx-build``
    drives a builder through exactly one of the three entry points
    below (``build_update`` for a plain run, ``build_all`` with ``-E``,
    ``build_specific`` with a file list), and the base implementation of
    each walks the read/write machinery (``get_outdated_docs``) — so
    every entry point is overridden to run the pipeline and stop."""

    name = "media"

    def _run(self):
        _media_pipeline(self.app)

    def build_update(self):
        self._run()

    def build_all(self):
        self._run()

    def build_specific(self, filenames):
        self._run()


def _copy_videos(app, exception=None):
    """Copy docs/img/*.mp4 into the HTML output's _static/img/.

    The <video> embeds are ``.. raw:: html``, so Sphinx's image/static
    machinery never sees them and never copies the files — the built page's
    ``_static/img/`` would be empty and the film would 404. This version of
    Sphinx exposes no ``app.add_static_file`` API, so copy on build-finished
    (idempotent; guarded to the HTML builder so the media run is untouched)."""
    if getattr(app.builder, "name", None) != "html":
        return
    out = getattr(app, "outdir", None)
    if not out:
        return
    dst_dir = Path(out) / "_static" / "img"
    dst_dir.mkdir(parents=True, exist_ok=True)
    for v in VIDEOS:
        src = _DOCS / "img" / v
        if src.exists():
            (dst_dir / v).write_bytes(src.read_bytes())


def setup(app):
    app.add_builder(MoonMediaBuilder)
    app.connect("builder-inited", _stale_check)
    app.connect("build-finished", _copy_videos)
    return {"version": "1.0", "parallel_read_safe": True, "parallel_write_safe": True}
