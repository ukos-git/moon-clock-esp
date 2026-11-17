"""Asset derivation: the equirect albedo + int8 relief DEM the C kernel
samples.

Design (measured, not assumed). The on-device model is compute-light: a
phase-INDEPENDENT static asset indexed by texture coordinates; the lighting
(the terminator) is applied live from the per-frame camera basis, and the
"ball in hand" is an in-plane rotation of the baked 8-bit frame.

The asset is an **equirect map sampled with the live per-frame camera basis**
-- NOT a disk-space map baked at one reference view. Baking in disk space
freezes the sub-Earth libration (+/-6.3 deg) and the annual posangle roll
(+/-18.7 deg) into the texture: the round-trip of that design measured MAE
32 / SSIM 0.63 (small at crescent, ~45 at full moon -- the
view-misregistration signature). Equirect sampling makes libration and
posangle exact; only texture resolution is approximate.

**Relief** (mountain shadows at the terminator) is a dedicated int8 DEM,
``dem.bin`` (2048x1024, ~10.5 km/px, the full CGI Moon Kit ``ldem_16.tif``
height map downsampled). The bake computes a 4-neighbor central-difference
surface normal from it and applies the displaced-normal Lambert
``L = clip(lns, 0, 1)`` over the WHOLE lit disk (``lns = (d - G.S)/sqrt(1+|G|^2)``,
unclipped) -- the physically exact displaced-heightfield model. The full-disk
form was chosen over the old thin-band blend because, at matched 2048 DEM
resolution, it is the only variant whose terminator-local contrast
(mean |grad| in a 48-px box at the terminator) reaches the NASA level
(~27 vs the NASA frames' 27); every banded/blended variant plateaued at
8-18 there. Measured over the 32-frame gate set (864x432 albedo, 2048x1024
DEM, full-disk relief): MAE 6.45 / SSIM 0.900 (gate <=12 / >=0.89).

The albedo size is PER-BOARD (``constants.TARGETS``): the two boards have
different moon_assets partition and PSRAM budgets, and both default to the
best resolution that fits the board (resolution over speed). The DEM is
2048x1024 on both. The Kconfig MOON_ALBEDO_W defaults mirror the table; the
root CMakeLists guard cross-checks the built blob against the configured
value and the board's partition size.

  s3 (466 px, 16 MB flash, 8 MB PSRAM): 864x432 albedo (373,248 B)
      + 2048x1024 DEM (2,097,152 B) = 2,470,400 B < 2,621,440 B (2560K).
  p4 (720 px, 32 MB flash, 32 MB PSRAM): 2048x1024 albedo (2,097,152 B)
      + 2048x1024 DEM (2,097,152 B) = 4,194,304 B < 5,242,880 B (5120K).
      (A 2560-wide albedo would need >= 5376K.)

The per-pixel bake cost is DEM-resolution-independent (a fixed 4-neighbor
gather), so the 2048 DEM does not slow the bake; the disk is an analytic
circle test at runtime (no disk asset).

Emits (one dir per board target, ``<out>/<target>/``):
  albedo.bin     WxH u8         sRGB->luma albedo, row 0 = north (lat +90),
                                 col 0 = longitude 0 (map origin; see
                                 constants.TEXTURE_LON_OFFSET_DEG)
  dem.bin        WxH i8         LOLA height (per-board size, constants.
                                 TARGETS[t]["dem"]), same equirect convention;
                                 code = round((h - MIN) / (MAX - MIN) * 254)
                                 - 128 (meters; MIN/MAX in moon_consts.h)
  moon_assets.bin  albedo.bin ++ dem.bin (the raw moon_assets partition
                   image the device flashes and reads as one PSRAM block)
  moon_consts.h  generated C header (the single source of truth for the
                 phase/terminator math between Python and C; also copied
                 to the tracked components/moon/generated/<t>/ location)
  moon_assets_gen.c  both blobs as C arrays (host simulator only)
  moon_assets_manifest.json  single-line JSON (alb_w, combined_bytes, ...)
                   for the root CMakeLists consistency guard

All of this stays gitignored (assets/ is ignored wholesale); the only
tracked generated artifacts are the two per-board moon_consts.h headers.

Round-trip: re-render the 32-frame gate set FROM THESE MAPS (not the 4K
textures) with the exact device lighting (full-disk displaced-normal
Lambert), and diff vs NASA with the same gate metrics.

Run:  python -m moon.assets [--target s3 p4] [--alb WxH] [--out <root>]
      [--roundtrip]     (default: both targets, sizes from TARGETS)
"""
from __future__ import annotations

