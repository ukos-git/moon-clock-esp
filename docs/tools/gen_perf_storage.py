#!/usr/bin/env python3
"""Generate the storage ladder for docs/verification/performance.rst — the
counting axis next to perf_bus (how many BYTES cross the wire), perf_pixels
(how many PIXELS every stage touches) and perf_mem (how much RAM): **how
many bytes lie in the flash**, per board, row by row. The binary page owns
the app side (binary_budget.png: app vs factory ceiling); the DATA side of
the flash budget — the albedo/DEM blob inside the moon_assets row, the
spiffs payload inside storage, the knob's coredump ring — has never been
counted. Nothing is measured and nothing is invented: every bar is parsed
at figure time or one product of parsed numbers, and the asset-pipeline
guards are RE-APPLIED here (the figure dies if the build-time
FATAL_ERROR trio is ever removed or the CSV comments drift from the rows).

Left panel — what each data row actually holds (fraction used):

  factory        the live .bin against the factory row (binary.rst's
                 numbers, same files — one truth source)
  moon_assets    stacked: albedo (MOON_ALB_W x H, u8) + relief DEM
                 (MOON_DEM_W x H, int8) — the manifest + the on-disk
                 .bin, both parsed, both asserted equal
  storage        the spiffs/ payload the image bakes (the CSV comment's
                 own "holds ~1KB", re-counted from the files)
  moon_movie     the S3's AND the P4's LAST row: the boot-movie blob —
                 the on-disk .bin vs the manifest's total_bytes, both
                 parsed, both asserted equal (the root CMake's movie
                 guard #2 prices the same pair at configure)
  coredump       the knob's 64 K ring — drawn italic, no bytes until a
                 crash (the *no such leg* precedent, honest: the space
                 is RESERVED, not filled)

Right panel — the moon_assets row, the product's tightest budget:

  three shipped bars (albedo + DEM stacked against each board's row,
  the row drawn as its ceiling line) and three what-if bars:
    S3 at albedo 1024  the next width in the root guard's own set
                       {864, 1024, ..., 4096} — the computed bar FILLS
                       THE ROW TO THE BYTE (headroom == the 151,040 B
                       the width change costs: asserted equal here)
    P4 at 2560-wide    the P4 CSV comment's own hypothetical — hatched
                       (cited), it overflows and the guard names the
                       KiB (5248K) — parsed AND re-derived, the comment
                       must keep its arithmetic
    knob with the clock's 2048x1024 DEM — hatched (cited from the knob
                       CSV's "sized for the proven 2048x1024-DEM bake
                       too"): the same combined number as today's S3
                       blob, 2,470,400 B — why the row never has to
                       change (a partition change = a full flash on the
                       bench unit, the comment's own reason)

Parsed at figure time (SystemExit-failing parsers, the house pattern):

  nvs/phy/factory/storage/moon_assets/moon_movie/coredump rows
      partitions.csv, partitions_p4.csv, knob/partitions.csv (sizes K or
      hex; the comment totals "Total = 16384K" (S3)/"Total = 32768K"
      (P4) and the knob's "~8.9/8.8 MB free after it" are parsed and
      re-derived)
  the S3 + P4 boot-movie blobs       assets/images/moon/out/{s3,p4}/
      moon_movie.bin (on-disk size) + moon_movie_manifest.json
      (total_bytes — asserted equal; the blob must fit its board's
      moon_movie row, the root CMake's movie guard #2 asserted the
      same way)
  alb_w/h, dem_w/h, *_bytes, combined_bytes, partition_kib
      assets/images/moon/out/{s3,p4,knob}/moon_assets_manifest.json —
      each asserted u8-albedo / i8-DEM product, file size, sum, and
      equality with the generated header AND the CSV row
  MOON_ALB_W/H + MOON_DEM_W/H      components/moon/generated/{t}/moon_consts.h
      (albedo asserted 2:1 equirect, the same shape asserts as the
      sharpness funnel)
  CONFIG_MOON_ALBEDO_W             build/config/sdkconfig.h + build/p4/
      config/sdkconfig.h (the guard's twin; the knob deliberately has
      NO Kconfig mirror — asserted absent from its CMakeLists, the
      documented asymmetry)
  the FATAL_ERROR trio             CMakeLists.txt ("must be an even
      width", the manifest-width equality, the partition fit) + the
      knob's replicant ("Missing knob moon assets", its fit guard) —
      the figure refuses to draw if a guard is removed
  spiffs payload                   spiffs/ file sizes; settings.txt must
      be ABSENT (CLAUDE.md's rule: the app owns it at runtime)
  the P4 2560-wide hypothetical    partitions_p4.csv comment (claimed
      albedo/DEM/sum + "need >= 5248K") — re-derived from the layout
      rule, the comment's arithmetic is checked, not trusted
  the knob 2048x1024-DEM sizing    knob/partitions.csv comment ("albedo
      864x432 u8 then relief DEM 1024x512 i8 = 897,536 B"; "sized for
      the proven 2048x1024-DEM bake too")
  binary sizes                     build/moon-phase.bin, build/p4/
      moon-phase.bin, ../build/knob/knob.bin (check_docs_drift.py's
      exact paths — one truth with the binary page's rows)

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet. albedo amber / DEM orange (the relief colours of the pixel
ladder). Hatched = cited (the bus ladder's precedent); italic = the
honest *no such leg*.

Run:  docs/.venv/bin/python docs/tools/gen_perf_storage.py
"""
from __future__ import annotations

