"""mooncode — browsable C source pages + two-way doc<->code links.

At ``builder-inited`` (before the source scan) the extension parses the
tracked C sources (``git ls-files '*.c' '*.h' '*.cpp'`` under
``components/`` and ``main/`` at the repo root) and writes real RST
into ``code/gen/`` (gitignored, excluded from no toctree): one page
per file, one section per function with an explicit ``.. _cfn-<name>:``
anchor, the body as a Pygments-highlighted block (``linespans="L"``)
so every line gets a ``#L-<lineno>`` anchor with its TRUE file line
number. ``code/`` is the LAST toctree entry in ``index.rst`` (the
reverse index below relies on that order at write time).

Doc → code roles (unknown targets degrade to a plain literal, so a
0-warning build holds even when code moves between the last commit and
the build):

    :cfile:`components/ui/screens.c`
        the per-file page;
    :cfunc:`moon_ui_screen_switched`
        the ``#cfn-<name>`` anchor (first definition wins);
    :clabel:`components/ui/screens.c:612`
        the ``#L-612`` line anchor (out-of-range lines degrade too).

Code → doc: every generated page ends with a "Referenced from" section
listing the doc pages whose :cfile:/:cfunc:/:clabel: targets hit it.
The reference map is recorded at role-parse time (read phase — before
ANY document is written, so write order is irrelevant), kept across
incremental builds in a pickle next to the doctrees, and merged at
``write-started``: a doc re-read this build replaces its old
contribution (removed links are evicted); docs not re-read keep theirs.
Reads are serial (the map is in-process state).
"""

import os
import pickle
import re
import subprocess
from pathlib import Path

from docutils import nodes
from pygments import highlight as _pyg_highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import get_lexer_by_name
from sphinx.addnodes import pending_xref

_C_LEXER = get_lexer_by_name("c", stripall=False)
_CPP_LEXER = get_lexer_by_name("cpp", stripall=False)


def _render_code(body_lines, lineno_start: int, rel: str) -> str:
    """Pygments-highlight *body_lines* starting at 1-based file line
    *lineno_start*, with a ``#L-<n>`` anchor per line (``linespans="L"``
    → ``id="L-<n>"`` on each line). Emitted as ``.. raw:: html`` — the
    repo already carries generated content that way (mermaid, video) —
    so the per-line anchors are under our control and the ``<div
    class="highlight">`` wrapper is styled by furo's pygments CSS."""
    text = "\n".join(body_lines)
    lexer = _CPP_LEXER if rel.endswith(".cpp") else _C_LEXER
    fmt = HtmlFormatter(
        linenos=True, linespans="L", cssclass="highlight", linenostart=lineno_start
    )
    return _pyg_highlight(text, lexer, fmt)

# --- symbol tables (rebuilt every build at builder-inited) -------------
# FILES: relpath -> generated docname
FILES: dict[str, str] = {}
# FUNCS: function name -> (generated docname, 1-based def line)
FUNCS: dict[str, tuple[str, int]] = {}
# LINES: relpath -> total line count
LINES: dict[str, int] = {}

# --- reverse index ------------------------------------------------------
# _PREV:      {refdoc: {(target docname, fragment)}}  loaded from pickle
# _THIS:      contributions recorded by this build's role parses (same shape)
# _READ_THIS: docnames re-read this build (source-read fires per re-read doc).
#             A re-read doc REPLACES its previous contribution — including an
#             empty one, so links removed from a re-read doc are evicted. A
#             doc NOT re-read this build keeps its previous contribution.
_PREV: dict[str, set] = {}
_THIS: dict[str, set] = {}
_READ_THIS: set[str] = set()

_BUILDER = None
_REPO: Path | None = None
_PICKLE_NAME = "mooncode-refs.pkl"

_KEYWORDS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else",
    "struct", "union", "enum", "typedef", "goto", "case", "default",
    "break", "continue",
}
# An identifier immediately followed by '(' where the identifier is not
# itself preceded by a word char or '(' (that rejects call targets in
# ``foo(bar(`` and function-pointer members ``(*cb)(``).
_CAND = re.compile(r"(?<![\w(])([A-Za-z_]\w*)\s*\(")


# --- source analysis -----------------------------------------------------

def _strip_code(text: str) -> str:
    """Blank out // and /* */ comments and "str" / 'ch' literals
    (newlines preserved, other chars space-filled) so the paren/brace
    counting below sees only code."""
    out: list[str] = []
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if c == '"':
                state = "str"
                out.append('"')
            elif c == "'":
                state = "chr"
                out.append("'")
            else:
                out.append(c)
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
        elif state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                state = "code"
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
        else:  # "str" / "chr"
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"
            out.append("\n" if c == "\n" else " ")
            i += 1
    return "".join(out)


