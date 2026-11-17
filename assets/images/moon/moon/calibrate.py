"""Calibrate the IAU_Moon body-frame rotation from NASA SVS 5415 ground truth.

astropy has no built-in ``IAU_Moon`` frame, so the body-frame rotation
(pole + prime meridian) is fitted empirically against the per-frame
``subsolar_lon/lat``, ``subearth_lon/lat`` published by NASA. Everything is
expressed in the same ICRF frame astropy gives (barycentric ``.xyz``), so the
fit is self-consistent and needs no ephemeris constants.

Pole
----
For one frame, the pole ``p`` (ICRF unit vector) satisfies two linear
equations from the two published latitudes::

    p . S = sin(lat_sub_solar)
    p . E = sin(lat_sub_earth)

with S, E ~166 deg apart, this has exactly two discrete solutions; a third
frame (or the full-year set) resolves the ambiguity. The pole is solved
exactly (no iterative fit), then scored over the whole year.

Prime meridian
--------------
The body-frame longitude of the sub-Earth direction is ``W(t) + f(E)`` where
``f(E)`` is the W=0 part. The implied prime meridian
``W_implied = subearth_lon - f(E)`` is linear in time (the Moon's prime
meridian is uniform, ~13.176 deg/day); a global linear least-squares fit
avoids the 360-deg periodic sign trap of a nonlinear longitude fit.

Run:  python -m moon.calibrate [--json /path/mooninfo_2025.json] [--n 500]
Prints the pole, prime meridian, max per-field residual, and paste-ready
constants for ``moon/constants.py``.
"""
from __future__ import annotations

import argparse
import json
import math
from datetime import datetime

import numpy as np


def _iso(tstr: str) -> str:
    """NASA '01 Jan 2025 00:00 UT' -> ISO '2025-01-01T00:00'."""
    return datetime.strptime(tstr.replace(" UT", ""), "%d %b %Y %H:%M").strftime("%Y-%m-%dT%H:%M")


