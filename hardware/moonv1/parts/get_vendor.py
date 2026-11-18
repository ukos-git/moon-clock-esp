#!/usr/bin/env python3
"""One-shot Waveshare vendor-data acquisition for the Moon v1 board.

The Moon v1 design (hardware/moonv1/) needs the vendor's own mechanical +
schematic data to be trustworthy: the S3 AMOLED 1.75 STEP board model +
dimension drawing (FPC connector part/pitch/pinout), the P4 4C STEP +
dimension drawing (board outline for the housing seat), and the two
schematic PDFs (power tree, FPC pin-for-pin table). None of that is
committed -- the archives are ~21 MB and the extracted STEP sets ~87 MB --
so this script is the single tracked entry point that re-acquires them
into the gitignored ``vendor/`` dir next to it, sha256- and size-pinned
(the same pattern as ``assets/images/moon/get_assets.py`` / ``moon-kit/``).

Run with stdlib python3:

    python3 hardware/moonv1/parts/get_vendor.py            # all files
    python3 hardware/moonv1/parts/get_vendor.py --force    # re-download

Every step is resumable; re-running costs one hash pass per file.
"""

import argparse
import hashlib
import sys
import urllib.request
import zipfile
from pathlib import Path

VENDOR_DIR = Path(__file__).resolve().parent / "vendor"

# (url, local name, sha256, size bytes). Fetched 2026-11-19 from the
# Waveshare wiki resource pages (docs.waveshare.com/.../Resources-And-Documents).
PINNED = [
    (
        "https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/"
        "ESP32-S3-Touch-AMOLED-1.75.pdf",
        "ESP32-S3-Touch-AMOLED-1.75.pdf",
        "26d33d14ae975f8d6817d103c41e704dc0682c8b28e61aed845dba933e9133e3",
        2193937,
    ),
    (
        "https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/"
        "ESP32-S3-Touch-AMOLED-1.75-3D.zip",
        "ESP32-S3-Touch-AMOLED-1.75-3D.zip",
        "71dd45581ea01ad6c275c7d311ddac603d603538332b4f79c2b7059cb909ff4d",
        4819412,
    ),
    (
        "https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/"
        "ESP32-S3-Touch-AMOLED-1.75-B-3D.zip",
        "ESP32-S3-Touch-AMOLED-1.75-B-3D.zip",
        "f3f47d061fe03a3632e60540b7d2aff748580d69d48f9f11fca0d2543f0ddd49",
        184612,
    ),
    (
        "https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC/"
        "ESP32-P4-WIFI6-Touch-LCD-XC-Schematic.pdf",
        "ESP32-P4-WIFI6-Touch-LCD-XC-Schematic.pdf",
        "7a12998f1e641c49ab5e38f4765bdaf76aad2269e908e87ccffd572316647d68",
        1869787,
    ),
    (
        "https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC/"
        "ESP32-P4-WIFI6-TOUCH-LCD-3_4C.zip",
        "ESP32-P4-WIFI6-TOUCH-LCD-3_4C.zip",
        "e98cb43946f658f95ad6c238f19f137c604cebcc0dcb715fde9e0a0670adb5da",
        5169965,
    ),
    (
        "https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC/"
        "ESP32-P4-WIFI6-TOUCH-LCD-4C-3D.zip",
        "ESP32-P4-WIFI6-TOUCH-LCD-4C-3D.zip",
        "8bbd1515fe38d5a509029ef4f8603df459c138435c250faad362f8bc8c675b56",
        5267252,
    ),
]

CHUNK = 1 << 20


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "moon-phase-vendor-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=120) as r, tmp.open("wb") as f:
        for chunk in iter(lambda: r.read(CHUNK), b""):
            f.write(chunk)
    tmp.rename(dest)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--force", action="store_true", help="re-download pinned files")
    ap.add_argument("--extract", action="store_true",
                    help="also unzip the archives next to themselves")
    args = ap.parse_args()

    VENDOR_DIR.mkdir(parents=True, exist_ok=True)
    failures = 0
    for url, name, want_sha, want_size in PINNED:
        dest = VENDOR_DIR / name
        if dest.exists() and not args.force:
            got_sha, got_size = sha256_of(dest), dest.stat().st_size
            if got_sha == want_sha and got_size == want_size:
                print(f"ok    {name}")
            else:
                print(f"stale {name} (sha {got_sha[:12]} / {got_size} B) -- re-fetching")
                dest.unlink()
        if not dest.exists() or args.force:
            print(f"fetch {url}")
            try:
                fetch(url, dest)
            except Exception as e:  # noqa: BLE001 - report and continue
                print(f"FAIL  {name}: {e}", file=sys.stderr)
                failures += 1
                continue
            got_sha, got_size = sha256_of(dest), dest.stat().st_size
            if got_sha != want_sha or got_size != want_size:
                dest.unlink()
                print(f"FAIL  {name}: pin mismatch (sha {got_sha[:12]} / {got_size} B)",
                      file=sys.stderr)
                failures += 1
                continue
            print(f"ok    {name}")
        if args.extract and dest.suffix == ".zip" and not dest.with_suffix("").exists():
            out = VENDOR_DIR / dest.stem
            with zipfile.ZipFile(dest) as z:
                z.extractall(out)
            print(f"ok    extracted -> {out.relative_to(VENDOR_DIR)}/")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
