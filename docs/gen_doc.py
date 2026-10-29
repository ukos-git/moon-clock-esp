#!/usr/bin/env python3
"""Generate docs/DOCUMENTATION.md from docs/doc_template.md.

The template's prose is kept verbatim; three placeholders are filled from the
working tree:
  DOCS_INVENTORY   the inventory of the docs/ tree (this document included)
  CLAUDE_MD        the project's CLAUDE.md, embedded verbatim
  CODE             every tracked file of the project except docs/ and media

Regenerate after any project change (the standing rule in CLAUDE.md):
    python3 docs/gen_doc.py
"""

import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
OUT = REPO / "docs" / "DOCUMENTATION.md"

# Files never embedded as code (binary / media / this document itself).
SKIP_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".mp4", ".bin", ".woff", ".otf", ".ttf"}
# Files under docs/ are inventoried, not embedded (they are the document).
CODE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".py", ".sh",
             ".csv", ".txt", ".yaml", ".yml", ".json", ".lock", ".md",
             ".cmake", ".lock"}

LANG = {
    ".c": "c", ".h": "c", ".cc": "cpp", ".cpp": "cpp", ".cxx": "cpp",
    ".hpp": "cpp", ".py": "python", ".sh": "bash", ".csv": "csv",
    ".yaml": "yaml", ".yml": "yaml", ".json": "json", ".lock": "json",
    ".txt": "ini", ".md": "markdown", ".cmake": "cmake",
}


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO,
                         capture_output=True, check=True).stdout
    return [p.decode() for p in out.split(b"\0") if p]


def fence(content):
    """A code fence longer than any run of backticks inside `content`."""
    n = 3
    while "`" * n in content:
        n += 1
    return "`" * n


def embed(path, content):
    return f"### `{path}`\n\n{fenced_block(path, content)}\n"


def code_section(files):
    parts = []
    for rel in files:
        p = REPO / rel
        if p.suffix in SKIP_EXTS or p.suffix not in CODE_EXTS:
            continue
        if rel.startswith("docs/"):
            continue
        parts.append(embed(pathlib.Path(rel), p.read_text(errors="replace")))
    return "\n".join(parts)


def docs_inventory():
    rows = []
    for p in sorted(REPO.glob("docs/**/*")):
        if not p.is_file():
            continue
        if p.name == "DOCUMENTATION.md" or "frames" in p.parts:
            continue
        rel = p.relative_to(REPO)
        rows.append(f"`{rel}` ({p.stat().st_size:,} B)")
    return "\n".join(f"- {r}" for r in rows)


def fenced_block(path, content):
    f = fence(content)
    lang = LANG.get(path.suffix, "")
    return f"{f}{lang}\n{content.rstrip()}\n{f}"


def main():
    tpl = (REPO / "docs" / "doc_template.md").read_text()
    claudemd = fenced_block(pathlib.Path("CLAUDE.md"),
                            (REPO / "CLAUDE.md").read_text())
    files = [f for f in tracked_files() if not f.startswith("docs/")]
    for name, text in [
        ("DOCS_INVENTORY", docs_inventory()),
        ("CLAUDE_MD", claudemd.rstrip()),
        ("CODE", code_section(files)),
    ]:
        if name not in tpl:
            sys.exit(f"placeholder {name} missing in doc_template.md")
        tpl = tpl.replace(name, text)
    OUT.write_text(tpl)
    print(f"wrote {OUT} ({OUT.stat().st_size:,} B)")


if __name__ == "__main__":
    main()
