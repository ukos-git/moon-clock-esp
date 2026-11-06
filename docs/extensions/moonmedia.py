"""moonmedia — the ``media`` builder + screenshot staleness checks.

Stage-2 skeleton. Stage 4 (task #6) registers:

* a ``media`` Sphinx builder — ``sphinx-build -b media`` regenerates
  the uisim screenshots for BOTH panels (``host_sim/uisim/build.sh
  run`` + ``--panel 720``), runs ``docs/tools/annotate.py`` over every
  ``ui/labels/*.json`` spec, and rebuilds the device composites
  (``docs/tools/composite_dark.py``); ``--videos`` also runs
  ``docs/gen_media.py``;
* a ``builder-inited`` staleness check for the normal html build:
  every referenced screenshot and composite must exist and must not be
  older than the tracked sources it was rendered from (the UI sources,
  fonts, uisim build inputs) — the docs must not silently show a
  stale UI.
"""

import os


def _stale_check(app):
    """Stage 4: compare the mtimes of the tracked screenshot files
    (docs/img/screens{,/p4}/*.png, _static/photos/sim_*.png) against
    their render inputs (components/ui/screens.c, styles.c, the font
    dirs, host_sim/uisim inputs) and app.warning() on drift."""
    pass


def setup(app):
    # Stage 4: app.add_builder(MoonMediaBuilder)
    # Stage 4: app.connect("builder-inited", _stale_check)
    return {"version": "0.1", "parallel_read_safe": True, "parallel_write_safe": True}