import json
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO_PARENT = os.path.dirname(ROOT)     # the knob builds OUTSIDE the repo
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_storage.png")

TARGETS = ("s3", "p4", "knob")
CSV = {
    "s3": os.path.join(ROOT, "partitions.csv"),
    "p4": os.path.join(ROOT, "partitions_p4.csv"),
    "knob": os.path.join(ROOT, "knob", "partitions.csv"),
}
BIN = {
    "s3": os.path.join(ROOT, "build", "moon-phase.bin"),
    "p4": os.path.join(ROOT, "build", "p4", "moon-phase.bin"),
    "knob": os.path.join(REPO_PARENT, "build", "knob", "knob.bin"),
}
MANIFEST = {t: os.path.join(ROOT, "assets", "images", "moon", "out", t,
                            "moon_assets_manifest.json") for t in TARGETS}
BLOB = {t: os.path.join(ROOT, "assets", "images", "moon", "out", t,
                        "moon_assets.bin") for t in TARGETS}
CONSTS = {t: os.path.join(ROOT, "components", "moon", "generated", t,
                          "moon_consts.h") for t in TARGETS}
SDKCFG = {
    "s3": os.path.join(ROOT, "build", "config", "sdkconfig.h"),
    "p4": os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h"),
}
ROOT_CMAKE = os.path.join(ROOT, "CMakeLists.txt")
KNOB_CMAKE = os.path.join(ROOT, "knob", "CMakeLists.txt")
SPIFFS = os.path.join(ROOT, "spiffs")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # S3 identity
VERM = "#c2410c"     # P4 identity
VIOLET = "#7c3aed"   # knob identity
AMBER = "#b07d10"    # albedo (the relief colours of the pixel ladder)
ORANGE = "#b5643a"   # int8 DEM
STEEL = "#5c7fb8"    # what-if bars (computed, not shipped)


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — this "
                         f"figure reads the build trees, the manifests and "
                         f"the blobs; build the targets and run the asset "
                         f"pipeline first")
    with open(path, encoding="utf-8") as f:
        return f.read()


def _n(s: str) -> int:
    """'2,470,400' / '2048K' / '0x6000' -> bytes."""
    s = s.strip().replace(",", "")
    if s.lower().startswith("0x"):
        return int(s, 16)
    if s.upper().endswith("K"):
        return int(s[:-1]) * 1024
    return int(s)


def must_t(pattern: str, text: str, what: str, flags: int = 0) -> re.Match:
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"{what}: pattern not found")
    return m


def must(pattern: str, path: str, what: str, flags: int = 0) -> re.Match:
    return must_t(pattern, _read(path),
                  f"{what} in {os.path.relpath(path, ROOT)}", flags)


def _comments(path: str) -> str:
    """A CSV's comment block, '#' bars stripped and one line: the claims
    wrap over several '#'-prefixed lines in the files, so flattening once
    lets one regex read a whole claim without regex line noise."""
    return " ".join(line.strip().lstrip("#").strip()
                    for line in _read(path).splitlines()
                    if line.strip().startswith("#"))


