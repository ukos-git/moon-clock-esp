#!/usr/bin/env python3
"""Documentation drift gate: single-sourced tables vs the live sources.

The reference tables (Kconfig, constants, partitions) and the UI
reachability matrix are *hand-kept-in-sync* fragments — they carry no
generator, so nothing catches a source edit that forgot to update the
doc. This script closes that hole: it parses the fragments and the
pages, parses the sources, and reports every disagreement.

Checks
------
1. ``docs/reference/_fragments/kconfig-*.inc`` vs the project Kconfigs
   (``components/{ui,clock,moon}/Kconfig``,
   ``components/board/Kconfig.projbuild``, ``knob/components/*/Kconfig``):
   every documented key exists, and every documented default is one of
   the source's ``default`` lines (first-match-wins allows several).
   Reverse coverage: every ``config`` symbol in a covered source
   appears in some fragment (allowlist below for deliberate omissions).
2. ``moon-consts.inc`` vs the firmware's truth for those constants:
   the S3 ``moon_consts.h`` generated header (rendered from
   ``constants.py`` by ``moon.assets``) plus the numeric ``#define`` /
   ``const`` homes in ``components/moon/core/*.c`` and ``include/*.h``
   (existence-only for arrays and expression macros).
3. ``partitions-*.inc`` vs the three partition CSVs (both the decimal
   and the hex spelling must agree with the CSV and with each other).
4. ``docs/ui/index.rst`` reachability matrix vs ``SCREEN_ID_*`` in
   ``components/ui/screens.h``, the page files, and the committed
   screenshots each page embeds (✓ rows need their S3/P4 annotation
   and, where ✓, the knob raw panel; — rows must not embed them).
5. ``docs/verification/binary.rst`` current-size rows vs the bytes on
   disk (skipped, not failed, when a build tree is absent).

Exit 0 = no drift; exit 1 = drift (each line printed as ``DRIFT:``).

Run:  docs/.venv/bin/python docs/tools/check_docs_drift.py
"""
from __future__ import annotations

import ast
import csv
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DOCS = REPO / "docs"
FRAG = DOCS / "reference" / "_fragments"

# fragment file(s) -> the Kconfig sources they document
KCONFIG_SOURCES = {
    "components/ui/Kconfig",
    "components/clock/Kconfig",
    "components/moon/Kconfig",
    "components/board/Kconfig.projbuild",
}
KNOB_SOURCES = {f"knob/components/{p}/Kconfig"
                for p in ("knob_ui", "knob_moon", "knob_time", "knob_power",
                          "board", "knob_battery")}
FRAGMENT_FOR = {  # fragment -> sources it alone must cover (coverage pass)
    "kconfig-ui.inc": ["components/ui/Kconfig"],
    "kconfig-clock.inc": ["components/clock/Kconfig"],
    "kconfig-moon.inc": ["components/moon/Kconfig"],
    "kconfig-board-choice.inc": ["components/board/Kconfig.projbuild"],
    "kconfig-knob.inc": sorted(KNOB_SOURCES),
}
# config symbols deliberately not documented (add with a one-line why)
KCONFIG_UNDOCUMENTED_OK: set[str] = set()

drifts: list[str] = []


def drift(msg: str) -> None:
    drifts.append(msg)


# ---------------------------------------------------------------- parsing

def table_rows(text: str) -> list[list[str]]:
    """Every list-table row as its cell list (multi-line cells joined)."""
    rows: list[list[str]] = []
    cur: list[str] | None = None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("* - "):
            if cur:
                rows.append(cur)
            cur = [s[4:].strip()]
        elif cur is not None and s.startswith("- "):
            cur.append(s[2:].strip())
        elif cur is not None and not s:
            if not cur:
                cur = None
            continue  # blank lines inside a cell's prose
        elif cur is not None and (s.startswith("..") or s.startswith(":")):
            rows.append(cur)
            cur = None
        elif cur:  # wrapped continuation of the last cell's prose
            cur[-1] += " " + s
    if cur:
        rows.append(cur)
    return rows


def backticked(cell: str) -> list[str]:
    return re.findall(r"``([^`]+)``", cell)


