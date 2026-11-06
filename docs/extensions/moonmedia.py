"""moonmedia — the ``media`` builder + screenshot staleness checks.

Two jobs:

* a ``media`` Sphinx builder — ``sphinx-build -b media`` regenerates the
  uisim screenshots for BOTH panels (``host_sim/uisim/build.sh run`` +
  ``--panel 720``, and the ``--wheel`` capture flow for the password
  keyboard), runs ``docs/tools/annotate.py`` over every
  ``ui/labels/*.json`` spec for both panels, rebuilds the device
  composites (``docs/tools/composite_dark.py``), and — with ``--videos``
  — the mp4 set (``docs/gen_media.py``). The builder performs ONLY these
  side effects; it reads no doctree and writes no HTML, so a media run
  is fast even on a cold tree.

* a ``builder-inited`` staleness check for the normal builds: every
  committed screenshot (``docs/img/screens{,/p4}/*.png``), annotated
  callout (``docs/img/screens/annot/*.png``) and device composite
  (``docs/_static/photos/sim_*.png``) must EXIST and must not be OLDER
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

# The four animation films, referenced by implementation/animations.rst via
# ``.. raw:: html`` ``<video>``. Raw HTML never triggers Sphinx's image copy,
# so each is registered as a static file (landed under _static/img/) and the
# RST points at ``_static/img/`` — otherwise the built page's ``../img/`` is
# empty and the video 404s.
VIDEOS = ["boot_sweep.mp4", "free_spin.mp4", "dusk.mp4", "fade.mp4"]


# The 16 base screen shots uisim names them (the ScreensEnum, underscored).
BASE_SCREENS = [
    "modem_sleep", "wifi", "light_sleep", "deep_sleep", "alarm",
    "settings_hub", "settings_alarm", "settings_pomodoro",
    "settings_alarm_time", "settings_light_sleep", "settings_deep_sleep",
    "settings_wifi", "settings_wifi_password", "settings_time",
    "settings_tz", "settings_alarm_sound",
]
# The --wheel capture flow's wheel_<name>.png -> the committed docs name.
WHEEL_MAP = {
    "rest": "settings_wifi_password",
    "touched": "settings_wifi_password_touched",
    "ABC": "settings_wifi_password_ABC",
    "123": "settings_wifi_password_123",
    "sym": "settings_wifi_password_sym",
}


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
    # the font sets (both boards; uisim compiles them in software)
    for fdir in ("components/ui/fonts", "components/ui/fonts_p4"):
        d = ROOT / fdir
        if d.is_dir():
            p.extend(sorted(d.glob("*.c")))
    # the uisim build inputs (the scripted indev + the build driver)
    for f in ("main.c", "stubs.c", "stb_impl.c", "build.sh"):
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
    for t in ("s3", "p4"):
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
    """Warn on any committed output that is missing or older than its
    render inputs. Skipped for the media builder (which regenerates)."""
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
    for sub in ("", "p4"):
        base = screens_dir / sub
        for png in sorted(base.glob("*.png")) if base.is_dir() else []:
            jobs.append((png, inputs))
    # annotated callouts: inputs + the matching label spec (the callout
    # positions are authored data, a distinct input from the raw shot)
    if annot_dir.is_dir():
        for png in sorted(annot_dir.glob("*.png")):
            spec = labels_dir / (png.name.rsplit("_", 1)[0] + ".json")
            extra = [spec] if spec.exists() else []
            jobs.append((png, inputs + extra))
    # device composites (the uisim-in-photo frames)
    if photos_dir.is_dir():
        for png in sorted(photos_dir.glob("sim_*.png")):
            jobs.append((png, inputs))

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
            "moonmedia: %d committed screenshot(s) are missing or older "
            "than their render inputs — regenerate with `sphinx-build -b media` "
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
    The 16 base screens are copied by name (out/ also carries
    verification shots that must NOT land in the docs); the password
    keyboard's base shot is the ``--wheel rest`` capture (the prefilled
    SSID), not the plain bare screen."""
    for name in BASE_SCREENS:
        _copy(OUT / f"{name}.png", panel_dir / f"{name}.png")
    for wheel, doc in WHEEL_MAP.items():
        _copy(OUT / f"wheel_{wheel}.png", panel_dir / f"{doc}.png")


def _annotate_all() -> None:
    labels = sorted((_DOCS / "ui" / "labels").glob("*.json"))
    annot = _DOCS / "img" / "screens" / "annot"
    annot.mkdir(parents=True, exist_ok=True)
    for spec in labels:
        screen = spec.stem                       # e.g. "settings-hub"
        shot = screen.replace("-", "_")          # e.g. "settings_hub"
        for panel, tag in (("466", "s3"), ("720", "p4")):
            base = _DOCS / "img" / "screens" / ("" if tag == "s3" else "p4") / f"{shot}.png"
            out = annot / f"{screen}_{tag}.png"
            _run([PY, "docs/tools/annotate.py", base,
                  "--spec", str(spec), "--panel", panel, "-o", out],
                 f"annotate {out.relative_to(ROOT)}")


def _media_pipeline(app) -> None:
    """Run the full regeneration: uisim both panels, the --wheel capture
    for both, the annotations, the device composites, and (if --videos)
    the mp4 set. Sequential — the two uisim builds share out/."""
    s3_dir = _DOCS / "img" / "screens"
    p4_dir = s3_dir / "p4"

    print("moonmedia: uisim S3 (466) — base screens")
    _run(["bash", "host_sim/uisim/build.sh", "run"], "build.sh run")
    print("moonmedia: uisim S3 (466) — --wheel capture")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--wheel"], "build.sh run --wheel")
    _copy_panel(s3_dir)

    # The device composites read the uisim frames from out/ directly and
    # composite the 466 S3 set into the product photo — so they must run
    # while out/ still holds the S3 frames, BEFORE the P4 run overwrites
    # the shared out/.
    print("moonmedia: device composites (the S3 466 set)")
    _run([PY, "docs/tools/composite_dark.py"], "composite_dark.py")

    print("moonmedia: uisim P4 (720) — base screens")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "720"], "build.sh run --panel 720")
    print("moonmedia: uisim P4 (720) — --wheel capture")
    _run(["bash", "host_sim/uisim/build.sh", "run", "--panel", "720", "--wheel"], "build.sh run --panel 720 --wheel")
    _copy_panel(p4_dir)

    print("moonmedia: annotations (both panels)")
    _annotate_all()

    if os.environ.get("MEDIA_VIDEOS"):
        print("moonmedia: mp4 set")
        _run([PY, "docs/gen_media.py"], "gen_media.py")
    else:
        print("moonmedia: skipping the mp4 set (set MEDIA_VIDEOS=1 to include it)")

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