def partition_rows(path: str) -> dict[str, int]:
    """The data rows of a partition CSV: name -> bytes (comments stripped)."""
    rows = {}
    for line in _read(path).splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5 or not cols[0]:
            continue
        rows[cols[0]] = _n(cols[4])
    for name in ("nvs", "phy_init", "factory", "storage", "moon_assets"):
        if name not in rows:
            raise SystemExit(f"{os.path.relpath(path, ROOT)}: no {name} row")
    return rows


def hdr_define(path: str, symbol: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.relpath(path, ROOT)}")
    return int(m.group(1))


# ------------------------------------------------------------------ rows
rows = {t: partition_rows(CSV[t]) for t in TARGETS}
for t in TARGETS:                                    # the fixed-64K shape
    if rows[t]["nvs"] != 24 * 1024 or rows[t]["phy_init"] != 4 * 1024:
        raise SystemExit(f"{os.path.relpath(CSV[t], ROOT)}: nvs/phy rows no "
                         f"longer 24K/4K — the fixed 64K assumption (and the "
                         f"comment totals) need re-deriving here")
coredump_bytes = rows["knob"].get("coredump")
if coredump_bytes != 64 * 1024:
    raise SystemExit("knob coredump row is no longer 64K — re-read task #27")

# comment totals: "Total = 16384K" (S3) / "Total = 32768K" (P4) —
# both tables fully allocated (the moon_movie LAST row owns each
# chip's leftover), the CSV's own arithmetic, re-derived from the
# rows (64K fixed prefix + every row but nvs/phy, which sit inside
# that prefix)
for t in ("s3", "p4"):
    m = must(r"Total = (\d+)K", CSV[t], f"{t} comment total")
    want = 64 + sum(v // 1024 for k, v in rows[t].items()
                    if k not in ("nvs", "phy_init"))
    if int(m.group(1)) != want:
        raise SystemExit(f"{os.path.relpath(CSV[t], ROOT)}: comment total "
                         f"{m.group(1)}K != computed {want}K — the comment "
                         f"and the rows disagree")
# the knob: "~8.9 MB free after it" before the coredump, "~8.8 MB" after —
# both parsed, both re-derived (16 MB chip)
knob_free = [float(x) for x in re.findall(r"~([\d.]+) MB free after it",
                                          _comments(CSV["knob"]))]
if len(knob_free) != 2:
    raise SystemExit("knob CSV: expected the two '~x.x MB free after it' "
                     "comments (before/after the coredump row)")
free_no_cd = (16 * 1024 - 64 - sum(rows["knob"][r] // 1024 for r in
                                   ("factory", "storage", "moon_assets"))) / 1024
free_cd = free_no_cd - coredump_bytes / (1024 * 1024)
for claimed, want in zip(knob_free, (free_no_cd, free_cd)):
    if abs(claimed - want) > 0.1:
        raise SystemExit(f"knob CSV comment claims ~{claimed} MB free, "
                         f"the rows leave {want:.3f} MB — fix the comment")

