# Sphinx configuration for the Moon Phase Clock documentation.
#
# Build (from the repo root):
#   docs/.venv/bin/sphinx-build -b html docs docs/_build/html
#
# The project lives directly under docs/ (a standard Sphinx layout).
# Two local extensions in docs/extensions/ are loaded by bare name
# (the extensions directory is on sys.path below):
#
#   mooncode  — generates browsable C source pages under code/gen/ and
#               provides the :cfile: / :cfunc: / :clabel: roles
#               (doc → code) plus a "Referenced from" index on every
#               code page (code → doc). The code/ entry must stay the
#               LAST toctree entry in index.rst so the reverse index
#               can see every doc's resolved references first.
#   moonmedia — registers the ``media`` builder (regenerates the uisim
#               screenshots, the annotated shots, and the device
#               composites) and warns during html builds when a
#               referenced screenshot is older than the UI sources it
#               was rendered from.

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "extensions"))

# The Python prototype package the reference docs autodoc (mooncore).
MOON_PKG = os.path.abspath(os.path.join(_HERE, "..", "assets", "images", "moon"))
sys.path.insert(0, MOON_PKG)

project = "The Moon Phase Clock"
author = "Matthias Kastner"
copyright = "2026, Matthias Kastner"
version = "1.0"
release = "2026.11"

extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.viewcode",
    "sphinx.ext.mathjax",
    "sphinx.ext.imgconverter",
    "sphinx.ext.graphviz",
    "sphinx_copybutton",
    "mooncode",
    "moonmedia",
]

# The hardware-wiring schematics and other structural diagrams are
# authored as Graphviz ``.dot`` files and rendered to inline SVG at
# build time (Sphinx-native ``.. graphviz::`` directives). ``dot``
# (Graphviz) must be on PATH for a clean build — the ``.dot`` sources
# are checked in so the SVG is reproducible.
graphviz_output_format = "svg"
graphviz_dot = "dot"

html_theme = "furo"
# SWAP → "sphinx_rtd_theme" (sphinx-rtd-theme is in requirements.txt).
html_static_path = ["_static"]
html_title = "The Moon Phase Clock"
html_favicon = "_static/photos/bench_panel.jpg"

# Furo theming: the theme follows the reader's OS preference (furo's
# native default — furo 2025.12 has no default-theme option), with the
# product's moon grey (SETTINGS_GREY, screens.c) as the accent in
# place of the stock blue. The greys step per theme so links clear
# WCAG AA on both backgrounds: light mode on #f8f8f6 (4.7–7.2:1),
# dark mode on #131416 (5.4–8.9:1). ``color-brand-content`` is the
# body link colour, ``color-brand-primary`` the sidebar/TOC active
# links, ``color-brand-visited`` the visited links.
pygments_style = "monokai"
html_theme_options = {
    "light_css_variables": {
        "color-brand-primary": "#525252",
        "color-brand-content": "#5f5f5f",
        "color-brand-visited": "#6e6e6e",
    },
    "dark_css_variables": {
        "color-brand-primary": "#8a8a8a",
        "color-brand-content": "#9b9b9b",
        "color-brand-visited": "#b3b3b3",
    },
}

# Math: MathJax (the geometry chapter uses it).
mathjax_path = "https://cdnjs.cloudflare.com/ajax/libs/mathjax/2.7.7/MathJax.js?config=TeX-MML-AM_CHTML"

# Mermaid: the architecture stack and the sleep state machine are
# rendered client-side from ``.. raw:: html`` ``<pre class="mermaid">``
# blocks. Pinned CDN build + the small init script that picks furo's
# current theme and re-renders on a theme toggle (harmless/unused
# under sphinx_rtd_theme).
html_js_files = [
    "https://cdn.jsdelivr.net/npm/mermaid@10.9.1/dist/mermaid.min.js",
    # Local entry: no ``_static/`` prefix — furo emits it as
    # ``<page-relative>_static/`` + entry, so prefixing it yields the
    # doubled ``_static/_static/`` path (404, the init script never loads).
    "mermaid_init.js",
]

# autodoc: the moon package is small and fully documented; show everything.
autodoc_default_options = {
    "members": True,
    "undoc-members": True,
    "show-inheritance": True,
    "member-order": "bysource",
}
autodoc_typehints = "description"

# copybutton: strip common prompt prefixes from the C/Python examples.
copybutton_prompt_text = r">>> |\.\.\. |\$ |esp32s3-moon~ |host:"
copybutton_prompt_is_regexp = True

master_doc = "index"
# code/gen/ is written at build time by the mooncode extension at
# ``builder-inited`` — i.e. BEFORE the source scan — so the generated
# RST is picked up normally (it must NOT be excluded, or the gen/index
# toctree entry would dangle). book/ is the old chapter book still on
# disk during the migration — it leaves in stage 4, with this exclude
# entry.
exclude_patterns = ["_build", ".venv", "book", "Thumbs.db", ".DS_Store"]

latex_elements = {
    "papersize": "a4paper",
    "pointsize": "10pt",
}