def parse_functions(code: str):
    """Find every function DEFINITION in *code* (comments/strings
    already stripped). A definition: at brace depth 0 and paren depth
    0, an identifier + ``(`` whose matching ``)`` is followed — skipping
    whitespace and newlines — by ``{`` (a ``;`` there is a prototype or
    declaration). Yields ``(name, start_line, [body lines])`` where
    start_line is 1-based and the body spans the signature line through
    the line holding the matching ``}``."""
    lines = code.split("\n")
    n = len(lines)
    results = []
    depth = 0      # brace depth at the start of the current line
    pdepth = 0     # paren depth at the start of the current line
    i = 0
    while i < n:
        line = lines[i]
        consumed = False
        if depth == 0 and pdepth == 0 and not line.lstrip().startswith("#"):
            for m in _CAND.finditer(line):
                # paren depth at the candidate's '(':
                prefix = line[: m.start()]
                if pdepth + prefix.count("(") - prefix.count(")") != 0:
                    continue
                name = m.group(1)
                if name in _KEYWORDS:
                    continue
                # find the matching ')' of this '(' (scan forward from it)
                op = line.index("(", m.end(1))
                pd = 1
                j, k = i, op + 1
                while pd > 0 and j < n:
                    seg = lines[j]
                    while k < len(seg) and pd > 0:
                        ch = seg[k]
                        if ch == "(":
                            pd += 1
                        elif ch == ")":
                            pd -= 1
                        k += 1
                    if pd > 0:
                        j += 1
                        k = 0
                if pd != 0:
                    continue  # unbalanced — not a sane definition
                # after the ')' at lines[j][k-1]: skip blanks to the next char
                j2, k2 = j, k
                found = False
                while j2 < n and not found:
                    seg = lines[j2]
                    while k2 < len(seg) and seg[k2] in " \t":
                        k2 += 1
                    if k2 < len(seg):
                        found = True
                        nxt = seg[k2]
                    else:
                        j2 += 1
                        k2 = 0
                if not found:
                    continue
                if nxt != "{":
                    continue  # prototype / declaration
                # walk to the matching '}':
                d = 0
                end = None
                for jj in range(j2, n):
                    for ch in lines[jj]:
                        if ch == "{":
                            d += 1
                        elif ch == "}":
                            d -= 1
                            if d == 0:
                                end = jj
                                break
                    if end is not None:
                        break
                if end is None:
                    continue  # brace-unbalanced to EOF: not a function
                results.append((name, i + 1, lines[i : end + 1]))
                # A top-level function body is brace-balanced, so after its
                # closing brace the depth returns to the value at its start
                # (0, by the candidate precondition). Consume the whole body
                # and do NOT depth-count the signature line — counting it
                # would wedge depth at 1 and hide every later function.
                i = end + 1
                depth = 0
                pdepth = 0
                consumed = True
                break
        if consumed:
            i += 1
            continue
        for c in line:
            if c == "{":
                depth += 1
            elif c == "}":
                depth = max(0, depth - 1)
            elif c == "(":
                pdepth += 1
            elif c == ")":
                pdepth = max(0, pdepth - 1)
        i += 1
    return results


# --- generation ----------------------------------------------------------

def _gen_docname(rel: str) -> str:
    return "code/gen/" + rel


def _rst_section(title: str, underline: str) -> str:
    return f"{title}\n{underline * len(title)}\n\n"


