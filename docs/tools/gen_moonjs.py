"""Generate the embedded data for the book's interactive moon simulator.

Writes docs/book/_static/moonjs/moonlab_data.js with:

  - alb: the S3 target's 864x432 uint8 equirect albedo
    (assets/images/moon/out/s3/albedo.bin, the same 8-bit texture the C
    core bakes from).
  - dem: the relief DEM downsampled to 1024x512 int8 (assets/images/moon/
    out/s3/dem.bin is 2048x1024; 2x2 box average of the int8 codes -- the
    codes are a near-linear meters map so the average keeps the slopes).
  - lut: the 256-entry tone LUT (assets/images/moon/tone_lut.bin).
  - geo: Float32 table, one row of 14 floats per timestamp (2-hourly over
    2026 + 2027 — the sim's date slider spans both years, 8760 steps):
    S_body, E_body, right_body, up_body (3 each), phase angle (deg),
    illumination. Computed with the SAME code path as the
    validated Python prototype (moon.astro: PyERFA ephemeris + the
    calibrated IAU_Moon body frame), so the sim's geometry is the
    validated geometry.

Run:  host_sim/.venv/bin/python docs/book/tools/gen_moonjs.py
(only needs numpy + astropy -- the gate venv).
"""
from __future__ import annotations

import base64
import os
import sys

import numpy as np
from astropy.coordinates import get_body_barycentric
from astropy.time import Time
import astropy.units as u

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MOON_DIR = os.path.join(REPO, "assets", "images", "moon")
sys.path.insert(0, MOON_DIR)
OUT = os.path.join(HERE, "..", "_static", "moonjs", "moonlab_data.js")

# The calibrated IAU_Moon constants (moon/constants.py, same as moon_consts.h).
from moon import constants as C  # noqa: E402

# --- texture data ----------------------------------------------------------
# The book sim bakes from the S3 target's texture (864x432 albedo): the
# interactive sim runs in a browser, and the S3 albedo is the shared
# baseline (P4's 2048x1024 albedo would more than double the embedded JS).
S3 = C.TARGETS["s3"]
S3_DIR = os.path.join(MOON_DIR, "out", "s3")
if not os.path.exists(os.path.join(S3_DIR, "albedo.bin")):
    sys.exit("missing %s/albedo.bin -- run: cd assets/images/moon && "
             ".venv/bin/python -m moon.assets --target s3" % S3_DIR)
alb = np.fromfile(os.path.join(S3_DIR, "albedo.bin"), dtype=np.uint8).reshape(
    S3["alb"][1], S3["alb"][0])
dem = np.fromfile(os.path.join(S3_DIR, "dem.bin"), dtype=np.int8).reshape(
    S3["dem"][1], S3["dem"][0])
lut = np.fromfile(os.path.join(MOON_DIR, "tone_lut.bin"), dtype=np.uint8)

# 2x2 box average to 1024x512 (the JS side rebuilds the gradient cache at
# this resolution, same central differences as moon_relief_build).
dem_ds = dem.reshape(512, 2, 1024, 2).mean(axis=(1, 3)).round().astype(np.int8)

# --- geometry table, 2-hourly over 2026 + 2027 ------------------------------
# Vectorized port of moon.astro.geometry (array Time -> array positions).
J2000 = 2451545.0
P = np.array([np.cos(np.radians(C.POLE_DEC_ICRF)) * np.cos(np.radians(C.POLE_RA_ICRF)),
              np.cos(np.radians(C.POLE_DEC_ICRF)) * np.sin(np.radians(C.POLE_RA_ICRF)),
              np.sin(np.radians(C.POLE_DEC_ICRF))])
W0 = C.PRIME_MER_W0
RATE = C.PRIME_MER_RATE_DEG_DAY


def _unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def rodrigues(v, axis, th):
    """Rotate array of unit vectors v (N,3) about unit axis by th (N, rad)."""
    c = np.cos(th)[:, None]
    s = np.sin(th)[:, None]
    a = axis[None, :]
    return v * c + np.cross(a, v) * s + a * ((v * a).sum(-1))[:, None] * (1.0 - c)