import argparse
import json
import os
import shutil

import numpy as np
from PIL import Image

from . import constants as C
from .astro import geometry
from .renderer import (REF_CX, REF_CY, REF_RADIUS, REF_SIZE, tonemap,
                       to_uint8)
from .register import register_shift
from .metrics import disk_metrics
from .sphere import disk_grid
from .textures import load_albedo, load_height

# Relief DEM (int8, equirect, same convention as the albedo). 2048x1024
# (~10.5 km/px) is the resolution at which the displaced-normal terminator
# contrast matches the NASA frames (terminator-local |grad| ~27 vs NASA's
# 27; 512x256 averaged the crater relief out and read 3-7x too soft) --
# the default here; the size actually baked is PER-BOARD from
# constants.TARGETS[t]["dem"] (the knob halves it for PSRAM; the gate is
# the arbiter, see that row). The albedo size is likewise per-board.
DEM_W = 2048
DEM_H = 1024
DEM_STEPS = 254  # code range used when quantizing (-128..126)


def bake_albedo(full: np.ndarray, width: int, height: int) -> np.ndarray:
    """Downsample the full-resolution equirect albedo (float 0..255,
    row 0 = north) to ``height x width`` u8 (bilinear/bicubic). The size is
    per-board (constants.TARGETS); equirect 2:1."""
    a = np.clip(np.round(full), 0, 255).astype(np.uint8)
    return np.asarray(Image.fromarray(a).resize((width, height),
                                                Image.BICUBIC),
                      dtype=np.uint8)


def bake_dem(h_full: np.ndarray, width: int = DEM_W,
             height: int = DEM_H) -> tuple[np.ndarray, float, float]:
    """Downsample the full-resolution LOLA height (meters, row 0 = north)
    to ``height x width`` and quantize to int8 over the full min..max
    range. Returns ``(dem, lo, hi)`` in meters (``lo``/``hi`` are the float
    map's min/max, so meters-per-code-step is ``(hi - lo) / DEM_STEPS``)."""
    h = np.asarray(Image.fromarray(h_full.astype(np.float32)).resize(
        (width, height), Image.BILINEAR))
    lo, hi = float(h.min()), float(h.max())
    q = np.round((h - lo) / (hi - lo) * DEM_STEPS).astype(np.int32) - 128
    assert q.min() >= -128 and q.max() <= 127
    return q.astype(np.int8), lo, hi