def _generate(app) -> None:
    global _BUILDER, _REPO
    _BUILDER = app.builder
    srcdir = Path(app.srcdir)
    _REPO = srcdir.parent  # the site sits at <repo>/docs

    out = subprocess.run(
        ["git", "ls-files", "-z", "*.c", "*.h", "*.cpp"],
        cwd=_REPO, capture_output=True, text=True, check=True,
    ).stdout
    files = []
    for f in out.split("\0"):
        f = f.strip()
        if not f or f.startswith("managed_components/"):
            continue
        if f.split("/")[0] not in ("components", "main"):
            continue
        if f.endswith("moon_assets_gen.c"):
            continue
        files.append(f)
    files.sort()

    FILES.clear()
    FUNCS.clear()
    LINES.clear()

    genroot = srcdir / "code" / "gen"
    index_groups: dict[str, list[str]] = {}  # dir -> [toctree entries]
    anchored: set[str] = set()  # cfn anchors already emitted (site-wide)

    for rel in files:
        text = (_REPO / rel).read_text(encoding="utf-8", errors="replace")
        code = _strip_code(text)
        fns = parse_functions(code)
        docname = _gen_docname(rel)
        FILES[rel] = docname
        LINES[rel] = len(text.splitlines())
        for fname, line, _body in fns:
            if fname not in FUNCS:  # first definition wins
                FUNCS[fname] = (docname, line)

        d = genroot / os.path.dirname(rel)
        d.mkdir(parents=True, exist_ok=True)
        parts = rel.split("/")
        group = "/".join(parts[:-1])
        buf = [_rst_section(rel, "=")]
        buf.append(
            f"Tracked source ``{rel}`` ({LINES[rel]} lines, {len(fns)} "
            f"functions). Generated by the ``mooncode`` extension at "
            f"build time — do not edit.\n"
        )
        for fname, line, body in fns:
            # explicit targets must be unique site-wide; the FIRST
            # definition of a name takes the canonical cfn-<name> anchor
            # (FUNCS points there), later definitions get a line-suffixed
            # variant so the section is still directly linkable.
            if fname in anchored:
                buf.append(f".. _cfn-{fname}~{line}:\n\n")
            else:
                buf.append(f".. _cfn-{fname}:\n\n")
                anchored.add(fname)
            buf.append(_rst_section(fname, "-"))
            buf.append(f"*Defined at line {line}.*\n\n")
            buf.append(".. raw:: html")
            buf.append("")
            for h in _render_code(body, line, rel).splitlines():
                buf.append("   " + h)
            buf.append("")
        # the container must carry content at parse time; the
        # doctree-resolved pass replaces it with the real section.
        buf.append("\n.. container:: mooncode-backlinks\n\n")
        buf.append("   (populated at build time: doc pages that link into this source)\n\n")
        (d / (rel.rsplit("/", 1)[-1] + ".rst")).write_text("\n".join(buf), encoding="utf-8")

        # toctree entry relative to code/gen/index.rst (it sits in code/gen/)
        index_groups.setdefault(group, []).append(rel)

    # code/gen/index.rst — one captioned toctree per directory. The
    # options must be contiguous (a blank line ends the option
    # field-list, and a stray :caption: would then parse as an entry).
    gbuf = [_rst_section("Generated source pages", "=")]
    gbuf.append(
        "One page per tracked C source under ``components/`` and "
        "``main/`` (the ``mooncode`` extension generates these at build "
        "time — do not edit).\n"
    )
    for group in sorted(index_groups):
        block = [".. toctree::", "   :maxdepth: 1", f"   :caption: {group}", ""]
        block += [f"   {e}" for e in sorted(index_groups[group])]
        gbuf.append("\n".join(block) + "\n")
    (genroot / "index.rst").write_text("\n".join(gbuf), encoding="utf-8")


def _builder_inited(app):
    if app.builder.name not in ("html", "media"):
        return
    if not os.path.isdir(os.path.join(app.srcdir, "code")):
        return
    _generate(app)
    # load the previous build's reference map (incremental builds)
    pk = Path(app.doctreedir) / _PICKLE_NAME
    if pk.exists():
        try:
            _PREV.update(pickle.loads(pk.read_bytes()))
        except Exception:
            _PREV.clear()


# _REFS_MERGED is filled at write-started (before any doctree-resolved)
_REFS_MERGED: dict[str, set] = {}


def _source_read(app, docname, source):
    if docname:
        _READ_THIS.add(docname)


def _write_started(app, builder):
    """Merge this build's recorded contributions over the previous map.
    A doc re-read this build REPLACES its previous contribution —
    including an empty one (its references were all removed), so the
    links are evicted; a doc NOT re-read keeps its previous
    contribution. Refs into docs that no longer exist are dropped. The
    result is stored for the next incremental build."""
    global _REFS_MERGED
    merged = {d: set(s) for d, s in _PREV.items()}
    # re-read docs: take exactly this build's record (or empty)
    for d in _READ_THIS:
        if d in merged:
            del merged[d]
        if d in _THIS:
            merged[d] = set(_THIS[d])
    found = set(builder.env.found_docs)
    _REFS_MERGED = {
        d: {(t, f) for (t, f) in s if t in found}
        for d, s in merged.items()
        if d in found
    }
    pk = Path(builder.doctreedir) / _PICKLE_NAME
    pk.write_bytes(pickle.dumps(_REFS_MERGED))


def _record(refdoc: str, target: str, frag: str):
    if refdoc:
        _THIS.setdefault(refdoc, set()).add((target, frag))


# --- roles (doc -> code) --------------------------------------------------

def _cfile_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    rel = text.strip().rstrip("/")
    if rel not in FILES:
        return [nodes.literal(rawtext, text, classes=["cfile", "xref"])], []
    env = inliner.document.settings.env
    target = FILES[rel]
    px = pending_xref(rawtext, refdoc=env.docname, reftype="cfile",
                      reftarget=target, refexplicit=True)
    px.append(nodes.literal(rawtext, text, classes=["cfile", "xref"]))
    _record(env.docname, target, "")
    return [px], []