# ------------------------------------------------------- manifests/blobs/consts
mani = {}
for t in TARGETS:
    d = json.loads(_read(MANIFEST[t]))
    for k in ("alb_w", "alb_h", "alb_bytes", "dem_w", "dem_h", "dem_bytes",
              "combined_bytes", "partition_kib", "target"):
        if k not in d:
            raise SystemExit(f"{t} manifest: no {k}")
    if d["target"] != t:
        raise SystemExit(f"{t} manifest names target {d['target']}")
    # the layout contract (moon_assets.h): albedo u8 equirect 2:1, DEM int8
    if d["alb_bytes"] != d["alb_w"] * d["alb_h"]:
        raise SystemExit(f"{t}: alb_bytes != alb_w*alb_h — the u8-albedo "
                         f"layout rule changed; re-read moon_assets.h")
    if d["alb_h"] * 2 != d["alb_w"]:
        raise SystemExit(f"{t}: albedo is no longer 2:1 equirect")
    if d["dem_bytes"] != d["dem_w"] * d["dem_h"]:
        raise SystemExit(f"{t}: dem_bytes != dem_w*dem_h — the int8-DEM "
                         f"layout rule changed")
    if d["combined_bytes"] != d["alb_bytes"] + d["dem_bytes"]:
        raise SystemExit(f"{t}: combined_bytes != albedo + DEM")
    if os.path.getsize(BLOB[t]) != d["combined_bytes"]:
        raise SystemExit(f"{t}: moon_assets.bin on disk is "
                         f"{os.path.getsize(BLOB[t])} B, the manifest says "
                         f"{d['combined_bytes']} B — regenerate the blob")
    # manifest vs the GENERATED header (the sharpness funnel's same source)
    for k, sym in (("alb_w", "MOON_ALB_W"), ("alb_h", "MOON_ALB_H"),
                   ("dem_w", "MOON_DEM_W"), ("dem_h", "MOON_DEM_H")):
        got = hdr_define(CONSTS[t], sym)
        if got != d[k]:
            raise SystemExit(f"{t}: manifest {k}={d[k]} but generated "
                             f"{os.path.basename(CONSTS[t])} {sym}={got}")
    # manifest vs the partition CSV the build actually flashes against
    if d["partition_kib"] * 1024 != rows[t]["moon_assets"]:
        raise SystemExit(f"{t}: manifest partition_kib {d['partition_kib']}K "
                         f"!= moon_assets row {rows[t]['moon_assets']} B")
    # the guard's third: the blob fits the row (the build FATAL_ERRORs at
    # configure if not — this figure refuses to draw the same failure)
    if d["combined_bytes"] > rows[t]["moon_assets"]:
        raise SystemExit(f"{t}: blob {d['combined_bytes']} B overflows the "
                         f"{rows[t]['moon_assets']} B moon_assets row")
    mani[t] = d

# Kconfig widths (the root guard's twin): CONFIG_MOON_ALBEDO_W must equal
# the manifest's alb_w for both clocks; the knob has NO Kconfig mirror —
# its CMakeLists must not mention the symbol (the documented asymmetry).
for t in ("s3", "p4"):
    got = hdr_define(SDKCFG[t], "CONFIG_MOON_ALBEDO_W")
    if got != mani[t]["alb_w"]:
        raise SystemExit(f"{t}: CONFIG_MOON_ALBEDO_W={got} but the blob is "
                         f"baked at alb_w={mani[t]['alb_w']} — exactly what "
                         f"the root's manifest-equality FATAL_ERROR guards")
if "CONFIG_MOON_ALBEDO_W" in _read(KNOB_CMAKE):
    raise SystemExit("knob/CMakeLists.txt now checks CONFIG_MOON_ALBEDO_W — "
                     "the documented 'no Kconfig mirror' asymmetry changed; "
                     "re-read knob/CMakeLists.txt:51-54")

# the guards themselves must still EXIST (the figure is the docs' twin of
# the build-time trio — a removed guard must not keep drawing quietly)
must(r"must be an even width", ROOT_CMAKE, "root even-width guard")
must(r"but the Kconfig is MOON_ALBEDO_W", ROOT_CMAKE, "root manifest-width guard")
# the root's fit message is SPLIT across two CMake string literals
# ("... do not " / "fit the ..."), so quotes may sit between the words
must(r'do not\s*["\s]*fit the', ROOT_CMAKE, "root partition-fit guard", re.I)
must(r"Missing knob moon assets", KNOB_CMAKE, "knob manifest-existence guard")
must(r"do not fit the moon_assets partition", KNOB_CMAKE, "knob fit guard")

# spiffs payload: the files the storage image bakes (FLASH_IN_PROJECT);
# settings.txt must be ABSENT — the app owns it at runtime (CLAUDE.md)
files = sorted(f for f in os.listdir(SPIFFS)
               if os.path.isfile(os.path.join(SPIFFS, f)))
if "settings.txt" in files:
    raise SystemExit("spiffs/ contains settings.txt — CLAUDE.md: the app "
                     "owns it at runtime; it must not ship in the image")
