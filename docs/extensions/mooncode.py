"""mooncode — browsable C source pages + two-way doc<->code links.

Stage-2 skeleton. Registers the doc -> code roles; every target
degrades to a plain ``nodes.literal`` (no pending_xref, no 'unknown
reference' warning) until stage 3 fills the symbol tables from the
generated parse of the tracked C sources:

    :cfile:`components/ui/screens.c`
        the per-file source page (``code/gen/components/ui/screens.c``);
    :cfunc:`moon_ui_screen_switched`
        the function anchor ``#cfn-<name>`` on that page;
    :clabel:`components/ui/screens.c:612`
        the exact line anchor ``#L612`` (Pygments ``lineanchors``).

Stage 3 wires the ``builder-inited`` generation into ``code/gen/`` and
fills the reverse index: every code page ends with a "Referenced from"
list of the doc pages that link into it. Invariant: ``code/`` is the
LAST toctree entry in ``index.rst`` so every doc's
``doctree-resolved`` has run before the code pages are resolved.
"""

import os
import re

from docutils import nodes

# Populated at ``builder-inited`` (stage 3) from the parse of the
# tracked C sources:
#   FILES: {relpath: gen docname}   e.g. "components/ui/screens.c"
#            -> "code/gen/components/ui/screens.c"
#   FUNCS: {name: (gen docname, line)}   first definition wins
#   LINES: {relpath: line-count}
FILES = {}
FUNCS = {}
LINES = {}

_REFTYPES = ("cfile", "cfunc", "clabel")


def _literal(rawtext, text, reftype):
    """The unknown-target shape: a bare literal, never a pending_xref."""
    return [nodes.literal(rawtext, text, classes=[reftype, "xref"])], []


def _cfile_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    rel = text.strip().rstrip("/")
    if rel in FILES:
        # Stage 3: pending_xref resolved by _missing_reference.
        pass
    return _literal(rawtext, text, "cfile")


def _cfunc_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    fname = text.strip()
    if fname in FUNCS:
        # Stage 3: pending_xref with refid "cfn-<fname>".
        pass
    return _literal(rawtext, text, "cfunc")


def _clabel_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    m = re.match(r"^(\S+?)(?::(\d+))?$", text.strip())
    if m and m.group(1) in FILES:
        # Stage 3: pending_xref with refid "L<line>".
        pass
    return _literal(rawtext, text, "clabel")


def _missing_reference(event, env, node, contnode):
    """Resolve cfile/cfunc/clabel pending_xrefs once the generated
    docnames are in ``env`` (stage 3). Unknowns never reach here."""
    reftype = node.get("reftype")
    target = node.get("reftarget")
    if reftype not in _REFTYPES or target not in env.found_docs:
        return None
    refuri = env.doc2path(target, absolute=True)
    refid = node.get("refid")
    if refid:
        refuri += "#" + refid
    node.attributes["refuri"] = refuri
    return contnode


def _generate_source_pages(app):
    """Stage 3: ``git ls-files '*.c' '*.h'`` at the repo root, keep
    components/** + main/** (exclude managed_components/ and the
    generated moon_assets_gen.c); regex-parse the functions; emit
    code/gen/*.rst (per-function ``.. _cfn-<name>:`` anchors, body as
    ``.. code-block:: c :linenos: :lineanchors: <start>``) and
    code/gen/index.rst grouped by directory; each file doc ends with
    the ``mooncode-backlinks`` placeholder the reverse pass fills."""
    raise NotImplementedError("stage 3 (task #3)")


def _builder_inited(app):
    if app.builder.name not in ("html", "media"):
        return
    if not os.path.isdir(os.path.join(app.srcdir, "code")):
        return
    _generate_source_pages(app)


def setup(app):
    app.add_role("cfile", _cfile_role)
    app.add_role("cfunc", _cfunc_role)
    app.add_role("clabel", _clabel_role)
    app.connect("missing-reference", _missing_reference)
    # Stage 3: app.connect("builder-inited", _builder_inited)
    # (wired once _generate_source_pages exists; without it a fresh
    # build would fail on the missing code/gen toctree entries.)
    app.add_config_value("mooncode_roots", ["components", "main"], "env")
    return {"version": "0.2", "parallel_read_safe": True, "parallel_write_safe": True}