def norm_token(tok: str) -> str:
    """Normalize a Kconfig default or a doc cell value for comparison."""
    tok = tok.strip().strip("`").strip()
    tok = re.sub(r"#.*$", "", tok).strip()          # trailing comment
    tok = tok.strip('"').strip("'")
    m = re.match(r"^(0[xX][0-9a-fA-F]+|-?\d+(?:\.\d+)?)$", tok)
    if m:  # canonicalize numbers (0x14 == 20, 1.50 == 1.5)
        return str(float(m.group(1))) if "." in m.group(1) else str(
            int(m.group(1), 0))
    return tok


def parse_kconfig(path: Path) -> dict[str, list[str]]:
    """symbol -> normalized default values (all `default` lines)."""
    defaults: dict[str, list[str]] = {}
    sym: str | None = None
    for line in path.read_text().splitlines():
        m = re.match(r"^\s*config\s+(\w+)", line)
        if m:
            sym = m.group(1)
            defaults.setdefault(sym, [])
            continue
        if re.match(r"^\s*(choice|endchoice|menu|endmenu)\b", line):
            sym = None if re.match(r"^\s*menu\b", line) else sym
            continue
        m = re.match(r"^\s*default\s+(.+)$", line) if sym else None
        if m and "if" not in m.group(1).split()[:1]:
            defaults[sym].append(norm_token(m.group(1)))
    return defaults


def parse_size(text: str) -> int | None:
    """Parse '24 K (0x6000)', '2048 K', '0x1a0000', '24576' -> bytes."""
    hx = re.search(r"0[xX][0-9a-fA-F]+", text)
    dec = re.search(r"([\d.,]+)\s*([KM]?)\b", text)
    val = None
    if hx:
        val = int(hx.group(0), 16)
    if dec and dec.group(1).replace(",", "").replace(".", "").isdigit():
        n = int(dec.group(1).replace(",", ""))
        mult = {"": 1, "K": 1024, "M": 1024 * 1024}[dec.group(2)]
        n *= mult
        if val is not None and val != n:
            return -1  # the two spellings disagree with each other
        val = n if val is None else val
    return val