spiffs_bytes = sum(os.path.getsize(os.path.join(SPIFFS, f)) for f in files)
for t in ("s3", "p4"):   # both clock CSVs claim the payload is "~1KB"
    m = must_t(r"holds ~(\d+)KB", _comments(CSV[t]), f"{t} spiffs claim")
    if not (0.5 * 1024 <= spiffs_bytes <= int(m.group(1)) * 1024 + 1024):
        raise SystemExit(f"{t}: spiffs/ payload is {spiffs_bytes} B, the "
                         f"CSV comment claims ~{m.group(1)}KB — fix one")
    # and it must sit inside the row it lands in
    if spiffs_bytes > rows[t]["storage"]:
        raise SystemExit(f"spiffs payload {spiffs_bytes} B overflows the "
                         f"{rows[t]['storage']} B storage row")

binbytes = {t: os.path.getsize(BIN[t]) for t in TARGETS}
for t in TARGETS:
    if binbytes[t] > rows[t]["factory"]:
        raise SystemExit(f"{t}: binary {binbytes[t]} B overflows the factory "
                         f"row — build fails, this figure cannot help")

# --------------------------------------------------- what-if arithmetic
# S3: the next width in the root guard's own set {864, 1024, ..., 4096}:
# the albedo grows 864x432 -> 1024x512 (both u8); the claim the figure
# draws is that the COST equals S3's remaining headroom — asserted here.
s3_next = 1024
s3_next_alb = s3_next * (s3_next // 2)
s3_cost = s3_next_alb - mani["s3"]["alb_bytes"]
s3_head = rows["s3"]["moon_assets"] - mani["s3"]["combined_bytes"]
if s3_cost != s3_head:
    raise SystemExit(f"the 'fills to the byte' claim broke: going 864->"
                     f"{s3_next} costs {s3_cost} B but the S3 row leaves "
                     f"{s3_head} B — the row or the widths changed under "
                     f"the figure; re-derive the claim (or the row)")
s3_whatif = mani["s3"]["combined_bytes"] + s3_cost

# P4: the CSV comment's own hypothetical — claimed numbers parsed AND
# re-derived from the layout rule (a stale comment must not keep drawing).
m = must_t(r"A (\d+)-wide albedo \(([\d,]+) B albedo \+ ([\d,]+) B DEM = "
           r"([\d,]+) B\) would\s+need >= (\d+)K", _comments(CSV["p4"]),
           "P4 2560-wide hypothetical comment", re.I)
w2 = int(m.group(1))
p2_claim = (_n(m.group(2)), _n(m.group(3)), _n(m.group(4)), int(m.group(5)))
p2_want = (w2 * (w2 // 2),                          # u8 albedo, 2:1
           mani["p4"]["dem_bytes"],                 # same int8 DEM
           w2 * (w2 // 2) + mani["p4"]["dem_bytes"],
           -(-p2_claim[2] // 1024))                 # ceil -> KiB needed
if p2_claim != p2_want:
    raise SystemExit(f"partitions_p4.csv comment claims {p2_claim} for a "
                     f"{w2}-wide albedo; the layout rule computes {p2_want} "
                     f"— fix the comment (the figure checks its arithmetic)")
if p2_want[3] <= rows["p4"]["moon_assets"] // 1024:
    raise SystemExit("the P4 hypothetical no longer overflows the row — the "
                     "comment's premise is stale, re-read it")

# the SHIPPED blob claims in both clock CSVs, re-derived the same way —
# binary.rst quotes the S3 one verbatim, so a drift here is a doc drift
for t in ("s3", "p4"):
    m = must_t(r"(\d+)x(\d+) albedo \+ (\d+)x(\d+) DEM = ([\d,]+) B < "
               r"([\d,]+) B", _comments(CSV[t]), f"{t} shipped blob comment")
    d = mani[t]
    if (int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)),
            _n(m.group(5)), _n(m.group(6))) != (
            d["alb_w"], d["alb_h"], d["dem_w"], d["dem_h"],
            d["combined_bytes"], rows[t]["moon_assets"]):
        raise SystemExit(f"{os.path.relpath(CSV[t], ROOT)}: shipped-blob "
                         f"comment claims {m.groups()} but the manifest/row "
                         f"say {d['alb_w']}x{d['alb_h']} + {d['dem_w']}x"
                         f"{d['dem_h']} = {d['combined_bytes']} < "
                         f"{rows[t]['moon_assets']} — fix the comment")
m = must_t(r"relief DEM \(([\d,]+) B\)", _comments(CSV["s3"]),
           "S3 DEM-wall comment (binary.rst quotes it)")
if _n(m.group(1)) != mani["s3"]["dem_bytes"]:
    raise SystemExit(f"partitions.csv claims the relief DEM is "
                     f"{m.group(1)} B; the S3 manifest says "
                     f"{mani['s3']['dem_bytes']} B — fix the comment")

# the moon_movie rows (LAST, S3 and P4): the boot-movie blobs — the
# on-disk .bin sizes and the manifests' total_bytes, both parsed, both
# asserted equal (the root CMake's movie guard #2 prices the same pair
# at configure; guard #1 keeps the whole table inside the chip, the
# figure's own total re-derivation above is that arithmetic). The P4's
# row starts past the old 16 MB mark — the 32 MB bump prices it.
movies = {}
for _t in ("s3", "p4"):
    _d = os.path.join(ROOT, "assets", "images", "moon", "out", _t)
    _m = json.loads(_read(os.path.join(_d, "moon_movie_manifest.json")))
    _b = os.path.getsize(os.path.join(_d, "moon_movie.bin"))
    if _m.get("target") != _t:
        raise SystemExit(f"{_d}/moon_movie_manifest.json names target "
                         f"{_m.get('target')!r} — regenerate for {_t}")
    if _m.get("total_bytes") != _b:
        raise SystemExit(f"{_t} moon_movie.bin on disk is {_b} B, the "
                         f"manifest says {_m.get('total_bytes')} — "
                         f"regenerate the movie")
    if _b > rows[_t]["moon_movie"]:
        raise SystemExit(f"{_t} moon_movie.bin {_b} B overflows the "
                         f"{_t.upper()}'s {rows[_t]['moon_movie']} B "
                         f"moon_movie row — exactly what the root "
                         f"CMake's movie guard #2 FATAL_ERRORs")
    movies[_t] = {"mani": _m, "bytes": _b}
movie_bytes = movies["s3"]["bytes"]
mmani = movies["s3"]["mani"]

# knob: "2560K is sized for the proven 2048x1024-DEM bake too" — the DEM
# size parsed FROM THE COMMENT, the projection computed from the layout.
m = must_t(r"albedo (\d+)x(\d+) u8 then relief DEM (\d+)x(\d+) i8 = "
           r"([\d,]+) B", _comments(CSV["knob"]), "knob blob layout comment")
if (_n(m.group(5)) != mani["knob"]["combined_bytes"]
        or int(m.group(1)) != mani["knob"]["alb_w"]
        or int(m.group(2)) != mani["knob"]["alb_h"]
        or int(m.group(3)) != mani["knob"]["dem_w"]
        or int(m.group(4)) != mani["knob"]["dem_h"]):
    raise SystemExit("knob blob layout comment drifted from the manifest")
m = must_t(r"sized for the proven (\d+)x(\d+)-DEM bake",
           _comments(CSV["knob"]), "knob pre-sizing comment")
knob_whatif = mani["knob"]["alb_bytes"] + int(m.group(1)) * int(m.group(2))
if knob_whatif != mani["s3"]["combined_bytes"]:
    raise SystemExit("the knob's pre-sized projection no longer equals the "
                     "S3's shipped blob — the 'same numbers' claim broke")
if knob_whatif > rows["knob"]["moon_assets"]:
    raise SystemExit("the knob row no longer fits the proven bake — the "
                     "pre-sizing claim is false")

for t in TARGETS:
    print(f"{t:>4}: bin {binbytes[t]:>9,} / {rows[t]['factory']:>9,} | "
          f"blob {mani[t]['combined_bytes']:>9,} / {rows[t]['moon_assets']:>9,} "
          f"(albedo {mani[t]['alb_bytes']:>9,} + dem {mani[t]['dem_bytes']:>9,}) | "
          f"spiffs {spiffs_bytes:>4} / {rows[t]['storage']:>6,}")
for _t in ("s3", "p4"):
    print(f"  {_t}: movie {movies[_t]['bytes']:>9,} / {rows[_t]['moon_movie']:>9,} "
          f"({movies[_t]['mani'].get('frame_count')} frames @ "
          f"{movies[_t]['mani'].get('hours_per_frame')} h)")
print(f"claim: S3 headroom {s3_head:,} B == 864->{s3_next} albedo cost; "
      f"P4 2560-wide needs {p2_want[3]}K vs {rows['p4']['moon_assets']//1024}K; "
      f"knob pre-sized {knob_whatif:,} B == S3's shipped {mani['s3']['combined_bytes']:,} B")

# ------------------------------------------------------------------ figure
plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})
fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(13.6, 5.4), dpi=150,
                                 gridspec_kw={"width_ratios": [1.0, 1.32]})