def _unit(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def _dirs_icrf(utc: str):
    """Return (S, E) unit ICRF vectors: Moon->Sun, Moon->geocenter."""
    from astropy.coordinates import get_body_barycentric
    from astropy.time import Time
    import astropy.units as u

    t = Time(utc, scale="utc")
    sun = get_body_barycentric("Sun", t).xyz.to_value(u.au)
    moon = get_body_barycentric("Moon", t).xyz.to_value(u.au)
    earth = get_body_barycentric("Earth", t).xyz.to_value(u.au)
    return _unit(sun - moon), _unit(earth - moon)


def pole_cart(ra_deg, dec_deg):
    ra, dec = math.radians(ra_deg), math.radians(dec_deg)
    return np.array([math.cos(dec) * math.cos(ra),
                     math.cos(dec) * math.sin(ra),
                     math.sin(dec)])


def _lat(v, P):
    return math.degrees(math.asin(float(np.clip(v @ P, -1.0, 1.0))))


def _wrap(a):
    return (a + 180.0) % 360.0 - 180.0


# Body-frame basis: single source of truth lives in astro.py (the runtime path).
from .astro import body_basis, body_lonlat  # noqa: E402


# ---------------------------------------------------------------------------
# Pole: exact two-cone solution
# ---------------------------------------------------------------------------

def _pole_candidates(S, ss_lat_deg, E, se_lat_deg):
    """The two unit vectors p with p.S = sin(ss_lat) and p.E = sin(se_lat)."""
    s, e = _unit(S), _unit(E)
    c1 = math.sin(math.radians(ss_lat_deg))
    c2 = math.sin(math.radians(se_lat_deg))
    d = float(s @ e)
    denom = 1.0 - d * d
    if denom < 1e-9:
        return []
    x = (c1 - c2 * d) / denom
    y = (c2 - c1 * d) / denom
    p0 = x * s + y * e
    n = np.cross(s, e)
    sin_a = np.linalg.norm(n)
    if sin_a < 1e-9:
        return []
    n = n / sin_a
    t2 = 1.0 - float(p0 @ p0)
    if t2 < -1e-9:
        return []
    t = math.sqrt(max(0.0, t2))
    return [p0 + t * n, p0 - t * n]


def _lat_rms(p, frames):
    tot, cnt = 0.0, 0
    for fr in frames:
        dl1 = _lat(fr[0], p) - fr[2]
        dl2 = _lat(fr[1], p) - fr[3]
        tot += dl1 * dl1 + dl2 * dl2
        cnt += 2
    return math.sqrt(tot / cnt)


def fit_pole(frames):
    """Exact pole fit: candidates from frame 0, scored over all frames."""
    S0, E0, ss0, se0 = frames[0][0], frames[0][1], frames[0][2], frames[0][3]
    cands = _pole_candidates(S0, ss0, E0, se0)
    if not cands:
        # degenerate frame; try the next one
        for fr in frames[1:]:
            cands = _pole_candidates(fr[0], fr[2], fr[1], fr[3])
            if cands:
                break
    best = None
    for c in cands:
        rms = _lat_rms(c, frames)
        if best is None or rms < best[1]:
            best = (c, rms)
    p = _unit(best[0])
    # Report max per-frame residual too.
    mx = 0.0
    for fr in frames:
        mx = max(mx, abs(_lat(fr[0], p) - fr[2]), abs(_lat(fr[1], p) - fr[3]))
    ra = math.degrees(math.atan2(p[1], p[0])) % 360.0
    dec = math.degrees(math.asin(float(np.clip(p[2], -1.0, 1.0))))
    return ra, dec, mx, best[1]


# ---------------------------------------------------------------------------
# Body-frame basis + prime meridian
# ---------------------------------------------------------------------------

def _opt_W(E, P, se_lon):
    """W (deg, in [0,360)) minimizing the wrapped body_lon error for one frame.

    body_lon(E, W) advances ~linearly with W (a rigid rotation once the basis
    is correct), so a coarse grid + local refine lands the per-frame optimum.
    """
    best = None
    for W in np.arange(0.0, 360.0, 2.0):
        lon, _ = body_lonlat(E, P, W)
        err = abs(_wrap(lon - se_lon))
        if best is None or err < best[0]:
            best = (err, W)
    # refine around the best cell
    lo, hi = best[1] - 2.0, best[1] + 2.0
    for W in np.linspace(lo, hi, 81):
        lon, _ = body_lonlat(E, P, W)
        err = abs(_wrap(lon - se_lon))
        if best is None or err < best[0]:
            best = (err, W % 360.0)
    return best[1]


def fit_prime_meridian(frames, P):
    """Fit W(t) = W0 + rate*days via per-frame W optimization + linear fit.

    frames: (S, E, ss_lat, se_lat, ss_lon, se_lon, days_since_J2000).
    """
    days = np.array([f[6] for f in frames])
    se_lon = np.array([f[5] for f in frames])
    E = np.array([f[1] for f in frames])

    W_opt = np.array([_opt_W(E[i], P, se_lon[i]) for i in range(len(frames))])
    Wu = np.unwrap(np.radians(W_opt))
    A = np.vstack([days, np.ones_like(days)]).T
    rate, W0 = np.linalg.lstsq(A, Wu, rcond=None)[0]
    W0_deg, rate_deg = math.degrees(W0) % 360.0, math.degrees(rate)

    r = np.array([_wrap(body_lonlat(E[i], P, W0_deg + rate_deg * days[i])[0] - se_lon[i])
                  for i in range(len(frames))])
    return W0_deg, rate_deg, float(np.max(np.abs(r))), float(np.sqrt(np.mean(r ** 2)))


def validate(frames, P, W0, rate):
    """Max/rms residual for all four NASA fields (subsolar, subearth lon/lat)."""
    res = {"ss_lon": [], "ss_lat": [], "se_lon": [], "se_lat": []}
    for fr in frames:
        S, E, ss_lat, se_lat, ss_lon, se_lon, days = fr
        W = W0 + rate * days
        ss_lon_c, ss_lat_c = body_lonlat(S, P, W)
        se_lon_c, se_lat_c = body_lonlat(E, P, W)
        res["ss_lon"].append(_wrap(ss_lon_c - ss_lon))
        res["ss_lat"].append(ss_lat_c - ss_lat)
        res["se_lon"].append(_wrap(se_lon_c - se_lon))
        res["se_lat"].append(se_lat_c - se_lat)
    return {k: (float(np.max(np.abs(v))), float(np.sqrt(np.mean(np.array(v) ** 2))))
            for k, v in res.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="/tmp/moon-research/mooninfo_2025.json")
    ap.add_argument("--n", type=int, default=500, help="frames to use for the fit")
    args = ap.parse_args()

    from astropy.time import Time

    rec = json.load(open(args.json))
    step = max(1, len(rec) // args.n)
    idx = list(range(0, len(rec), step))
    print("using %d frames (step %d)" % (len(idx), step))

    raw = []
    for i in idx:
        r = rec[i]
        S, E = _dirs_icrf(_iso(r["time"]))
        days = float(Time(_iso(r["time"]), scale="utc").jd - 2451545.0)
        raw.append((S, E, r["subsolar"]["lat"], r["subearth"]["lat"],
                    r["subsolar"]["lon"], r["subearth"]["lon"], days))

    print("fit pole (exact two-cone) ...")
    pole_ra, pole_dec, lat_max, lat_rms = fit_pole(raw)
    P = pole_cart(pole_ra, pole_dec)
    print("  pole RA=%.5f Dec=%.5f   (lat max %.4f deg, rms %.4f)"
          % (pole_ra, pole_dec, lat_max, lat_rms))

    print("fit prime meridian (linear) ...")
    W0, rate, pm_max, pm_rms = fit_prime_meridian(raw, P)
    print("  W0=%.4f rate=%.6f deg/day   (lon max resid %.4f deg, rms %.4f)"
          % (W0, rate, pm_max, pm_rms))

    print("validate (max, rms) per field:")
    v = validate(raw, P, W0, rate)
    for k in ("ss_lon", "ss_lat", "se_lon", "se_lat"):
        m, r = v[k]
        print("  %-6s max %8.4f  rms %8.4f" % (k, m, r))

    print("\n# paste into moon/constants.py:")
    print("POLE_RA_ICRF = %.6f" % pole_ra)
    print("POLE_DEC_ICRF = %.6f" % pole_dec)
    print("PRIME_MER_W0 = %.6f" % W0)
    print("PRIME_MER_RATE_DEG_DAY = %.8f" % rate)


if __name__ == "__main__":
    main()