def _cfunc_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    fname = text.strip()
    hit = FUNCS.get(fname)
    if not hit:
        return [nodes.literal(rawtext, text, classes=["cfunc", "xref"])], []
    target, _line = hit
    env = inliner.document.settings.env
    frag = "cfn-" + fname
    px = pending_xref(rawtext, refdoc=env.docname, reftype="cfunc",
                      reftarget=target, refid=frag, refexplicit=True)
    px.append(nodes.literal(rawtext, text, classes=["cfunc", "xref"]))
    _record(env.docname, target, frag)
    return [px], []


_CLABEL_RE = re.compile(r"^(\S+?)(?::(\d+))?$")


def _clabel_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    m = _CLABEL_RE.match(text.strip())
    if not m or m.group(1) not in FILES:
        return [nodes.literal(rawtext, text, classes=["clabel", "xref"])], []
    rel, ln = m.group(1), m.group(2)
    if ln is not None and not (1 <= int(ln) <= LINES[rel]):
        return [nodes.literal(rawtext, text, classes=["clabel", "xref"])], []
    env = inliner.document.settings.env
    target = FILES[rel]
    # Pygments linespans="L" anchors are id="L-<n>" (dash included)
    frag = "L-" + ln if ln else ""
    px = pending_xref(rawtext, refdoc=env.docname, reftype="clabel",
                      reftarget=target, refid=frag, refexplicit=True)
    px.append(nodes.literal(rawtext, text, classes=["clabel", "xref"]))
    _record(env.docname, target, frag)
    return [px], []


def _missing_reference(event, env, node, contnode):
    """Resolve the three custom reftypes to a relative page URI.

    Must RETURN a new ``nodes.reference`` carrying the ``refuri`` (the
    post-transform replaces the pending_xref with whatever is returned
    and keeps only that node's attributes) — mutating the pending node
    or the returned contnode loses the URI. The display literal
    (``contnode``) is appended so the text + its ``xref`` classes render.
    """
    reftype = node.get("reftype")
    if reftype not in ("cfile", "cfunc", "clabel"):
        return None
    target = node.get("reftarget")
    if target not in env.found_docs:
        return None
    refuri = _BUILDER.get_relative_uri(node["refdoc"], target)
    refid = node.get("refid")
    if refid:
        refuri += "#" + refid
    ref = nodes.reference("", internal=True)
    ref["refuri"] = refuri
    ref.append(contnode)
    return ref


# --- reverse index (code -> doc) ------------------------------------------

def _doctree_resolved(app, doctree, docname):
    if not docname.startswith("code/gen/"):
        return
    placeholder = None
    for cont in doctree.findall(nodes.container):
        if "mooncode-backlinks" in (cont.get("classes") or []):
            placeholder = cont
            break
    if placeholder is None:
        return
    parents = sorted(
        d for d, s in _REFS_MERGED.items() if any(t == docname for (t, _f) in s)
    )
    # A container (NOT a nodes.section): doctree-resolved runs after the
    # id-assigning transform, so a bare section has no ``ids`` and the
    # HTML writer's secnumber pass crashes on ``parent['ids'][0]``.
    # container renders as <div class="mooncode-backlinks-section">.
    div = nodes.container()
    div["classes"] = ["mooncode-backlinks-section"]
    label = nodes.paragraph()
    label += nodes.strong("", "Referenced from")
    div += label
    if parents:
        bl = nodes.bullet_list()
        for d in parents:
            title = app.env.titles.get(d)
            name = title.astext() if title is not None else d
            ref = nodes.reference("", internal=True)
            ref["refuri"] = app.builder.get_relative_uri(docname, d)
            ref.append(nodes.inline("", name, classes=["doc"]))
            # docutils 0.21: an INLINE child passed to a node
            # CONSTRUCTOR is stringified into text — append (+=) it
            # instead. The paragraph wrapper is load-bearing: the HTML
            # writer asserts that a reference whose parent is not a
            # TextElement holds exactly one image, so a bare reference
            # directly under the list_item would trip it (a paragraph
            # IS a TextElement).
            p = nodes.paragraph("")
            p += ref
            bl += nodes.list_item("", p)
        div += bl
    else:
        div += nodes.paragraph("", "No doc pages reference this source yet.")
    placeholder.parent.replace(placeholder, div)


def setup(app):
    app.add_role("cfile", _cfile_role)
    app.add_role("cfunc", _cfunc_role)
    app.add_role("clabel", _clabel_role)
    app.connect("missing-reference", _missing_reference)
    app.connect("builder-inited", _builder_inited)
    app.connect("source-read", _source_read)
    app.connect("write-started", _write_started)
    app.connect("doctree-resolved", _doctree_resolved)
    app.add_config_value("mooncode_roots", ["components", "main"], "env")
    return {
        "version": "1.0",
        "parallel_read_safe": False,  # the reference map is in-process state
        "parallel_write_safe": True,
    }