COLOR = {"s3": BLUE, "p4": VERM, "knob": VIOLET}

# ---- left: what each data row actually holds (fraction used)
bar_rows = [("s3", "factory"), ("s3", "moon_assets"), ("s3", "storage"),
            ("s3", "moon_movie"),
            ("p4", "factory"), ("p4", "moon_assets"), ("p4", "storage"),
            ("p4", "moon_movie"),
            ("knob", "factory"), ("knob", "moon_assets"), ("knob", "storage"),
            ("knob", "coredump")]
used = {}
for (t, r), y in zip(bar_rows, range(len(bar_rows))):
    if r == "factory":
        used[(t, r)] = [(binbytes[t], COLOR[t], f"{COLOR[t]}")]
    elif r == "moon_assets":
        used[(t, r)] = [(mani[t]["alb_bytes"], AMBER, "albedo"),
                        (mani[t]["dem_bytes"], ORANGE, "int8 DEM")]
    elif r == "moon_movie":
        used[(t, r)] = [(movies[t]["bytes"], COLOR[t], "boot movie (q90 JPEG)")]
    elif r == "storage":
        used[(t, r)] = [(spiffs_bytes, STEEL, "spiffs payload")]
    else:
        used[(t, r)] = [(0, "#ffffff", "no bytes until a crash")]
    left = 0
    for nb, col, _ in used[(t, r)]:
        ax_l.barh(y, nb / rows[t][r], left=left / rows[t][r], height=0.62,
                  color=col, edgecolor="#ffffff", linewidth=0.6,
                  hatch="///" if (t, r) == ("knob", "coredump") else None,
                  zorder=3)
        left += nb
    tot = left
    frac = tot / rows[t][r]
    lbl = (f"{tot:,} / {rows[t][r]:,} B"
           f"  ({(1 - frac) * 100:.0f} % free)"
           if r != "coredump" else f"reserved {rows[t]['coredump']:,} B")
    style = {"fontsize": 7, "color": INK, "va": "center"}
    if (t, r) == ("knob", "coredump"):
        ax_l.text(0.01, y, lbl, fontstyle="italic", color=MUTED, fontsize=7,
                  va="center", transform=ax_l.get_yaxis_transform())
    else:
        ax_l.text(frac + 0.012, y, lbl, **style)