def relief_lns(r_body: np.ndarray, g, dem: np.ndarray,
               m_per_step: float) -> np.ndarray:
    """Unclipped displaced-normal sun dot ``(d - G.S)/sqrt(1 + |G|^2)`` for
    every pixel, where N is the DEM-derived surface normal (4-neighbor
    central difference) and d = r.S.

    Ports moon_bake.c::relief_lns 1:1 (the C-vs-Python gate depends on the
    two agreeing pixel-for-pixel). Texture coords share the albedo's
    equirect convention; the tangent basis is built from the body-frame
    direction itself (no lon/lat transcendentals): for a unit r,
    east = (-ry, rx, 0)/rho and north = (-rz*rx, -rz*ry, rho)/rho with
    rho = sqrt(rx^2 + ry^2) = cos(lat). Since east/north are tangent,
    N = normalize(r - G) gives N.S = (d - G.S)/sqrt(1 + |G|^2).

    UNCLIPPED: this is the exact displaced-heightfield Lambert, which goes
    NEGATIVE on slopes that face away from the Sun -- the shadow side of a
    crater ridge at the terminator. (The old ``max(N.S, 0)`` floor is what
    kept those shadows from going fully dark, leaving the terminator 3-7x
    too soft vs the NASA frames.) The caller clips at 0 for the dark side.
    """
    W, H = dem.shape[1], dem.shape[0]
    rx, ry, rz = r_body[..., 0], r_body[..., 1], r_body[..., 2]
    theta = np.degrees(np.arctan2(ry, rx)) % 360.0
    lat = np.degrees(np.arcsin(np.clip(rz, -1.0, 1.0)))
    colf = ((theta + C.TEXTURE_LON_OFFSET_DEG) % 360.0) / 360.0 * W
    rowf = (90.0 - lat) / 180.0 * H
    c = np.clip(np.floor(colf + 0.5).astype(np.int64), 0, W - 1)
    r = np.clip(np.floor(rowf + 0.5).astype(np.int64), 0, H - 1)
    cp = (c + 1) % W
    cm = (c - 1) % W
    rn = np.clip(r - 1, 0, H - 1)
    rs = np.clip(r + 1, 0, H - 1)
    h_e = dem[r, cp].astype(np.float64)
    h_w = dem[r, cm].astype(np.float64)
    h_n = dem[rn, c].astype(np.float64)
    h_s = dem[rs, c].astype(np.float64)

    dlon = 2.0 * np.pi / W  # radians per texture pixel (longitude)
    dlat = np.pi / H        # radians per texture pixel (latitude)
    rho = np.sqrt(np.maximum(rx * rx + ry * ry, 0.0))
    rho = np.maximum(rho, 1e-4)  # poles: keep the tangent basis finite
    rho_c = np.maximum(rho, 0.05)  # longitude arc-length floor (polar cap)
    inv = 1.0 / rho
    glon = (h_e - h_w) * m_per_step / (2.0 * C.MOON_RADIUS_M * rho_c * dlon)
    glat = (h_n - h_s) * m_per_step / (2.0 * C.MOON_RADIUS_M * dlat)
    S = g.S_body
    east_s = (-ry * S[0] + rx * S[1]) * inv
    north_s = (-rz * rx * S[0] - rz * ry * S[1] + rho * S[2]) * inv
    gs = glon * east_s + glat * north_s
    d = r_body @ S
    lns = (d - gs) / np.sqrt(1.0 + glon * glon + glat * glat)
    return lns


