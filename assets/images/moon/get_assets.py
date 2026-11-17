#!/usr/bin/env python3
"""One-shot moon-asset acquisition for a fresh clone.

Every moon-asset *binary* in this project is either downloaded or generated
-- nothing in ``assets/`` is committed, and this script is the single
tracked entry point that re-acquires what ``.gitignore`` deliberately keeps
out of the tree: the two NASA source TIFFs (``moon-kit/``), the baked
per-board blobs (``out/<target>/``), the fetched ``mooninfo_2025.json`` and
the reference renders (``final_400x400/``), plus the local ``.venv``. The
pipeline *code* (the ``moon/`` package, this script, the research scripts)
is tracked.

Run it with any Python 3 -- it is stdlib-only and bootstraps its own venv:

    python3 assets/images/moon/get_assets.py                # all three boards
    python3 assets/images/moon/get_assets.py --target s3    # one board only
    python3 assets/images/moon/get_assets.py --refs         # + image-gate refs
    python3 assets/images/moon/get_assets.py --force        # re-download

Every step is resumable; re-running costs one hash pass per file.

Steps
-----
1. venv      Create ``.venv`` (uv when present, else venv+pip) and install
             ``requirements.txt`` into it.
2. sources   Download the CGI Moon Kit pair (NASA SVS 4720) into
             ``moon-kit/``, sha256- and size-pinned. NASA's edge sometimes
             sends the leaf certificate without its Let's Encrypt
             intermediate; we then emulate the browser's AIA fetch once
             (fetch the CA-Issuers URI over http, add it as an extra trust
             anchor) -- the payload stays checksum-verified regardless.
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
    with _ssl_get(url) as r, open(tmp, "wb") as f:
        for chunk in iter(lambda: r.read(1 << 20), b""):
            f.write(chunk)
            h.update(chunk)
            n += len(chunk)
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
