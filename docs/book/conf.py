# Sphinx configuration for the Moon Phase Clock book.
#
# Build (from the repo root):
#   docs/book/.venv/bin/sphinx-build -b html docs/book docs/book/_build/html
#
# The book lives under docs/ on purpose: docs/gen_doc.py only *embeds*
# code files that are NOT under docs/, so the book's RST and conf.py
# stay out of the generated DOCUMENTATION.md (they appear in its
# docs/ inventory instead).

import sys
import os

# The Python prototype package the book documents (Part V autodoc).
MOON_PKG = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "assets", "images", "moon"))
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
    "sphinx_copybutton",
]

html_theme = "furo"
html_static_path = ["_static"]
html_title = "The Moon Phase Clock"
html_favicon = "_static/photos/bench_panel.jpg"

# Furo theming: dark-first, moon-grey accents.
pygments_style = "monokai"
html_theme_options = {
    "navigation_with_keys": True,
}

# Math: MathJax (the geometry chapter uses it).
mathjax_path = "https://cdnjs.cloudflare.com/ajax/libs/mathjax/2.7.7/MathJax.js?config=TeX-MML-AM_CHTML"

# Mermaid: the architecture stack (Chapter 10) and the sleep state
# machine (Chapter 15) are rendered client-side from ``.. raw:: html``
# ``<pre class="mermaid">`` blocks. Pinned CDN build (the book already
# loads MathJax from a CDN) + the small init script that picks furo's
# current theme and re-renders on a theme toggle.
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

# The book references the repo's committed figures directly (relative
# paths from this conf.py).
import sphinx  # noqa: E402  (version pin for the theme)

master_doc = "index"
exclude_patterns = ["_build", ".venv", "Thumbs.db", ".DS_Store"]

latex_elements = {
    "papersize": "a4paper",
    "pointsize": "10pt",
}