def body_basis_th(W_rad):
    """Body basis (x, y, z=P) in ICRF for an array of prime-meridian angles."""
    x0 = np.array([P[2], 0.0, -P[0]])
    x0 = x0 / np.linalg.norm(x0)
    y0 = np.cross(P, x0)
    y0 = y0 / np.linalg.norm(y0)
    return rodrigues(np.broadcast_to(x0, (len(W_rad), 3)).copy(), P, W_rad), \
        rodrigues(np.broadcast_to(y0, (len(W_rad), 3)).copy(), P, W_rad), P[None, :]


n = 8760  # 2-hourly over TWO years (2026-01-01T00:00 -> 2027-12-31T22:00) --
          # the sim's date slider spans both years (8760 steps of 2 h).
jd0 = 2461042.5  # 2026-01-01T00:00 UTC
jd = np.linspace(jd0, jd0 + 730.0, n)
t = Time(jd, format="jd", scale="utc")

def bary(name):
    p = np.asarray(get_body_barycentric(name, t, ephemeris=C.EPHEMERIS).xyz.to_value(u.au))
    return p.T if p.shape == (3, n) else p  # astropy returns (3, N) for array times


sun = bary("Sun")
moon = bary("Moon")
earth = bary("Earth")

S = _unit(sun - moon)
E = _unit(earth - moon)
L = _unit(moon - earth)

W = W0 + RATE * (jd - J2000)
bx, by, bz = body_basis_th(np.radians(W))
def dots(a, b):
    return (a * b).sum(-1)


S_body = np.stack([dots(S, bx), dots(S, by), dots(S, bz)], axis=-1)
E_body = np.stack([dots(E, bx), dots(E, by), dots(E, bz)], axis=-1)

# Camera basis (celestial-north-up, east-left), ICRF then body frame.
Nsky = np.zeros((n, 3))
Nsky[:, 2] = 1.0
north = _unit(Nsky - dots(Nsky, L)[:, None] * L)
east = _unit(np.cross(north, L))
right = -east
r_body = np.stack([dots(right, bx), dots(right, by), dots(right, bz)], axis=-1)
u_body = np.stack([dots(north, bx), dots(north, by), dots(north, bz)], axis=-1)

cos_i = np.clip(dots(S, E), -1.0, 1.0)
phase_deg = np.degrees(np.arccos(cos_i))
illum = (1.0 + cos_i) / 2.0

geo = np.empty((n, 14), dtype=np.float32)
geo[:, 0:3] = S_body
geo[:, 3:6] = E_body
geo[:, 6:9] = r_body
geo[:, 9:12] = u_body
geo[:, 12] = phase_deg
geo[:, 13] = illum
assert geo.shape == (n, 14), geo.shape

# --- emit ------------------------------------------------------------------
b = lambda a: base64.b64encode(np.ascontiguousarray(a).tobytes()).decode()

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    f.write("/* Generated by docs/book/tools/gen_moonjs.py -- do not edit. */\n")
    f.write("window.MOONLAB_DATA = {\n")
    f.write("  ALB_W: %d, ALB_H: %d,\n" % (S3["alb"][0], S3["alb"][1]))
    # The DEM min/max/steps are the device constants (components/moon/include/
    # moon_consts.h) -- the JS ports the C bake, so it must use the C map.
    f.write("  DEM_W: 1024, DEM_H: 512,\n")
    f.write("  DEM_STEPS: 254,\n")
    f.write("  DEM_MIN_M: -8518.19921875,\n")
    f.write("  DEM_MAX_M: 10416.568359375,\n")
    f.write("  RADIUS_M: 1737400.0,\n")
    f.write("  TEX_LON_OFFSET: 180.0,\n")
    f.write("  ES_A: 30.455, ES_B: -11.733, ES_C: -2.615,\n")
    f.write("  GEO_N: %d, GEO_STEP_H: %.6f, GEO_START_JD: %.6f,\n"
            % (n, (jd[-1] - jd[0]) / (n - 1) * 24.0, jd[0]))
    f.write('  alb: "%s",\n' % b(alb))
    f.write('  dem: "%s",\n' % b(dem_ds))
    f.write('  lut: "%s",\n' % b(lut))
    f.write('  geo: "%s"\n' % b(geo))
    f.write("};\n")

print("wrote", OUT, os.path.getsize(OUT), "bytes;", n, "geometry rows")