ax_l.set_yticks(range(len(bar_rows)))
ax_l.set_yticklabels([f"{t}  {r}" for t, r in bar_rows], fontsize=7.5)
ax_l.invert_yaxis()
# xlim beyond 1.0 so the "used / reserved" annotations beside full bars
# never clip; the vertical line at 1.0 IS the partition row's ceiling
ax_l.set_xlim(0, 1.72)
ax_l.set_xticks([0, 0.25, 0.5, 0.75, 1.0])
ax_l.set_xticklabels(["0", "25 %", "50 %", "75 %", "100 %"], fontsize=7)
ax_l.axvline(1.0, color=MUTED, lw=1.0, zorder=2)
for (t, r) in [("s3", "moon_assets"), ("p4", "factory"), ("knob", "moon_assets")]:
    y = bar_rows.index((t, r))
    ax_l.axhspan(y - 0.5, y + 0.5, color="#f5f3ec" if (t, r) != ("s3", "moon_assets") else "#fdf0e6", zorder=0)
s3_moon_pct = mani["s3"]["combined_bytes"] / rows["s3"]["moon_assets"] * 100
s3_app_pct = binbytes["s3"] / rows["s3"]["factory"] * 100
ax_l.set_title(f"left: what each data row actually holds — fraction of the "
               f"partition row used\n(the S3 moon_assets row is the product's "
               f"tightest budget at {s3_moon_pct:.1f} % — above the S3 app's "
               f"{s3_app_pct:.0f} %)", loc="left")
