#!/usr/bin/env python3
"""One-shot moon-asset acquisition for a fresh clone.

The STATIC NASA ground truth ships with the repo: the two NASA source TIFFs
(``moon-kit/``), the 36 SVS 5415 gate/quarter reference frames
(``moon_phases_2025/``) and the 8760-hour ``mooninfo_2025.json`` are
committed (the TIFs via LFS) because the data is proven static (the archive
snapshots carry one constant digest 2020 to 2026) and public-domain — a
fresh clone therefore works with no network round-trip to NASA. Everything
ELSE the pipeline needs is still downloaded or generated, and this script
is the single tracked entry point that acquires it: the baked per-board
blobs (``out/<target>/``), the answer-key crops (``final_400x400/``),
``tone_lut.bin``, the docs font faces (``assets/fonts/``) and the local
``.venv``. The pipeline *code* (the ``moon/`` package, this script, the
research scripts) is tracked too.

Run it with any Python 3 -- it is stdlib-only and bootstraps its own venv:

    python3 assets/images/moon/get_assets.py                # all three boards
    python3 assets/images/moon/get_assets.py --target s3    # one board only
    python3 assets/images/moon/get_assets.py --refs         # + image-gate refs
    python3 assets/images/moon/get_assets.py --fonts        # + docs font faces
    python3 assets/images/moon/get_assets.py --force        # re-download

Every step is resumable; re-running costs one hash pass per file.

Steps
-----
1. venv      Create ``.venv`` (uv when present, else venv+pip) and install
             ``requirements.txt`` into it.
2. sources   The CGI Moon Kit pair (NASA SVS 4720) in ``moon-kit/`` is
             COMMITTED (LFS, sha256- and size-pinned) — on a fresh LFS
             clone this step is a no-op hash pass. It only re-downloads
             (from the primary or the archive fallback below) when a copy
             fails its pins (a ``--force``, a missing LFS pull). NASA's
             edge sometimes
             sends the leaf certificate without its Let's Encrypt
             intermediate; we then emulate the browser's AIA fetch once
             (fetch the CA-Issuers URI over http, add it as an extra trust
             anchor) -- the payload stays checksum-verified regardless.
             Since 2026-11-26 svs.gsfc.nasa.gov answers a uniform 403 for
             its whole ``/vis/`` data tree (the homepage and the
             ``/api/dialamoon`` leg are still live), every SVS data fetch
             also carries an Internet Archive fallback: on a failed
             primary, ``fetch()`` retries the file from the archive's
             status-200 snapshot (raw ``id_`` serve) and holds THAT copy
             to the same sha256/size pins -- provenance is the pin, not
             the server.
3. tone_lut  Regenerate ``tone_lut.bin`` from ``moon.constants.TONE_LUT``
             (the single source of truth the C header is generated from;
             the .bin is a raw dump the docs figures read).
4. bake      ``python -m moon.assets --target ...``: per-board
             ``out/<t>/moon_assets.bin`` + manifest, and the tracked
             generated header ``components/moon/generated/<t>/``.
5. --refs    Fetch ``mooninfo_2025.json`` from the NASA dialamoon API (one
             call per hour of 2025, resumable cache under ``.mooninfo/``),
             then the SVS 5415 reference frames the image gate and the
             docs quarter strip need (the gate's 32-frame set plus the five
             quarter hours), cropped to 400x400 alpha PNGs under
             ``final_400x400/`` with ImageMagick.
6. --fonts   Fetch the four foundry faces the docs glyph census
             (``docs/tools/gen_perf_glyphs.py``) sets its panels in into
             ``assets/fonts/``: Montserrat Regular/Bold from the designer's
             commit-pinned upstream repo (google/fonts ships the family
             variable-only), Font Awesome 7 Free Regular/Solid extracted
             with stdlib ``zipfile`` from the official 7.3.1 release zip.
             sha256- and size-pinned throughout; the zip stays cached under
             ``.fonts/``. gen_perf_glyphs.py refuses to run without them.

Provenance of every URL and pin: README.md.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

SVS = "https://svs.gsfc.nasa.gov"
KIT_DIR = os.path.join(HERE, "moon-kit")
KIT_BASE = SVS + "/vis/a000000/a004700/a004720/"          # CGI Moon Kit
FRAMES_BASE = SVS + ("/vis/a000000/a005400/a005415/frames/"
                     "1920x1080_16x9_30p/plain/")          # Moon Phases 2025

# The two bake inputs, pinned by sha256 + exact size (verified on 2026-11-17).
SOURCES = {
    "lroc_color_16bit_srgb_4k.tif": (
        KIT_BASE + "lroc_color_16bit_srgb_4k.tif",
        "9731fa8af425b6c2f88f277ecca82bf8c603f3743894f64ed7b25c5bfefa22ff",
        61_891_324),
    "ldem_16.tif": (
        # The kit's name means PIXELS PER DEGREE: this is the 16 px/deg
        # displacement map, 5760x2880 float32 km (the bake bilinearly
        # resizes it to each board's DEM row).
        KIT_BASE + "ldem_16.tif",
        "1ea42bf44f7e9d694f79c3afa7145f97fbf06cc67372067d9fe73dce43bad796",
        66_378_634),
}

# --- foundry faces (docs/tools/gen_perf_glyphs.py's glyph census) ----------
# The four source faces the figure sets its panels in. assets/fonts/ is
# gitignored, so they are acquired, never committed. Montserrat is pinned to
# a commit of the designer's upstream repo (google/fonts ships the family
# variable-only -- no statics there); Font Awesome rides the official
# release zip with one member pin per face on top of the zip pin. Verified
# 2026-11-17; full provenance in README.md.
FONTS_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "fonts"))
MONTSERRAT_BASE = ("https://raw.githubusercontent.com/JulietaUla/Montserrat/"
                   "555facfb2a18c72c3c0380f0d9c0f060453a9058/fonts/ttf/")
MONTSERRAT = {                       # rel assets/fonts/ -> (url, sha256, B)
    "Montserrat/Montserrat-Regular.ttf": (
        MONTSERRAT_BASE + "Montserrat-Regular.ttf",
        "3e8abe50c44c82e2242e97d1ec8c0d385c4890cdc50447bcdb8605c81a38cfb2",
        445_928),
    "Montserrat/Montserrat-Bold.ttf": (
        MONTSERRAT_BASE + "Montserrat-Bold.ttf",
        "bc6e854971cea46b463be6f9eef4d9cd52f51cfc1fc0dd90c9d3e6483dc0ec61",
        454_864),
}
FA_ZIP = ("https://github.com/FortAwesome/Font-Awesome/releases/download/"
          "7.3.1/fontawesome-free-7.3.1-desktop.zip",
          "c61edde261707f33376a28e9a30bb8c70c1a20bf0bd975206b809f3b3b70add5",
          6_508_960)
FA_MEMBERS = {           # member basename -> (rel assets/fonts/, sha256, B)
    "Font Awesome 7 Free-Regular-400.otf": (
        "Fontawesome/Font Awesome 7 Free-Regular-400.otf",
        "d0d7ae248cfa08da7f1beaaad4844e3aff6f980ec4ec5bb31413a64d028b98b4",
        87_340),
    "Font Awesome 7 Free-Solid-900.otf": (
        "Fontawesome/Font Awesome 7 Free-Solid-900.otf",
        "daa92774861a14e9ccbccc5e73a5fba514271ba4d1bd7e5adbf952182bc00835",
        414_704),
}

MOONINFO = os.path.join(HERE, "mooninfo_2025.json")
LINE_DIR = os.path.join(HERE, ".mooninfo")
HOURS = 8760                          # 2025 is not a leap year: 365 * 24
GATE_FRAMES = 32                      # host_sim/check_gate.py default set
QUARTER_IDX = (0, 2190, 4380, 6570, 8759)   # docs/tools/gen_nasa_quarters.py


# --- HTTPS, with an AIA fallback ------------------------------------------
# svs.gsfc.nasa.gov intermittently sends the leaf without its LE
# intermediate, which breaks plain OpenSSL clients (browsers follow the
# leaf's AIA "CA Issuers" URI instead). We mirror that behaviour, once per
# host, only after the normal verification fails.

_CTX_CACHE: dict[str, ssl.SSLContext] = {}


def _is_cert_error(e: BaseException) -> bool:
    # urllib wraps the handshake failure in URLError(reason=SSLError...);
    # match both spellings.
    return isinstance(e, ssl.SSLCertVerificationError) or (
        isinstance(e, urllib.error.URLError) and
        isinstance(getattr(e, "reason", None), ssl.SSLCertVerificationError))


def _ssl_get(url: str, timeout: int = 60):
    host = urllib.parse.urlparse(url).netloc.rsplit("@", 1)[-1].split(":")[0]
    ctx = _CTX_CACHE.get(host) or ssl.create_default_context()
    try:
        return urllib.request.urlopen(url, timeout=timeout, context=ctx)
    except urllib.error.URLError as e:
        if not _is_cert_error(e):
            raise
        ctx = _ctx_with_aia(host)
        _CTX_CACHE[host] = ctx
        return urllib.request.urlopen(url, timeout=timeout, context=ctx)


def _openssl(args: list[str], stdin: bytes = b"", timeout: int = 60) -> str:
    if shutil.which("openssl") is None:
        raise SystemExit(
            "openssl(1) is needed for the NASA TLS fallback (or fix the "
            "system trust store so svs.gsfc.nasa.gov verifies directly)")
    p = subprocess.run(["openssl"] + args, input=stdin,
                       capture_output=True, timeout=timeout)
    return p.stdout.decode(errors="replace")


def _ctx_with_aia(host: str) -> ssl.SSLContext:
    # s_client exits non-zero on the very verification failure we are
    # working around; its stdout still carries the leaf.
    out = _openssl(["s_client", "-connect", f"{host}:443",
                    "-servername", host])
    m = re.search(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
                  out, re.S)
    if not m:
        raise SystemExit(f"could not read the TLS leaf for {host}")
    with tempfile.TemporaryDirectory() as td:
        leaf = os.path.join(td, "leaf.pem")
        with open(leaf, "w") as f:
            f.write(m.group(0) + "\n")
        txt = _openssl(["x509", "-in", leaf, "-noout", "-text"])
    uri = re.search(r"CA Issuers - URI:(\S+)", txt)
    if not uri:
        raise SystemExit(f"{host}: TLS failure and the leaf carries no "
                         "AIA CA-Issuers URI -- check the network/clock")
    der = urllib.request.urlopen(uri.group(1), timeout=30).read()  # plain http
    with tempfile.TemporaryDirectory() as td:
        d, pem = os.path.join(td, "i.der"), os.path.join(td, "i.pem")
        with open(d, "wb") as f:
            f.write(der)
        _openssl(["x509", "-inform", "der", "-in", d, "-outform", "pem",
                  "-out", pem])
        with open(pem) as f:
            extra = f.read()
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(cadata=extra)   # add the missing intermediate
    # Path building must not die trying to look up the intermediate's own
    # issuer: either terminate the chain AT the supplied (non-self-signed)
    # anchor (PARTIAL_CHAIN, 3.13+), or hand it the system bundle so the
    # chain completes to the real root -- exactly what the browser does.
    partial = getattr(ssl, "VERIFY_X509_PARTIAL_CHAIN", None)
    if partial:
        ctx.verify_flags |= partial
    else:
        paths = ssl.get_default_verify_paths()
        if paths.openssl_cafile and os.path.exists(paths.openssl_cafile):
            with open(paths.openssl_cafile) as f:
                ctx.load_verify_locations(cadata=f.read())
        elif paths.openssl_capath and os.path.isdir(paths.openssl_capath):
            for name in sorted(os.listdir(paths.openssl_capath)):
                with open(os.path.join(paths.openssl_capath, name)) as f:
                    ctx.load_verify_locations(cadata=f.read())
    return ctx


# --- downloads --------------------------------------------------------------

def _wayback(url: str, timeout: int = 30) -> str | None:
    """The Internet Archive's raw (``id_``) copy of ``url``, or None.

    ``svs.gsfc.nasa.gov`` retired its ``/vis/`` data tree (uniform 403 for
    every path in it, known and unknown alike, since 2026-11-26) while the
    homepage and the dialamoon API stayed live. The archive holds 200
    snapshots of the pinned files; only a snapshot the availability API
    reports as status-200 is offered, and the caller's sha256/size checks
    still apply to whatever bytes come back, so a broken snapshot can
    never be kept.
    """
    api = ("http://archive.org/wayback/available?url="
           + urllib.parse.quote(url, safe=""))
    try:
        with urllib.request.urlopen(api, timeout=timeout) as r:
            snap = (json.loads(r.read())
                    .get("archived_snapshots") or {}).get("closest")
    except Exception:
        return None
    if not snap or snap.get("status") != "200":
        return None
    ts = str(snap.get("timestamp") or "")
    u = snap.get("url") or ""
    return u.replace(f"/web/{ts}/", f"/web/{ts}id_/", 1) if ts else None


def _candidates(url: str):
    """``url`` and, for NASA SVS data files, the archive fallback.

    Lazy: the archive is only consulted after the primary fails, so a
    healthy primary costs no extra lookup.
    """
    yield url
    if url.startswith(SVS):
        wb = _wayback(url)
        if wb:
            yield wb


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url: str, dest: str, sha256: str | None = None,
          size: int | None = None, min_size: int = 1,
          force: bool = False) -> bool:
    """GET url -> dest unless dest already passes the checks. True on fetch."""
    if os.path.exists(dest) and not force:
        ok = (size is None or os.path.getsize(dest) == size) and \
             (sha256 is None or _sha256(dest) == sha256)
        if ok:
            return False
        os.unlink(dest)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    h = hashlib.sha256()
    n = 0
    last: Exception | str | None = None
    for cand in _candidates(url):
        try:
            with _ssl_get(cand) as r, open(tmp, "wb") as f:
                for chunk in iter(lambda: r.read(1 << 20), b""):
                    f.write(chunk)
                    h.update(chunk)
                    n += len(chunk)
            if cand != url:
                print(f"  fetched from the Internet Archive snapshot "
                      f"({url} is down): {cand}")
            break
        except Exception as e:      # 403 from the retired tree, network, ...
            last = e
            if cand != url:
                last = (f"{last}; and the Internet Archive snapshot "
                        f"failed too")
    else:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise SystemExit(f"{url}: {last} -- the pinned file could not be "
                         "fetched from svs.gsfc.nasa.gov or the Internet "
                         "Archive")
    if size is not None and n != size:
        os.unlink(tmp)
        raise SystemExit(f"{url}: got {n} B, expected {size}")
    if n < min_size:
        os.unlink(tmp)
        raise SystemExit(f"{url}: only {n} B -- refusing to keep it")
    if sha256 is not None and h.hexdigest() != sha256:
        os.unlink(tmp)
        raise SystemExit(f"{url}: sha256 mismatch (got {h.hexdigest()}) -- "
                         "the pinned upstream file changed; re-verify and "
                         "re-pin in SOURCES")
    os.replace(tmp, dest)
    print(f"  fetched {os.path.relpath(dest, HERE)} ({n:,} B)")
    return True


# --- steps ------------------------------------------------------------------

def ensure_venv(base_python: str) -> str:
    """The bake/gate venv (posix layout: Linux/macOS dev hosts)."""
    vpy = os.path.join(HERE, ".venv", "bin", "python")
    if os.path.exists(vpy):
        return vpy
    print("creating .venv (this pulls numpy/scipy/tifffile/astropy ...)")
    uv = shutil.which("uv")
    if uv:
        subprocess.run([uv, "venv", ".venv"], cwd=HERE, check=True)
        subprocess.run([uv, "pip", "install", "--python", vpy,
                        "-r", os.path.join(HERE, "requirements.txt")],
                       cwd=HERE, check=True)
    else:
        subprocess.run([base_python, "-m", "venv", ".venv"], cwd=HERE,
                       check=True)
        subprocess.run([vpy, "-m", "pip", "install", "-r",
                        os.path.join(HERE, "requirements.txt")],
                       cwd=HERE, check=True)
    return vpy


def ensure_tone_lut(vpy: str) -> None:
    """tone_lut.bin is a raw dump of the tracked TONE_LUT constant (docs
    figures read the .bin; the C header carries the same table)."""
    out = os.path.join(HERE, "tone_lut.bin")
    code = "from moon import constants as C; open(%r,'wb').write(bytes(C.TONE_LUT))" % out
    subprocess.run([vpy, "-c", code], cwd=HERE, check=True)
    if os.path.getsize(out) != 256:
        raise SystemExit(f"tone_lut.bin is {os.path.getsize(out)} B, "
                         "expected 256")


def bake(vpy: str, targets: list[str]) -> None:
    env = dict(os.environ, MOON_HEIGHT=os.path.join(KIT_DIR, "ldem_16.tif"))
    subprocess.run([vpy, "-m", "moon.assets", "--target", *targets],
                   cwd=HERE, check=True, env=env)


def _hour_iso(i: int) -> str:
    return (dt.datetime(2025, 1, 1) +
            dt.timedelta(hours=i)).isoformat(timespec="minutes")


def _fetch_hour(i: int) -> None:
    line = os.path.join(LINE_DIR, f"line.{i:04d}.json")
    if os.path.exists(line) and os.path.getsize(line) > 0:
        return
    url = f"{SVS}/api/dialamoon/{_hour_iso(i)}"
    last: Exception | None = None
    for attempt in range(5):
        try:
            with _ssl_get(url) as r:
                a = json.loads(r.read())
            # Fold the API's flat fields into the nested record schema the
            # gate and research docs expect (mooninfo_2025.json schema).
            rec = {
                "time": dt.datetime.strptime(
                    _hour_iso(i), "%Y-%m-%dT%H:%M"
                ).strftime("%d %b %Y %H:%M") + " UT",
                "phase": a["phase"],
                "age": a["age"],
                "diameter": a["diameter"],
                "distance": a["distance"],
                "j2000": {"ra": a["j2000_ra"], "dec": a["j2000_dec"]},
                "subsolar": {"lon": a["subsolar_lon"], "lat": a["subsolar_lat"]},
                "subearth": {"lon": a["subearth_lon"], "lat": a["subearth_lat"]},
                "posangle": a["posangle"],
            }
            with open(line, "w") as f:
                json.dump(rec, f)
            return
        except Exception as e:                     # retry, keep the last error
            last = e
            time.sleep(1 + attempt)
    raise SystemExit(f"dialamoon {url}: {last}")


def ensure_mooninfo(force: bool = False) -> str:
    if os.path.exists(MOONINFO) and not force:
        try:
            with open(MOONINFO) as f:
                if len(json.load(f)) == HOURS:
                    return MOONINFO
        except (json.JSONDecodeError, ValueError):
            pass
    print(f"fetching mooninfo_2025.json ({HOURS} dialamoon calls, "
          "resumable under .mooninfo/) ...")
    os.makedirs(LINE_DIR, exist_ok=True)
    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        list(ex.map(_fetch_hour, range(HOURS)))
    recs = []
    for i in range(HOURS):
        with open(os.path.join(LINE_DIR, f"line.{i:04d}.json")) as f:
            recs.append(json.load(f))
    with open(MOONINFO, "w") as f:
        json.dump(recs, f)
    print(f"  fetched mooninfo_2025.json ({HOURS} hours)")
    return MOONINFO


def _gate_indices(vpy: str, n: int) -> list[int]:
    """The gate frame set, computed by the very selector the gate uses."""
    code = ("import json; from moon.validate import select_frames;"
            "print(' '.join(str(i) for i, _ in select_frames("
            "json.load(open(%r)), %d)))" % (MOONINFO, n))
    r = subprocess.run([vpy, "-c", code], cwd=HERE, check=True,
                       capture_output=True, text=True)
    return [int(x) for x in r.stdout.split()]


def ensure_refs(vpy: str, n_gate: int, force: bool) -> None:
    ensure_mooninfo(force)
    idx = sorted(set(_gate_indices(vpy, n_gate)) | set(QUARTER_IDX))
    tif_dir = os.path.join(HERE, "moon_phases_2025")
    png_dir = os.path.join(HERE, "final_400x400")
    os.makedirs(png_dir, exist_ok=True)
    print(f"reference frames: {len(idx)} SVS 5415 tifs "
          "(gate set + quarter hours) ...")

    def get(i: int) -> None:
        name = f"moon.{i + 1:04d}.tif"          # NASA frame numbers are 1-based
        fetch(FRAMES_BASE + name, os.path.join(tif_dir, name),
              min_size=100_000, force=force)

    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        list(ex.map(get, idx))

    im = shutil.which("magick") or shutil.which("convert")
    if im is None:
        raise SystemExit("ImageMagick (magick/convert) is required to crop "
                         "the reference frames -- apt install imagemagick")

    def crop(i: int) -> None:
        src = os.path.join(tif_dir, f"moon.{i + 1:04d}.tif")
        dst = os.path.join(png_dir, f"moon.{i + 1:04d}.png")
        if os.path.exists(dst) and not force:
            return
        subprocess.run(
            [im, src, "-alpha", "on", "-background", "none",
             "-trim", "+repage", "-filter", "Lanczos", "-resize", "400x400",
             "-gravity", "center", "-extent", "400x400", "PNG32:" + dst],
            check=True, capture_output=True)

    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        list(ex.map(crop, idx))
    print(f"reference frames: {len(idx)} cropped -> final_400x400/")


def ensure_fonts(force: bool) -> None:
    """assets/fonts/ -- the four foundry faces gen_perf_glyphs.py sets its
    glyph census in. Fetch-only; nothing here is committed."""
    for rel, (url, sha, size) in MONTSERRAT.items():
        fetch(url, os.path.join(FONTS_DIR, rel), sha256=sha, size=size,
              force=force)
    zpath = os.path.join(HERE, ".fonts", FA_ZIP[0].rsplit("/", 1)[-1])
    fetch(FA_ZIP[0], zpath, sha256=FA_ZIP[1], size=FA_ZIP[2], force=force)
    with zipfile.ZipFile(zpath) as z:
        entries = {os.path.basename(n): n for n in z.namelist()
                   if n.lower().endswith(".otf")}
        for name, (rel, sha, size) in FA_MEMBERS.items():
            member = entries.get(name)
            if member is None:
                raise SystemExit(f"{os.path.basename(zpath)}: member "
                                 f"{name!r} is gone -- check the release "
                                 "layout against README.md")
            data = z.read(member)
            if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
                raise SystemExit(f"{name}: member sha256/size mismatch -- "
                                 "re-verify upstream and re-pin FA_MEMBERS")
            dest = os.path.join(FONTS_DIR, rel)
            if (os.path.exists(dest) and not force
                    and os.path.getsize(dest) == size
                    and _sha256(dest) == sha):
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data)
            print(f"  extracted {os.path.relpath(dest, HERE)} ({size:,} B)")


# --- entry point -------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Acquire + bake the moon assets (nothing here is "
                    "committed; see the module docstring).")
    ap.add_argument("--target", nargs="+", default=["s3", "p4", "knob"],
                    metavar="T", help="board targets (default: s3 p4 knob)")
    ap.add_argument("--refs", action="store_true",
                    help="also fetch mooninfo_2025.json and the SVS 5415 "
                         "reference frames the image gate and docs need")
    ap.add_argument("--fonts", action="store_true",
                    help="also fetch the four foundry faces into "
                         "assets/fonts/ (docs glyph census prerequisite)")
    ap.add_argument("--gate-frames", type=int, default=GATE_FRAMES,
                    help="frame count of the gate set (default: "
                         f"{GATE_FRAMES}, matching check_gate.py)")
    ap.add_argument("--force", action="store_true",
                    help="re-download and re-crop even when the files exist")
    ap.add_argument("--skip-venv", action="store_true")
    ap.add_argument("--skip-sources", action="store_true")
    ap.add_argument("--skip-bake", action="store_true")
    args = ap.parse_args()

    vpy = None if args.skip_venv else ensure_venv(sys.executable)

    if not args.skip_sources:
        print("CGI Moon Kit sources (NASA SVS 4720), checksum-pinned ...")
        for name, (url, sha, size) in SOURCES.items():
            fetch(url, os.path.join(KIT_DIR, name), sha256=sha, size=size,
                  force=args.force)

    if vpy:
        ensure_tone_lut(vpy)
        if not args.skip_bake:
            print(f"baking {', '.join(args.target)} ...")
            bake(vpy, args.target)

    if args.refs:
        if vpy is None:
            vpy = ensure_venv(sys.executable)
        ensure_refs(vpy, args.gate_frames, args.force)

    if args.fonts:
        print("foundry faces for the docs glyph census ...")
        ensure_fonts(args.force)

    print("\nnow in place:")
    for t in args.target:
        blob = os.path.join(HERE, "out", t, "moon_assets.bin")
        state = f"{os.path.getsize(blob):,} B" if os.path.exists(blob) \
            else "MISSING"
        print(f"  out/{t}/moon_assets.bin: {state}")
    if args.refs:
        n = len([p for p in os.listdir(os.path.join(HERE, "final_400x400"))
                 if p.endswith(".png")]) if os.path.isdir(
            os.path.join(HERE, "final_400x400")) else 0
        print(f"  final_400x400: {n} reference PNGs")
        print(f"  mooninfo_2025.json: "
              f"{'present' if os.path.exists(MOONINFO) else 'MISSING'}")
    faces = list(MONTSERRAT) + [rel for rel, _s, _b in FA_MEMBERS.values()]
    n = sum(1 for rel in faces if os.path.exists(os.path.join(FONTS_DIR, rel)))
    state = ("docs glyph census ready" if n == len(faces) else
             "MISSING -- gen_perf_glyphs.py needs all four (get_assets.py "
             "--fonts)")
    print(f"  assets/fonts: {n}/{len(faces)} foundry faces: {state}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
