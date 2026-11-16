#!/usr/bin/env python3
"""Generate the glyph ladder for docs/verification/performance.rst — the
typographic axis no other ladder prices: **how many glyphs every panel
carries, what the 4 bpp rasters cost, and what the firmware statically
asks for** (the assembly ladder counts OBJECTS, the tap ladder counts
TAPS, the silicon ladder counts CHIP ASKS — nobody counted GLYPHS: the
21 lv_font_conv bakes (7 faces × 3 per-panel tiers) that paint every
word of the UI).

Nothing is recalled. Every number is parsed from the live sources at
figure time with SystemExit-failing parsers (the house pattern), and
the parsers double as docs-side twins of the type system's structural
rules — the figure refuses to draw when a face appears in one tier
only, when a charset drifts between tiers, when a face falls out of
favour with the askers, or when an ask escapes a face's charset:

  21 files               7 faces × 3 tiers under the IDENTICAL symbol
      names (fonts.h externs exactly the 7); every file's lv_font_conv
      header: Size == --size, Bpp == 4, --no-compress present
  px triples             parsed == the triples devices/p4.rst and
      devices/knob.rst and ui/index.rst assert (96/48/32, 148/74/50,
      74/37/25) — page-vs-file drift kills the figure
  Opts charset           byte-equal across tiers for six of the seven
      faces (only --size differs); the regular icon face is the parsed
      exception: S3 {62286,61830} vs P4+knob {62286,61830,61463,…}
  carried glyph sets     the /* U+XXXX */ bitmap annotations, per file
      (12 / 13 / 95 / 95 / 12 / 7 / 2-3); the 12-glyph weekday face is
      derived FROM the two weekday tables (s_day_abbr actions.c:1070,
      ALARM_DAY_ABBR screens.c:1053 plus '-') and asserted EQUAL to
      the baked set, both directions
  raster bytes           the glyph_bitmap[] hex token count (raw 4 bpp,
      --no-compress); must be > 0 and strictly monotone in px per face
  askers                 a repo-wide scan for &ui_font_ must hit
      EXACTLY components/ui/screens.c (46) + styles.c (4) with the
      parsed per-face reference counts equal to the table
      (12/7/15/3/2/6/5 across the faces); every face asked ≥ 1
  hub ring               7 unconditional entries + exactly 1 under
      #if defined(CONFIG_BOARD_P4) (clock → settings-time); the ring's
      per-board asked glyph sets ⊆ the tier's carried icon sets
  GLYPH defines          UTF-8 escape bytes decoded == the U+XXXX the
      define's own comment claims; asked icon glyphs ⊆ carried
  wheel registers        all four s_wheel_sets strings stay inside the
      32 SSID face's carried set (printable ASCII) — an escaping
      register char is a SystemExit
  choosers               FONT_DIR ladder tokens in
      components/ui/CMakeLists.txt; --panel map tokens in
      host_sim/uisim/build.sh
  foundry                the four source faces exist under assets/fonts/

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet; the static-asked markers are steel, the never-drawn inherited
glyph is the one red strike.

Run:  docs/.venv/bin/python docs/tools/gen_perf_glyphs.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
UI = os.path.join(ROOT, "components", "ui")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_glyphs.png")

TIERS = {"s3": ("fonts", "#2b5fb0", "S3 466"),
         "p4": ("fonts_p4", "#c2410c", "P4 720"),
         "knob": ("fonts_knob", "#7c3aed", "knob 360")}

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
STEEL = "#8a8a8a"
RED = "#b91c1c"
AMBER = "#b07d10"

# The seven faces: short label, filename stem, expected per-tier px
# (parsed and asserted, never recalled from this dict alone — the dict
# IS the drift gate the sources are checked against), expected carried
# codepoint sets per tier where fixed, and the static-ask kind.
FACES = [
    ("96 numbers",  "ui_font_montserrat_bold_96_numbers",
     {"s3": 96, "p4": 148, "knob": 74}, None, "runtime"),
    ("48 numbers",  "ui_font_montserrat_regular_48_numbers",
     {"s3": 48, "p4": 74, "knob": 37}, None, "runtime"),
    ("32 SSID",     "ui_font_montserrat_regular_32_ssid",
     {"s3": 32, "p4": 50, "knob": 25}, None, "registers"),
    ("48 SSID",     "ui_font_montserrat_regular_48_ssid",
     {"s3": 48, "p4": 74, "knob": 37}, None, "runtime"),
    ("32 range",    "ui_font_montserrat_regular_32_range",
     {"s3": 32, "p4": 50, "knob": 25}, None, "weekday"),
    ("icons solid", "ui_font_fontawesome7_solid_96_icons",
     {"s3": 96, "p4": 148, "knob": 74}, None, "glyphs-solid"),
    ("icons regular", "ui_font_fontawesome7_regular_96_icons",
     {"s3": 96, "p4": 148, "knob": 74}, None, "glyphs-regular"),
]
FACE_NAMES = [f[1] for f in FACES]

# Parsed-and-asserted ask counts (file → face stem → reference count).
EXPECT_ASKS = {
    "screens.c": {
        "ui_font_montserrat_bold_96_numbers": 11,
        # 6 since the wall-mount trim's 0..359 roller (89afd8d): the five
        # time-screen asks + the display screen's angle roller.
        "ui_font_montserrat_regular_48_numbers": 6,
        "ui_font_montserrat_regular_32_ssid": 15,
        # 3 since the same wave: the display screen's angle readout left the
        # SSID face with the roller's digits.
        "ui_font_montserrat_regular_48_ssid": 3,
        "ui_font_montserrat_regular_32_range": 2,
        "ui_font_fontawesome7_solid_96_icons": 5,
        "ui_font_fontawesome7_regular_96_icons": 4,
    },
    "styles.c": {
        "ui_font_montserrat_bold_96_numbers": 1,
        "ui_font_montserrat_regular_48_numbers": 1,
        "ui_font_fontawesome7_solid_96_icons": 1,
        "ui_font_fontawesome7_regular_96_icons": 1,
    },
}

# The regular icon face's expected carried sets (S3 lacks the clock;
# P4 and knob were baked from the P4 icon Opts and carry it).
REGULAR_BASE = {0xF186, 0xF34E}
CLOCK = 0xF017

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "text.color": INK,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.grid": True,
    "grid.color": HAIR,
    "grid.linewidth": 0.6,
})


def die(msg: str) -> None:
    raise SystemExit(f"gen_perf_glyphs: {msg}")


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def read(p: str) -> str:
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def parse_font(path: str) -> dict:
    """Parse one lv_font_conv file: header facts, carried U+ set, bitmap bytes."""
    src = read(path)
    mh = re.search(r"Size:\s*(\d+)", src)
    mb = re.search(r"Bpp:\s*(\d+)", src)
    mo = re.search(r"Opts:(.*?)\*/", src, re.S)
    if not (mh and mb and mo):
        die(f"{os.path.basename(path)}: no Size/Bpp/Opts header")
    opts = " ".join(mo.group(1).replace("*", " ").split())
    size = int(mh.group(1))
    ms = re.search(r"--size\s+(\d+)", opts)
    if not ms or int(ms.group(1)) != size:
        die(f"{os.path.basename(path)}: Opts --size {ms and ms.group(1)} != header Size {size}")
    mbp = re.search(r"--bpp\s+(\d+)", opts)
    if not mbp or int(mbp.group(1)) != 4 or mb.group(1) != "4":
        die(f"{os.path.basename(path)}: not a Bpp-4 bake")
    if "--no-compress" not in opts:
        die(f"{os.path.basename(path)}: not --no-compress")
    cps = set(int(x, 16) for x in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6})", src))
    if not cps:
        die(f"{os.path.basename(path)}: zero /* U+XXXX */ annotations")
    mbm = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not mbm:
        die(f"{os.path.basename(path)}: no glyph_bitmap[]")
    n = len(re.findall(r"0x[0-9A-Fa-f]{2}", mbm.group(1)))
    # charset fingerprint: every token that is not a flag/size/path —
    # compared byte-equal across tiers per face
    mcharset = re.findall(r"(?:-r|--range)\s+([^\s-]+)|(--symbols(?:=\S+|\s+\S+))", opts)
    charset = "|".join(sorted(a or b for a, b in mcharset))
    return {"px": size, "cps": cps, "bytes": n, "charset": charset}


def utf8_decode(bs: bytes) -> int:
    s = bs.decode("utf-8")
    if len(s) != 1:
        raise ValueError(s)
    return ord(s)


def parse_glyph_defines(screens_raw: str) -> dict:
    """GLYPH_* defines: decoded escape bytes, asserted against the define's own
    /* U+XXXX */ comment (raw text — the comment is the claim)."""
    out = {}
    for m in re.finditer(r'#define\s+(GLYPH_\w+)\s+"((?:\\x[0-9A-Fa-f]{2})+)"\s*(?:/\*\s*U\+([0-9A-Fa-f]+))?',
                         screens_raw):
        name, hexes, claimed = m.group(1), m.group(2), m.group(3)
        bs = bytes(int(h, 16) for h in re.findall(r"\\x([0-9A-Fa-f]{2})", hexes))
        try:
            cp = utf8_decode(bs)
        except (ValueError, UnicodeDecodeError) as exc:
            die(f"{name}: not a single UTF-8 codepoint: {exc}")
        if claimed is not None and int(claimed, 16) != cp:
            die(f"{name}: decoded U+{cp:04X} but its comment claims U+{int(claimed, 16):04X}")
        out[name] = cp
    want = {"GLYPH_XMARK": 0xF057, "GLYPH_MOON": 0xF186, "GLYPH_WIFI": 0xF1EB,
            "GLYPH_STOPWATCH": 0xF2F2, "GLYPH_ALARM": 0xF34E, "GLYPH_CLOCK": CLOCK,
            "GLYPH_GLOBE": 0xF0AC, "GLYPH_SPEAKER": 0xF028, "GLYPH_ROTATE": 0xF2F1}
    if set(want) - set(out):
        die(f"missing GLYPH defines: {sorted(set(want) - set(out))}")
    for k, v in want.items():
        if out[k] != v:
            die(f"{k}: decoded U+{out[k]:04X}, expected U+{v:04X}")
    return out


def main() -> None:
    # ---- the 21 files ------------------------------------------------------------
    if sorted(os.listdir(os.path.join(UI, "fonts"))) != sorted(
            os.path.join(UI, "fonts", f"{n}.c") and f"{n}.c" for n in FACE_NAMES) or len(FACE_NAMES) != 7:
        die("fonts/ does not contain exactly the seven face files")
    data: dict[str, dict[str, dict]] = {}
    for tk, (tdir, _, _) in TIERS.items():
        data[tk] = {}
        for stem in FACE_NAMES:
            data[tk][stem] = parse_font(os.path.join(UI, tdir, f"{stem}.c"))

    # px triples vs the expected per-tier values (the drift gate)
    for label, stem, px, _, _ in FACES:
        for tk in TIERS:
            if data[tk][stem]["px"] != px[tk]:
                die(f"{stem} {tk}: px {data[tk][stem]['px']} != {px[tk]}")

    # raster bytes: > 0 and strictly monotone in px per face
    for label, stem, px, _, _ in FACES:
        order = sorted(TIERS, key=lambda tk: px[tk])
        seq = [data[tk][stem]["bytes"] for tk in order]
        if any(b <= 0 for b in seq):
            die(f"{stem}: non-positive raster bytes {seq}")
        if not all(a < b for a, b in zip(seq, seq[1:])):
            die(f"{stem}: raster bytes not monotone in px: {list(zip([px[t] for t in order], seq))}")

    # charset byte-equality across tiers, six faces; the regular icons are the
    # parsed exception with a parsed expected value
    for label, stem, _, _, _ in FACES:
        if stem.endswith("regular_96_icons"):
            continue
        if len({data[tk][stem]["charset"] for tk in TIERS}) != 1:
            die(f"{stem}: Opts charset drifts between tiers: {[data[tk][stem]['charset'] for tk in TIERS]}")
    if data["s3"]["ui_font_fontawesome7_regular_96_icons"]["charset"] != "62286,61830":
        die(f"regular icons S3 charset {data['s3']['ui_font_fontawesome7_regular_96_icons']['charset']!r} "
            "expected '62286,61830'")
    p4_charset = data["p4"]["ui_font_fontawesome7_regular_96_icons"]["charset"]
    if p4_charset != "62286,61830,61463,61463":
        die(f"regular icons P4 charset {p4_charset!r} expected '62286,61830,61463,61463'")
    if data["p4"]["ui_font_fontawesome7_regular_96_icons"]["charset"] != \
            data["knob"]["ui_font_fontawesome7_regular_96_icons"]["charset"] or \
            "61463" not in data["p4"]["ui_font_fontawesome7_regular_96_icons"]["charset"]:
        die("regular icons: P4 and knob must share the wider range that adds 61463")

    # carried U+ sets: digits faces, SSID faces, weekday face (derived from
    # the two weekday tables), icon faces (from the GLYPH defines' tier rule)
    digits = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x2D}
    if data["s3"]["ui_font_montserrat_bold_96_numbers"]["cps"] != digits:
        die("96 numbers carried set drifted")
    if data["s3"]["ui_font_montserrat_regular_48_numbers"]["cps"] != digits | {0x2E}:
        die("48 numbers carried set drifted")
    ascii95 = set(range(0x20, 0x7F))
    for tk in TIERS:
        for stem in ("ui_font_montserrat_regular_32_ssid", "ui_font_montserrat_regular_48_ssid"):
            if data[tk][stem]["cps"] != ascii95:
                die(f"{stem} {tk}: carried set is not printable ASCII 0x20-0x7E")

    screens_raw = read(os.path.join(UI, "screens.c"))
    actions = read(os.path.join(UI, "actions.c"))
    days_a = re.search(r"s_day_abbr\[7\]\s*=\s*\{([^}]*)\}", strip_comments(actions))
    days_s = re.search(r"ALARM_DAY_ABBR\[7\]\s*=\s*\{([^}]*)\}", strip_comments(screens_raw))
    if not (days_a and days_s):
        die("weekday tables not found (actions.c s_day_abbr / screens.c ALARM_DAY_ABBR)")
    days = [w for blob in (days_a.group(1), days_s.group(1))
            for w in re.findall(r'"([A-Za-z]{2})"', blob)]
    if len(days) != 14:
        die(f"weekday tables must yield 14 two-letter names, got {len(days)}")
    weekday_set = set(ord(c) for c in "".join(days)) | {0x2D}
    if len(weekday_set) != 12:
        die(f"derived weekday alphabet is {len(weekday_set)} codepoints, expected 12")
    for tk in TIERS:
        if data[tk]["ui_font_montserrat_regular_32_range"]["cps"] != weekday_set:
            die(f"32 range {tk}: carried set != the weekday tables' alphabet")

    glyphs = parse_glyph_defines(screens_raw)
    solid_expect = {glyphs["GLYPH_XMARK"], glyphs["GLYPH_MOON"], glyphs["GLYPH_WIFI"],
                    glyphs["GLYPH_STOPWATCH"], glyphs["GLYPH_GLOBE"], glyphs["GLYPH_SPEAKER"],
                    glyphs["GLYPH_ROTATE"]}
    for tk in TIERS:
        if data[tk]["ui_font_fontawesome7_solid_96_icons"]["cps"] != solid_expect:
            die(f"solid icons {tk}: carried set != the seven asked icon codepoints")
    for tk in TIERS:
        want = REGULAR_BASE | ({CLOCK} if tk != "s3" else set())
        if data[tk]["ui_font_fontawesome7_regular_96_icons"]["cps"] != want:
            die(f"regular icons {tk}: carried {sorted(data[tk]['ui_font_fontawesome7_regular_96_icons']['cps'])} "
                f"!= expected {sorted(want)}")

    # ---- askers: repo-wide scan must hit exactly screens.c + styles.c -----------
    askers = {}
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames
                       if d not in ("docs", "build", ".git", ".venv", "managed_components",
                                    "host_sim", "assets")]
        for fn in filenames:
            if not fn.endswith((".c", ".cpp")):
                continue
            p = os.path.join(dirpath, fn)
            src = strip_comments(read(p))
            hits = re.findall(r"&\s*(ui_font_\w+)", src)
            if hits:
                askers[os.path.relpath(p, ROOT)] = hits
    if set(askers) != {"components/ui/screens.c", "components/ui/styles.c"}:
        die(f"repo-wide &ui_font_ scan hit unexpected files: {sorted(askers)}")
    counts = {f: {tk: 0 for tk in TIERS} for f in FACE_NAMES}
    per_file = {}
    for rel, hits in askers.items():
        base = os.path.basename(rel)
        per_file[base] = {}
        for stem in FACE_NAMES:
            c = sum(1 for h in hits if h == stem)
            per_file[base][stem] = c
    for fbase, table in EXPECT_ASKS.items():
        for stem, want in table.items():
            if per_file[fbase][stem] != want:
                die(f"{fbase} asks {stem} {per_file[fbase][stem]} times, expected {want}")
    if sum(sum(v.values()) for v in (per_file[b] for b in ("screens.c", "styles.c"))) != 50:
        die("total ask references drifted from 50")

    # ---- hub ring: 7 unconditional + 1 P4-only; asked ⊆ carried per board -------
    mring = re.search(r"struct hub_icon ring\[\]\s*=\s*\{(.*?)\};", strip_comments(screens_raw), re.S)
    if not mring:
        die("ring[] table not found")
    ring_src = mring.group(1)
    has_if = "#if defined(CONFIG_BOARD_P4)" in ring_src
    has_endif = "#endif" in ring_src
    if not (has_if and has_endif):
        die("ring[] must carry exactly the #if defined(CONFIG_BOARD_P4) leg")
    if ring_src.count("#if defined(CONFIG_BOARD_P4)") != 1 or ring_src.count("#endif") != 1:
        die("ring[] must carry exactly one #if defined(CONFIG_BOARD_P4)/#endif leg")
    ifs = re.search(r"#if defined\(CONFIG_BOARD_P4\)(.*?)#endif", ring_src, re.S)
    guard_lo, guard_hi = ifs.span(1)

    def ring_entries(blob: str, off: int) -> list:
        return [(g, f, s, lo + off) for lo, hi, g, f, s in
                [(m.start(), m.end(), m.group(1), m.group(2), m.group(3))
                 for m in re.finditer(r"\{(GLYPH_\w+),\s*&\s*(ui_font_\w+),\s*(SCREEN_ID_\w+)\}", blob)]]

    all_entries = ring_entries(ring_src, 0)
    p4only = [(g, f, s) for g, f, s, pos in all_entries if guard_lo <= pos < guard_hi]
    uncond = [(g, f, s) for g, f, s, pos in all_entries if not (guard_lo <= pos < guard_hi)]
    if len(uncond) != 7 or len(p4only) != 1:
        die(f"ring table drifted: {len(uncond)} unconditional + {len(p4only)} P4-only")
    if not (p4only[0][0] == "GLYPH_CLOCK"
            and p4only[0][1] == "ui_font_fontawesome7_regular_96_icons"
            and p4only[0][2] == "SCREEN_ID_SETTINGS_TIME"):
        die(f"P4-only ring entry drifted: {p4only[0]}")
    asked_regular = {tk: set() for tk in TIERS}
    asked_solid = set()
    for gname, fname, _ in uncond:
        if "regular" in fname:
            for tk in TIERS:
                asked_regular[tk].add(glyphs[gname])
        elif "solid" in fname:
            asked_solid.add(glyphs[gname])
    asked_regular["p4"].add(glyphs["GLYPH_CLOCK"])  # the entry is P4-only code
    # the xmark exit button and the stopwatch draw solid via the style
    # installer and the pomodoro label
    if "GLYPH_XMARK" not in screens_raw or "GLYPH_STOPWATCH" not in screens_raw:
        die("xmark/stopwatch asks vanished")
    asked_solid |= {glyphs["GLYPH_XMARK"], glyphs["GLYPH_STOPWATCH"]}
    for tk in TIERS:
        if not asked_regular[tk] <= data[tk]["ui_font_fontawesome7_regular_96_icons"]["cps"]:
            die(f"{tk}: ring asks a regular glyph the tier does not carry")
        if not asked_solid <= data[tk]["ui_font_fontawesome7_solid_96_icons"]["cps"]:
            die(f"{tk}: ring asks a solid glyph the tier does not carry")
    dead_knob = data["knob"]["ui_font_fontawesome7_regular_96_icons"]["cps"] - asked_regular["knob"]
    if dead_knob != {CLOCK}:
        die(f"knob regular dead-glyph set drifted: {sorted(dead_knob)}")

    # ---- wheel registers stay inside the 32 SSID face ----------------------------
    mw = re.search(r"s_wheel_sets\[4\]\s*=\s*\{(.*?)\};", strip_comments(screens_raw), re.S)
    if not mw:
        die("s_wheel_sets not found")
    regs = re.findall(r'"([^"]*)"', mw.group(1))
    if len(regs) != 4:
        die(f"s_wheel_sets must have 4 registers, got {len(regs)}")
    reg_chars = set("".join(regs))
    if any(not (0x20 <= ord(c) <= 0x7E) for c in reg_chars):
        die("a wheel register char escapes printable ASCII")
    if not all(ord(c) in data[tk]["ui_font_montserrat_regular_32_ssid"]["cps"]
               for tk in TIERS for c in reg_chars):
        die("a wheel register char is outside the 32 SSID face's carried set")

    # ---- choosers + foundry + docs cross-checks ---------------------------------
    cmake = read(os.path.join(UI, "CMakeLists.txt"))
    for tok in ('if(KNOB_BOARD)', 'set(FONT_DIR "fonts_knob")', 'elseif(CONFIG_BOARD_P4)',
                'set(FONT_DIR "fonts_p4")', 'set(FONT_DIR "fonts")'):
        if tok not in cmake:
            die(f"CMakeLists.txt lost the FONT_DIR token {tok!r}")
    build = read(os.path.join(ROOT, "host_sim", "uisim", "build.sh"))
    for tok in ("fonts", "fonts_p4", "fonts_knob", "466", "720", "360"):
        if tok not in build:
            die(f"uisim build.sh lost the panel-map token {tok!r}")
    for tk, (tdir, _, _) in TIERS.items():
        if f'$UI/{tdir}' not in build:
            die(f"uisim build.sh must map a panel to {tdir}")
    fonts_h = read(os.path.join(UI, "fonts.h"))
    externs = re.findall(r"extern const lv_font_t (ui_font_\w+);", fonts_h)
    if sorted(externs) != sorted(FACE_NAMES):
        die(f"fonts.h externs drifted: {sorted(externs)}")
    if "Knob tier only" in fonts_h:
        die("fonts.h still calls 48_ssid 'Knob tier only' while all three tiers "
            "carry the file and the roller asks for it family-wide")
    for rel, lit in (("docs/devices/p4.rst", "148/74/50"),
                     ("docs/devices/knob.rst", "74/37/25"),
                     ("docs/ui/index.rst", "96/48/32")):
        if lit not in read(os.path.join(ROOT, rel)):
            die(f"{rel} no longer carries the literal {lit!r} the files bake")
    for src_face in ("Montserrat/Montserrat-Regular.ttf", "Montserrat/Montserrat-Bold.ttf",
                     "Fontawesome/Font Awesome 7 Free-Regular-400.otf",
                     "Fontawesome/Font Awesome 7 Free-Solid-900.otf"):
        if not os.path.isfile(os.path.join(ROOT, "assets", "fonts", src_face)):
            die(f"foundry source face missing: assets/fonts/{src_face}")

    # ---- the figure --------------------------------------------------------------
    fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(13.6, 6.6),
                                     gridspec_kw={"width_ratios": [1.25, 1.0]})
    fig.suptitle("THE GLYPH LADDER — what 21 lv_font_conv bakes cost, and what the firmware asks them to paint",
                 fontsize=13, fontweight="bold", x=0.5)

    # LEFT: raster bytes per face, one grouped bar per tier (log)
    groups = FACES
    y = 0.0
    ylab, ypos = [], []
    h = 0.24
    for label, stem, px, _, _ in groups:
        ypos.append(y + h)  # centred on the three-bar group
        ylab.append(label)
        for k, tk in enumerate(("s3", "p4", "knob")):
            b = data[tk][stem]["bytes"]
            ax_l.barh(y + (1 - k) * h, b / 1024.0, height=h * 0.92,
                      color=TIERS[tk][1], edgecolor="white", linewidth=0.5, zorder=3)
            ax_l.text(b / 1024.0 * 1.06, y + (1 - k) * h,
                      f"{b / 1024.0:.1f} KiB · {px[tk]} px", va="center", fontsize=6.6, color=MUTED)
        y += 1.0
    ax_l.set_xscale("log")
    ax_l.set_yticks(ypos)
    ax_l.set_yticklabels(ylab, fontsize=8.5)
    ax_l.invert_yaxis()
    ax_l.set_xlim(0.1, 400)
    ax_l.set_xlabel("raster bytes (4 bpp, --no-compress) — log")
    ax_l.set_title("WHAT EVERY BAKE COSTS — the same face, three densities", fontsize=9,
                   color=MUTED, loc="left")
    handles = [plt.Rectangle((0, 0), 1, 1, color=TIERS[tk][1]) for tk in ("s3", "p4", "knob")]
    ax_l.legend(handles, [TIERS[tk][2] for tk in ("s3", "p4", "knob")],
                fontsize=7.5, loc="lower right", frameon=True)

    # RIGHT: carried glyphs per face + static-asked markers; italic stubs where
    # the asked set is runtime-formatted
    y = 0.0
    rlab, rpos = [], []
    for label, stem, px, _, kind in FACES:
        carried = len(data["s3"][stem]["cps"])
        rpos.append(y)
        rlab.append(label)
        ax_r.barh(y, carried, height=0.52, color=STEEL, edgecolor="white", linewidth=0.5,
                  alpha=0.35, zorder=2)
        ax_r.plot([carried], [y], marker="o", ms=7, color=STEEL, zorder=4)
        ax_r.text(carried + 2, y + 0.30, f"carried {carried}", fontsize=7, color=STEEL)
        if kind == "weekday":
            ax_r.plot([len(weekday_set)], [y], marker="o", ms=8, mfc="none", mew=1.6,
                      color="#2b5fb0", zorder=5)
            ax_r.text(2, y - 0.36, "asked = the two weekday tables' full alphabet (12 = 12)",
                      fontsize=6.8, color="#2b5fb0")
        elif kind == "registers":
            ax_r.plot([len(reg_chars)], [y], marker="o", ms=8, mfc="none", mew=1.6,
                      color="#2b5fb0", zorder=5)
            ax_r.text(2, y - 0.36,
                      f"asked = the wheel's 4 registers ({len(reg_chars)} distinct chars, all inside)",
                      fontsize=6.8, color="#2b5fb0")
        elif kind == "glyphs-solid":
            ax_r.plot([len(asked_solid)], [y], marker="o", ms=8, mfc="none", mew=1.6,
                      color="#2b5fb0", zorder=5)
            ax_r.text(2, y - 0.36, "asked = xmark + ring 5 + stopwatch (7 = 7, no dead glyph)",
                      fontsize=6.8, color="#2b5fb0")
        elif kind == "glyphs-regular":
            for k, tk in enumerate(("s3", "p4", "knob")):
                ax_r.plot([len(asked_regular[tk])], [y - 0.08 - k * 0.13], marker="o", ms=6,
                          mfc="none", mew=1.5, color=TIERS[tk][1], zorder=5)
            ax_r.text(2, y - 0.55, "asked: S3 2 · P4 3 (ring clock) · knob 2 — "
                                   "the knob carries +1 inherited glyph, never drawn",
                      fontsize=6.8, color=RED)
        else:
            ax_r.text(2, y - 0.36, "asked set is runtime-formatted (digits/SSIDs) — no static stub",
                      fontsize=6.8, color=MUTED, style="italic")
        y += 1.0
    # per-board totals block — drawn at HALF scale on the shared x-axis
    # (label carries the true glyph count) so nothing needs a twin y scale
    tot = {tk: sum(len(data[tk][stem]["cps"]) for _, stem, _, _, _ in FACES) for tk in TIERS}
    y += 0.3
    for k, tk in enumerate(("s3", "p4", "knob")):
        ax_r.barh(y + k * 0.55, tot[tk] / 2.0, height=0.4, color=TIERS[tk][1], alpha=0.85, zorder=3)
        ax_r.text(tot[tk] / 2.0 - 3, y + k * 0.55, f"{TIERS[tk][2]}: {tot[tk]} glyphs · "
                  f"{sum(data[tk][st]['bytes'] for _, st, _, _, _ in FACES) / 1024.0:.0f} KiB",
                  va="center", ha="right", fontsize=7.5, color="white", fontweight="bold")
    ax_r.set_yticks(rpos + [y + k * 0.55 for k in range(3)])
    ax_r.set_yticklabels(rlab + ["", "", ""], fontsize=8.5)
    ax_r.set_xlim(0, 130)
    ax_r.set_title("WHO PAINTS WHAT — carried (steel dot) vs statically asked (open circle)",
                   fontsize=9, color=MUTED, loc="left")
    ax_r.invert_yaxis()

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fig.savefig(OUT, dpi=160)
    print(f"gen_perf_glyphs: wrote {OUT}")
    for tk in TIERS:
        print(f"  {TIERS[tk][2]:>9}: {tot[tk]:>4} glyphs, "
              f"{sum(data[tk][st]['bytes'] for _, st, _, _, _ in FACES):>7} raster B")
    print(f"  asks: 50 references in screens.c(46)+styles.c(4); "
          f"knob regular dead glyph: U+{CLOCK:04X}")


if __name__ == "__main__":
    main()