def render_from_assets(g, alb: np.ndarray, dem: np.ndarray | None = None,
                       m_per_step: float = 0.0, size: int = REF_SIZE,
                       radius: float = REF_RADIUS, cx: float = REF_CX,
                       cy: float = REF_CY) -> tuple[np.ndarray, np.ndarray]:
    """Render one frame FROM THE ASSET MAPS with the exact device lighting:
    full-disk displaced-normal Lambert. ``L = clip(lns, 0, 1)`` where lns is
    the DEM relief-normal sun dot -- applied over the WHOLE lit disk (not
    just a terminator band), which is the physically exact displaced-
    heightfield model and is what makes the mountain shadows read correctly.
    ``dem = None`` runs the relief-free variant (``L = clip(d, 0, 1)``).
    Returns ``(raw, d)`` like ``renderer.render`` so the caller applies the
    same earthshine + tone LUT. Isolates asset-resolution error (libration
    and posangle are exact -- the camera basis is live)."""
    u, v, w, mask = disk_grid(size, radius, cx, cy)
    if not mask.any():
        zero = np.zeros((size, size), dtype=np.float64)
        return zero, zero

    r_body = (u[..., None] * g.right_body[None, None, :]
              + v[..., None] * g.up_body[None, None, :]
              + w[..., None] * g.E_body[None, None, :])
    lon_body = np.degrees(np.arctan2(r_body[..., 1], r_body[..., 0])) % 360.0
    lat_body = np.degrees(np.arcsin(np.clip(r_body[..., 2], -1.0, 1.0)))
    lon_tex = (lon_body + C.TEXTURE_LON_OFFSET_DEG) % 360.0

    # Albedo: NEAREST-NEIGHBOR, mirroring moon_bake.c's single PSRAM gather
    # per pixel (compute-light; the albedo texture is >= 1 texel per screen
    # px across the disk -- ~1.08 at 864, ~2.56 at 2048 -- so NN is
    # sub-pixel accurate). The C-vs-
    # Python porting gate depends on both using the SAME sampler -- with
    # bilinear here the two differed by up to ~1.3 MAE at high phase.
    colf = (lon_tex / 360.0) * alb.shape[1]
    rowf = (90.0 - lat_body) / 180.0 * alb.shape[0]
    c = np.clip(np.floor(colf + 0.5).astype(int), 0, alb.shape[1] - 1)
    r = np.clip(np.floor(rowf + 0.5).astype(int), 0, alb.shape[0] - 1)
    A = alb[r, c].astype(np.float64)

    # Flat-sphere dot: the earthshine driver (x = max(0, -d)).
    d = r_body @ g.S_body
    if dem is None:
        L = np.clip(d, 0.0, None)
    else:
        # Full-disk displaced-normal Lambert: the relief normal applies
        # everywhere on the lit disk. lns is unclipped (negative on slopes
        # facing away from the Sun); clip at 0 so the dark side gets no
        # direct light (the earthshine term below carries it).
        L = np.clip(relief_lns(r_body, g, dem, m_per_step), 0.0, None)
    I = A * L
    out = np.zeros((size, size), dtype=np.float64)
    out[mask] = I[mask]
    d_full = np.zeros((size, size), dtype=np.float64)
    d_full[mask] = d[mask]
    return out, d_full


def _ref_path(i: int, here: str) -> str:
    return os.path.join(here, "final_400x400", "moon.%04d.png" % (i + 1))