def parse_csv_sizes(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    for row in csv.reader(path.read_text().splitlines()):
        row = [c.split("#")[0].strip() for c in row]
        if len(row) < 5 or row[0] in ("Name", ""):
            continue
        size = row[4]
        m = re.fullmatch(r"0[xX][0-9a-fA-F]+|(\d+)([KMkm]?)", size)
        if not m:
            continue
        if size.lower().startswith("0x"):
            out[row[0]] = int(size, 16)
        else:
            out[row[0]] = int(m.group(1)) * {"": 1, "k": 1024, "m": 1024 * 1024}[
                m.group(2).lower()]
    return out


# ---------------------------------------------------------------- checks

def check_kconfig() -> None:
    all_sources = {}
    for rel in sorted(KCONFIG_SOURCES | KNOB_SOURCES):
        p = REPO / rel
        if not p.exists():
            drift(f"kconfig source missing: {rel}")
            continue
        all_sources[rel] = parse_kconfig(p)
    union_defaults: dict[str, list[str]] = {}
    for d in all_sources.values():
        for k, v in d.items():
            union_defaults.setdefault(k, []).extend(v)

    fragment_keys: dict[str, set[str]] = {}  # documented symbols per fragment
    for frag in FRAG.glob("kconfig-*.inc"):
        text = frag.read_text()
        documented: set[str] = set()
        for row in table_rows(text):
            key_cells = backticked(row[0])
            if not key_cells:
                continue
            key = key_cells[0]
            if key.upper() != key or "(choice" in row[0]:
                continue  # prose key or the choice line itself
            key = key.removeprefix("CONFIG_")
            documented.add(key)
            if key not in union_defaults:
                drift(f"{frag.name}: {key} documented but no such config in "
                      f"any project Kconfig")
                continue
            # default agreement: any candidate in the Default cell may match
            # any of the source's default lines (first-match-wins lists).
            if len(row) < 2:
                continue
            cands = {norm_token(t) for t in backticked(row[1])}
            src = union_defaults[key]
            if cands and src and not (cands & set(src)):
                drift(f"{frag.name}: {key} documented default {sorted(cands)} "
                      f"not among source defaults {src}")
        fragment_keys[frag.name] = documented

    # Coverage accepts any backticked name anywhere in the fragments —
    # choice options are documented inside their choice row's meaning.
    documented_anywhere: set[str] = set()
    for frag in FRAG.glob("kconfig-*.inc"):
        last = ""
        # ``KEY`` / ``_SUFFIX`` shorthand documents KEY_SUFFIX too
        for t in re.findall(r"``((?:[A-Z][A-Z0-9_]+)|_[A-Z][A-Z0-9_]+)``",
                            frag.read_text()):
            if t.startswith("_"):
                # ``DIV_NUM`` / ``_DEN`` documents DIV_DEN: replace the
                # parent's last segment with the shorthand's.
                parent = last.split("_")
                parent[-1] = t.lstrip("_")
                documented_anywhere.add("_".join(parent))
            else:
                documented_anywhere.add(t.removeprefix("CONFIG_"))
                last = t.removeprefix("CONFIG_")
    for frag, sources in FRAGMENT_FOR.items():
        for rel in sources:
            for sym in all_sources.get(rel, {}):
                if sym in KCONFIG_UNDOCUMENTED_OK or sym in documented_anywhere:
                    continue
                drift(f"{frag}: source symbol {sym} ({rel}) is undocumented")


def check_moon_consts() -> None:
    # The generated header is the firmware's truth (rendered from
    # constants.py by moon.assets); the fragment must match IT. The
    # S3 target's header carries the board-independent constants.
    sources = [REPO / "components/moon/generated/s3/moon_consts.h",
               *(REPO / "components/moon/core").glob("*.c"),
               *(REPO / "components/moon/include").glob("*.h")]
    if not sources[0].exists():
        print("note: s3 moon_consts.h absent (run the asset pipeline) — "
              "constant check skipped")
        return
    values: dict[str, float] = {}   # numeric literals (compared)
    present: set[str] = set()       # any spelling (existence only:
                                    # arrays, expression macros)
    for src in sources:
        text = src.read_text()
        for name, val in re.findall(r"#define\s+(MOON_\w+)\s+(\S+)", text):
            present.add(name)
            try:
                values[name] = float(val.rstrip("fF"))
            except ValueError:
                pass
        for name in re.findall(r"(?:static\s+)?const\s+[\w\s]+?\s+(MOON_\w+)\s*\[",
                               text):
            present.add(name)
    text = (FRAG / "moon-consts.inc").read_text()
    for row in table_rows(text):
        cells = backticked(row[0])
        if not cells or not cells[0].startswith("MOON_"):
            continue
        const = cells[0]
        if const not in present:
            drift(f"moon-consts.inc: {const} defined nowhere in the moon "
                  f"core (generated header, core/*.c, include/*.h)")
            continue
        if const not in values:
            continue  # expression macro / LUT array: existence only
        want = values[const]
        got_cells = backticked(row[1]) if len(row) > 1 else []
        ok = False
        for tok in got_cells:
            try:
                ok |= abs(float(tok) - want) <= 1e-9 * max(1.0, abs(want))
            except ValueError:
                pass
        if got_cells and not ok:
            drift(f"moon-consts.inc: {const} documented {got_cells}, "
                  f"generated header says {want!r}")


def check_partitions() -> None:
    csvs = {"s3": REPO / "partitions.csv",
            "p4": REPO / "partitions_p4.csv",
            "knob": REPO / "knob/partitions.csv"}
    for board, path in csvs.items():
        if not path.exists():
            drift(f"partition CSV missing: {path.relative_to(REPO)}")
            continue
        src = parse_csv_sizes(path)
        frag = FRAG / f"partitions-{board}.inc"
        documented: set[str] = set()
        for row in table_rows(frag.read_text()):
            names = backticked(row[0])
            if not names or len(row) < 3:
                continue
            name = names[0]
            documented.add(name)
            want = src.get(name)
            if want is None:
                drift(f"partitions-{board}.inc: {name} not in "
                      f"{path.relative_to(REPO)}")
                continue
            got = parse_size(row[2])
            if got == -1:
                drift(f"partitions-{board}.inc: {name} decimal and hex "
                      f"spellings disagree")
            elif got is not None and got != want:
                drift(f"partitions-{board}.inc: {name} documented "
                      f"{row[2]!r}, CSV says {want} B")
        for name in src:
            if name not in documented:
                drift(f"partitions-{board}.inc: CSV partition {name} "
                      f"undocumented")


def check_ui_matrix() -> None:
    enum = re.findall(r"(SCREEN_ID_\w+)",
                      (REPO / "components/ui/screens.h").read_text())
    enum = [e for e in enum if not e.endswith("_COUNT")]
    text = (DOCS / "ui/index.rst").read_text()
    rows = table_rows(text)
    matrix: dict[str, tuple[str, str, str]] = {}
    for row in rows:
        m = re.match(r":doc:`([^`]+) <([^`]+)>`", row[0])
        if not m or len(row) < 4:
            continue
        matrix[m.group(2)] = (row[1].strip(), row[2].strip(), row[3].strip())
    want = {e.removeprefix("SCREEN_ID_").lower().replace("_", "-") for e in enum}
    for slug in sorted(want - set(matrix)):
        drift(f"ui/index.rst: screen {slug} (screens.h) missing from matrix")
    for slug in sorted(set(matrix) - want):
        drift(f"ui/index.rst: matrix row {slug} has no SCREEN_ID_*")
    for slug, (s3, p4, knob) in matrix.items():
        page = DOCS / "ui" / f"{slug}.rst"
        if not page.exists():
            drift(f"ui/index.rst: page docs/ui/{slug}.rst missing")
            continue
        page_text = page.read_text()
        name = slug.replace("-", "_")
        for mark, img, label in (
                (s3, DOCS / "img/screens/annot" / f"{slug}_s3.png", "S3"),
                (p4, DOCS / "img/screens/annot" / f"{slug}_p4.png", "P4"),
                (knob, DOCS / "img/screens/knob" / f"{name}.png", "knob")):
            reached = "✓" in mark
            if reached and not img.exists():
                drift(f"{label}: screenshot missing for {slug} ({img.name})")
            if reached and img.name not in page_text:
                drift(f"docs/ui/{slug}.rst: matrix says {label} reachable "
                      f"but {img.name} is not embedded")
            if not reached and img.name in page_text:
                # A page may legitimately render an unreachable panel's
                # layout if it says so; it must carry an explicit
                #   .. drift-check:allow-panel <file>
                # marker for it.
                marker = f"drift-check:allow-panel {img.name}"
                if marker not in page_text:
                    drift(f"docs/ui/{slug}.rst: embeds {img.name} although the "
                          f"matrix says {label} unreachable (add a "
                          f"'.. drift-check:allow-panel {img.name}' marker "
                          f"if the shot is intentional)")
    # every knob/annotated screenshot is referenced by at least one page
    # — scanning ALL RST pages, not just docs/ui/: the wheel keyboard's
    # knob press shot lives on keyboard.rst, and any future page may
    # legitimately embed a screen shot
    pages = "\n".join(p.read_text() for p in DOCS.rglob("*.rst")
                      if "_build" not in p.parts)
    for img in sorted((DOCS / "img/screens/annot").glob("*.png")):
        if pages.count(img.name) < 1:
            drift(f"annot/{img.name}: committed but referenced by no page")
    for img in sorted((DOCS / "img/screens/knob").glob("*.png")):
        if img.name not in pages:
            drift(f"knob/{img.name}: committed but referenced by no page")


def check_binary_sizes() -> None:
    bins = {"S3": REPO / "build/moon-phase.bin",
            "P4": REPO / "build/p4/moon-phase.bin",
            "knob": REPO.parent / "build/knob/knob.bin"}
    text = (DOCS / "verification/binary.rst").read_text()
    present = {b: p for b, p in bins.items() if p.exists()}
    if not present:
        print("note: no build trees present — binary-size check skipped")
        return
    for board, p in present.items():
        size = p.stat().st_size
        if f"{size:,} B" not in text:
            drift(f"binary.rst: {board} binary on disk is {size:,} B but "
                  f"that number appears nowhere on the page")


def main() -> int:
    for fn in (check_kconfig, check_moon_consts, check_partitions,
               check_ui_matrix, check_binary_sizes):
        fn()
    for d in drifts:
        print(f"DRIFT: {d}")
    if drifts:
        print(f"\n{len(drifts)} drift(s) found")
        return 1
    print("docs drift check: fragments, matrix and sizes match the sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
