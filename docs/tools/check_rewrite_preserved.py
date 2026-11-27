#!/usr/bin/env python3
"""Rewrite guard: did an edited RST page keep its links, figures and facts?

Compares each given page (working tree) against a git revision (default
HEAD) and reports what the edit dropped:

- labels (``.. _name:``) — other pages link to them;
- cross-reference targets (``:doc:``, ``:ref:``, ``:cfile:``, ``:cfunc:``,
  ``:clabel:``, ``:commit:``, ``:term:``);
- embedded media (``image``/``figure``/``graphviz``/``include``/
  ``literalinclude`` arguments, ``src=`` in raw HTML);
- numbers (every numeric token with 2+ significant characters) — a
  number that vanished is either a fact that was cut or one that moved
  to another page; the report lists them so a reviewer can tell which.

Targets/media/labels that moved to a *different* page in the same edit
are fine: pass every touched page and the guard checks the union.

Run:  docs/.venv/bin/python docs/tools/check_rewrite_preserved.py \
          [--rev HEAD] docs/index.rst docs/science/*.rst
Exit 0 = nothing structural dropped (numbers are advisory); 1 = a
label, link target or media file was dropped.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

ROLE = re.compile(r":(doc|ref|cfile|cfunc|clabel|commit|term):`([^`]+)`")
LABEL = re.compile(r"^\.\. _([^:]+):\s*$", re.M)
MEDIA = re.compile(
    r"^\s*\.\. (?:image|figure|graphviz|include|literalinclude)::\s*(\S+)",
    re.M)
SRC = re.compile(r'src="([^"]+)"')
NUM = re.compile(r"(?<![\w.])(\d[\d,]*(?:\.\d+)?)(?![\w])")


def target(raw: str) -> str:
    m = re.search(r"<([^>]+)>\s*$", raw)
    return (m.group(1) if m else raw).strip()


def facts(text: str) -> dict[str, Counter]:
    return {
        "labels": Counter(LABEL.findall(text)),
        "links": Counter(f"{r}:{target(t)}" for r, t in ROLE.findall(text)),
        "media": Counter(MEDIA.findall(text) + SRC.findall(text)),
        "numbers": Counter(n for n in NUM.findall(text)
                           if len(n.replace(",", "").replace(".", "")) >= 2),
    }


def old_text(rev: str, path: Path) -> str:
    rel = path.resolve().relative_to(REPO)
    r = subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{rel}"],
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else ""


def norm_doc(page: Path, key: str) -> str:
    """Make :doc: targets page-independent so a moved link still matches."""
    if not key.startswith("doc:"):
        return key
    t = key[4:]
    if t.startswith("/"):
        return "doc:" + t.lstrip("/")
    base = page.resolve().parent.relative_to(REPO / "docs")
    parts = []
    for seg in (base / t).parts:
        if seg == "..":
            if parts:
                parts.pop()
        elif seg != ".":
            parts.append(seg)
    return "doc:" + "/".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("pages", nargs="+", type=Path)
    a = ap.parse_args()

    before = {k: Counter() for k in ("labels", "links", "media", "numbers")}
    after = {k: Counter() for k in before}
    for p in a.pages:
        for side, text in ((before, old_text(a.rev, p)),
                           (after, p.read_text() if p.exists() else "")):
            f = facts(text)
            f["links"] = Counter({norm_doc(p, k): v
                                  for k, v in f["links"].items()})
            for k in side:
                side[k].update(f[k])

    hard = 0
    for kind in ("labels", "links", "media"):
        lost = sorted(set(before[kind]) - set(after[kind]))
        for item in lost:
            print(f"DROPPED {kind[:-1]}: {item}")
        hard += len(lost)
    lost_nums = sorted(set(before["numbers"]) - set(after["numbers"]))
    if lost_nums:
        print("advisory — numbers no longer present: " + ", ".join(lost_nums))
    print(f"{hard} structural drop(s), {len(lost_nums)} number(s) gone")
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