def _roundtrip(here: str, alb: np.ndarray, dem: np.ndarray,
               m_per_step: float, json_path: str, n: int) -> int:
    """Re-render the gate set from the equirect asset maps vs NASA;
    gate metrics."""
    from .validate import select_frames, _iso
    rec = json.load(open(json_path))
    frames = select_frames(rec, n)
    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)
    rows = []
    n_fail_mae = 0
    for i, phase in frames:
        p = _ref_path(i, here)
        if not os.path.exists(p):
            continue
        ref_rgba = np.asarray(Image.open(p))
        ref = np.asarray(Image.open(p).convert("L"), dtype=np.float64)
        g = geometry(_iso(rec[i]["time"]))
        raw, d = render_from_assets(g, alb, dem, m_per_step)
        shifted, shift = register_shift(to_uint8(raw).astype(np.float64), ref)
        out = to_uint8(tonemap(shifted, d, lut))
        m = disk_metrics(None, ref_rgba, shifted=out.astype(np.float64))
        rows.append((i + 1, phase, m["mae"], m["ssim"]))
        if m["mae"] > C.MAE_MAX:
            n_fail_mae += 1
    print("\n== Round-trip (rendered from the %dx%d equirect asset + %dx%d DEM) =="
          % (alb.shape[1], alb.shape[0], dem.shape[1], dem.shape[0]))
    print("  frame  phase    MAE   SSIM")
    for i, phase, mae, ssim in rows:
        flag = " *" if mae > C.MAE_MAX else ""
        print("  %4d  %6.2f  %6.2f %6.3f%s" % (i, phase, mae, ssim, flag))
    mae = np.mean([r[2] for r in rows])
    ssim = np.mean([r[3] for r in rows])
    print("  mean MAE %.2f (max %.2f, %d frames > %.0f)  "
          "mean content SSIM %.3f   (gate: per-frame <=%.0f / mean >=%.2f)"
          % (mae, max(r[2] for r in rows), n_fail_mae, C.MAE_MAX, ssim,
             C.MAE_MAX, C.SSIM_MIN))
    ok = n_fail_mae == 0 and ssim >= C.SSIM_MIN
    print("  %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def gen_consts_header(out_path: str, dem_lo: float, dem_hi: float,
                      alb_w: int, alb_h: int,
                      dem_w: int = DEM_W, dem_h: int = DEM_H) -> None:
    """Emit moon_consts.h from ``constants.py`` (single source of truth).
    ``dem_lo``/``dem_hi`` (meters) pin the DEM's quantization range so C
    and Python use the same meters-per-code-step; ``alb_w``/``alb_h`` and
    ``dem_w``/``dem_h`` are the per-board asset sizes (constants.TARGETS)."""
    lut = C.TONE_LUT
    lut_body = ",\n    ".join(
        ", ".join(str(int(x)) for x in lut[i:i + 8])
        for i in range(0, 256, 8))
    hdr = f"""// GENERATED by `python -m moon.assets` from moon/constants.py -- do not edit.
// Single source of truth for the moon phase/terminator math (Python + C).
#ifndef MOON_CONSTS_H
#define MOON_CONSTS_H

// --- Calibrated IAU_Moon body frame (pole + prime meridian) ---
#define MOON_POLE_RA_ICRF   {C.POLE_RA_ICRF}
#define MOON_POLE_DEC_ICRF  {C.POLE_DEC_ICRF}
#define MOON_PRIME_MER_W0   {C.PRIME_MER_W0}
#define MOON_PRIME_MER_RATE {C.PRIME_MER_RATE_DEG_DAY}   // deg / day
#define MOON_J2000_JD       {C.J2000_JD}

// --- Geometry / rendering ---
#define MOON_DISK_RADIUS_PX {C.DISK_RADIUS_PX}
#define MOON_IMAGE_SIZE     {C.IMAGE_SIZE}
#define MOON_RADIUS_M       {C.MOON_RADIUS_M}
#define MOON_POSANGLE_SIGN  {C.POSANGLE_SIGN}
#define MOON_TEX_LON_OFFSET {C.TEXTURE_LON_OFFSET_DEG}

// --- Equirect albedo asset (row 0 = north, col 0 = longitude 0) ---
#define MOON_ALB_W          {alb_w}
#define MOON_ALB_H          {alb_h}

// --- Relief DEM asset (int8, same equirect convention) ---
// LOLA height quantized to int8 over [MOON_DEM_MIN, MOON_DEM_MAX] meters:
// code = round((h - MIN) / (MAX - MIN) * MOON_DEM_STEPS) - 128. The bake
// computes a 4-neighbor central-difference normal and applies the
// displaced-normal Lambert L = clip(lns, 0, 1) over the WHOLE lit disk
// (lns = (d - G.S)/sqrt(1 + |G|^2), unclipped) -- the physically exact
// displaced-heightfield model (see moon_bake.c).
#define MOON_DEM_W          {dem_w}
#define MOON_DEM_H          {dem_h}
#define MOON_DEM_STEPS      {DEM_STEPS}
#define MOON_DEM_MIN_M      {dem_lo}
#define MOON_DEM_MAX_M      {dem_hi}

// --- Earthshine (phase-independent dark-side gradient) ---
// R = A*x + B*x^2 + C*x^3, x = max(0, -d), d = r_hat . S. 8-bit units.
#define MOON_ES_A {C.EARTHSHINE_A}
#define MOON_ES_B {C.EARTHSHINE_B}
#define MOON_ES_C {C.EARTHSHINE_C}

// --- 256-entry 8-bit tone LUT (bin 0 == 0: pure dark side) ---
static const unsigned char MOON_TONE_LUT[256] = {{
    {lut_body}
}};

#endif  // MOON_CONSTS_H
"""
    with open(out_path, "w") as f:
        f.write(hdr)