ax_l.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_l.set_axisbelow(True)
for s in ("top", "right"):
    ax_l.spines[s].set_visible(False)

# ---- right: the moon_assets row, the tightest budget
p4_free = ((rows["p4"]["moon_assets"] - mani["p4"]["combined_bytes"])
           / rows["p4"]["moon_assets"] * 100)
knob_blob_free = ((rows["knob"]["moon_assets"] - mani["knob"]["combined_bytes"])
                  / rows["knob"]["moon_assets"] * 100)
right = [
    ("S3 shipped", "s3", mani["s3"]["alb_bytes"], mani["s3"]["dem_bytes"],
     f"{mani['s3']['combined_bytes']:,} B in a {rows['s3']['moon_assets']:,} B "
     f"row — {s3_head:,} B free", False),
    ("S3 albedo 1024 (computed)", "s3", s3_next_alb, mani["s3"]["dem_bytes"],
     "fills the row TO THE BYTE", False),
    ("P4 shipped", "p4", mani["p4"]["alb_bytes"], mani["p4"]["dem_bytes"],
     f"{mani['p4']['combined_bytes']:,} B in a {rows['p4']['moon_assets']:,} B "
     f"row — {p4_free:.0f} % free", False),
    ("P4 albedo 2560 (cited comment)", "p4", p2_want[0], p2_want[1],
     f"{p2_want[2]:,} B — needs {p2_want[3]}K, over row", True),
    ("knob shipped", "knob", mani["knob"]["alb_bytes"], mani["knob"]["dem_bytes"],
     f"{mani['knob']['combined_bytes']:,} B in a {rows['knob']['moon_assets']:,} B "
     f"row — {knob_blob_free:.0f} % free", False),
    ("knob with the clock's 2048x1024 DEM (cited comment)", "knob",
     mani["knob"]["alb_bytes"], int(m.group(1)) * int(m.group(2)),
     f"{knob_whatif:,} B — equals the S3's blob", True),
]
ys = list(range(len(right)))
for (label, t, ab, db, note, cited), y in zip(right, ys):
    ax_r.barh(y, ab, height=0.58, color=AMBER, edgecolor="#ffffff",
              linewidth=0.6, hatch="///" if cited else None, zorder=3)
    ax_r.barh(y, db, left=ab, height=0.58, color=ORANGE, edgecolor="#ffffff",
              linewidth=0.6, hatch="///" if cited else None, zorder=3)
    rowb = rows[t]["moon_assets"]
    ax_r.plot([rowb, rowb], [y - 0.44, y + 0.44], color=COLOR[t], lw=1.6, zorder=4)
    ax_r.text(ab + db + 60_000, y, note, fontsize=6.4, color=INK, va="center")
ax_r.set_yticks(ys)
ax_r.set_yticklabels([r[0] for r in right], fontsize=7.2)
ax_r.invert_yaxis()
ax_r.set_xlim(0, 7_000_000)
ax_r.set_xticks([0, 2e6, 4e6, 6e6])
ax_r.set_xticklabels(["0", "2 M", "4 M", "6 M"], fontsize=7)
ax_r.set_xlabel("bytes in the moon_assets partition (albedo amber, int8 DEM orange)",
                fontsize=7.5)
ax_r.set_title("right: the moon_assets row — albedo + DEM against the row "
               "tick (the board's colour)\nthe CSV comments keep their "
               "arithmetic only because this figure re-derives them",
               loc="left")
ax_r.grid(axis="x", color=HAIR, linewidth=0.6, zorder=0)
ax_r.set_axisbelow(True)
for s in ("top", "right"):
    ax_r.spines[s].set_visible(False)

fig.suptitle("The storage ladder: how many bytes lie in the flash, row by row — "
             "the app side is binary.rst; this is the data side",
             fontsize=9.5, color=INK, y=0.985)
fig.tight_layout(rect=(0, 0, 1, 0.965))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=150)
print(f"wrote {OUT}")