def gen_assets_c(out_path: str, alb: np.ndarray, dem: np.ndarray) -> None:
    """Emit the albedo + DEM blobs as C arrays (host simulator only)."""
    flat_a = alb.tobytes()
    flat_d = dem.tobytes()
    lines = [
        "// GENERATED by `python -m moon.assets` -- host simulator assets.",
        "// Do not link into the device app (size); the device reads",
        "// moon_assets.bin (albedo.bin ++ dem.bin) from the moon_assets",
        "// raw partition.",
        "#include <stdint.h>",
        "const uint8_t MOON_ALBEDO[%d] = {" % len(flat_a),
    ]
    for i in range(0, len(flat_a), 16):
        lines.append("    " + ", ".join(str(b) for b in flat_a[i:i + 16]) + ",")
    lines.append("};")
    lines.append("// %dx%d equirect albedo, row 0 = north, col 0 = longitude 0."
                 % (alb.shape[1], alb.shape[0]))
    lines.append("const int8_t MOON_DEM[%d] = {" % len(flat_d))
    # int8 codes: print signed so the array stays debuggable.
    codes = np.frombuffer(flat_d, dtype=np.int8)
    for i in range(0, len(codes), 16):
        lines.append("    " + ", ".join(str(int(b)) for b in codes[i:i + 16]) + ",")
    lines.append("};")
    lines.append("// %dx%d equirect relief DEM (int8 meters), same convention."
                 % (dem.shape[1], dem.shape[0]))
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


def _bake_target(t: str, alb_w: int, alb_h: int, full: np.ndarray,
                 h_full: np.ndarray, here: str, repo: str,
                 out_root: str) -> None:
    """Bake one board target's assets into ``<out_root>/<t>/`` and copy the
    generated header to the tracked per-board location."""
    spec = C.TARGETS[t]
    part_b = spec["partition_kib"] * 1024
    dem_w, dem_h = spec["dem"]
    out = os.path.join(out_root, t)
    os.makedirs(out, exist_ok=True)

    print("[%s] baking albedo %dx%d from full-resolution %dx%d equirect"
          % (t, alb_w, alb_h, full.shape[1], full.shape[0]))
    alb = bake_albedo(full, alb_w, alb_h)
    alb.tofile(os.path.join(out, "albedo.bin"))
    print("[%s]   albedo.bin %d bytes" % (t, alb.size))

    print("[%s] baking DEM %dx%d from full-resolution %dx%d height map"
          % (t, dem_w, dem_h, h_full.shape[1], h_full.shape[0]))
    dem, dem_lo, dem_hi = bake_dem(h_full, dem_w, dem_h)
    dem.tofile(os.path.join(out, "dem.bin"))
    m_per_step = (dem_hi - dem_lo) / DEM_STEPS
    print("[%s]   dem.bin %d bytes (%.0f m/level)" % (t, dem.size,
                                                      m_per_step))

    # The raw moon_assets partition image: albedo ++ dem (the device reads
    # it as one PSRAM block and slices it).
    total = alb.size + dem.size
    with open(os.path.join(out, "moon_assets.bin"), "wb") as f:
        f.write(alb.tobytes())
        f.write(dem.tobytes())
    if total > part_b:
        need = (total + 1023) // 1024
        raise SystemExit(
            "[%s] moon_assets.bin %d B does not fit the %dK "
            "moon_assets partition (%d B); it needs >= %dK. Widen the "
            "partition (partitions_p4.csv / partitions.csv) or shrink the "
            "albedo." % (t, total, spec["partition_kib"], part_b, need))
    print("[%s]   moon_assets.bin %d bytes (%d + %d; partition %dK = %d B)"
          % (t, total, alb.size, dem.size, spec["partition_kib"], part_b))

    # Steady-state asset PSRAM: the albedo + the DEM + the two dusk shade
    # maps (each albedo-sized) live in PSRAM at the same time.
    steady = 3 * alb.size + dem.size
    print("[%s]   steady-state asset PSRAM ~%.2f MiB (albedo + DEM + two "
          "shade maps) of %d MiB" % (t, steady / 1048576.0,
                                     spec["psram_mib"]))

    gen_consts_header(os.path.join(out, "moon_consts.h"), dem_lo, dem_hi,
                      alb_w, alb_h, dem_w, dem_h)
    gen_assets_c(os.path.join(out, "moon_assets_gen.c"), alb, dem)
    print("[%s]   wrote moon_consts.h, moon_assets_gen.c" % t)

    # The tracked per-board header (everything else under assets/ is
    # gitignored; the build's include-dir switch picks this one up).
    hdr_src = os.path.join(out, "moon_consts.h")
    hdr_dst = os.path.join(repo, C.HEADER_PATHS[t])
    os.makedirs(os.path.dirname(hdr_dst), exist_ok=True)
    shutil.copyfile(hdr_src, hdr_dst)
    print("[%s]   copied header -> %s" % (t, C.HEADER_PATHS[t]))

    # Single-line manifest for the root CMakeLists consistency guard.
    manifest = {
        "target": t,
        "alb_w": alb_w,
        "alb_h": alb_h,
        "dem_w": dem_w,
        "dem_h": dem_h,
        "alb_bytes": alb.size,
        "dem_bytes": dem.size,
        "combined_bytes": total,
        "partition_kib": spec["partition_kib"],
    }
    with open(os.path.join(out, "moon_assets_manifest.json"), "w") as f:
        f.write(json.dumps(manifest, sort_keys=True,
                           separators=(",", ":")) + "\n")
    print("[%s]   wrote moon_assets_manifest.json" % t)
    return alb, dem, m_per_step


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None,
                    help="output ROOT dir (default: <pkg>/out; each target "
                         "writes to <root>/<target>/)")
    ap.add_argument("--target", nargs="+", choices=sorted(C.TARGETS),
                    default=list(C.TARGETS),
                    help="board targets to bake (default: both)")
    ap.add_argument("--alb", default=None, metavar="WxH",
                    help="override the albedo size for the selected "
                         "target(s) (equirect 2:1, even W, W <= 4096)")
    ap.add_argument("--json", default="/tmp/moon-research/mooninfo_2025.json")
    ap.add_argument("--roundtrip", action="store_true")
    ap.add_argument("--frames", type=int, default=32)
    args = ap.parse_args()

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    out_root = args.out or os.path.join(here, "out")

    override = None
    if args.alb:
        w_s, h_s = args.alb.lower().split("x")
        override = (int(w_s), int(h_s))
        if override[0] % 2 or override[1] != override[0] // 2:
            raise SystemExit("--alb must be equirect 2:1 with an even width")
        if override[0] > 4096:
            raise SystemExit("--alb width %d exceeds the 4096 source size"
                             % override[0])

    full = load_albedo()
    h_full = load_height()
    rc = 0
    for t in args.target:
        alb_w, alb_h = override or C.TARGETS[t]["alb"]
        alb, dem, mps = _bake_target(t, alb_w, alb_h, full, h_full, here,
                                     repo, out_root)
        if args.roundtrip:
            rc = max(rc, _roundtrip(here, alb, dem, mps,
                                    args.json, args.frames))
            if rc:
                break

    # One canonical location now: drop the pre-per-target top-level
    # artifacts (gitignored, regenerated under out/<t>/ by the runs above).
    for name in ("albedo.bin", "dem.bin", "moon_assets.bin",
                 "moon_assets_gen.c", "moon_consts.h"):
        p = os.path.join(here, name)
        if os.path.exists(p):
            os.remove(p)
            print("removed stale top-level %s (regenerated under %s/)"
                  % (name, out_root))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
