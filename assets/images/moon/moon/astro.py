"""UTC -> lunar geometry in the calibrated IAU_Moon body frame.

astropy ships no ``IAU_Moon`` frame, so the body-frame rotation (pole +
prime meridian) is the set of calibrated constants in ``constants.py``
(fitted by ``python -m moon.calibrate`` against NASA SVS 5415 ground truth).

For a UTC timestamp this returns:

* ``S_body`` / ``E_body``  -- Moon->Sun and Moon->geocenter as unit vectors
  in the body frame. ``E_body`` is the near pole of the "view from Earth"
  disk (the sub-Earth point); the camera looks down ``-E_body``.
* ``camera``              -- ``(right, up)`` orthonormal body-frame vectors
  for the image plane (celestial-north-up, east-left convention). Screen
  +z (toward the camera) is ``E_body``.
* ``subsolar_lon/lat``, ``subearth_lon/lat``  -- body-frame angles (deg),
  the same convention NASA publishes; used by the geometry gate.
* ``posangle``            -- sky position angle of the lunar north pole (deg,
  east of celestial north), computed independently and gated vs the JSON.
* ``phase_angle_deg`` / ``illum`` -- Sun-Moon-geocenter angle and
  ``(1+cos i)/2``.
* ``distance_km``.

Ephemeris: ``constants.EPHEMERIS`` (built-in PyERFA, validated against the
JSON on phase/distance/declination).
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import astropy.units as u
import numpy as np
from astropy.coordinates import get_body_barycentric
from astropy.time import Time

from . import constants as C

_AU_KM = 149_597_870.7


def _unit(v) -> np.ndarray:
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def _rot_axis(v: np.ndarray, axis: np.ndarray, theta: float) -> np.ndarray:
    """Rotate unit vector ``v`` about unit ``axis`` by ``theta`` (rad)."""
    c, s = math.cos(theta), math.sin(theta)
    return v * c + np.cross(axis, v) * s + axis * float(axis @ v) * (1.0 - c)


def pole_vector() -> np.ndarray:
    """ICRF unit pole vector of the IAU_Moon body frame (calibrated constant)."""
    ra, dec = math.radians(C.POLE_RA_ICRF), math.radians(C.POLE_DEC_ICRF)
    return np.array([math.cos(dec) * math.cos(ra),
                     math.cos(dec) * math.sin(ra),
                     math.sin(dec)])


def prime_meridian(jd: float) -> float:
    """Prime meridian W(deg) at Julian Date ``jd``."""
    return C.PRIME_MER_W0 + C.PRIME_MER_RATE_DEG_DAY * (jd - C.J2000_JD)


def body_basis(P: np.ndarray, W_deg: float):
    """Orthonormal body-frame basis (x, y, z=P) expressed in ICRF.

    The prime meridian parameterizes a rigid rotation of the body frame about
    the pole: fix the 0-deg meridian direction (in the ICRF x-z plane, perp to
    the pole) at W=0, then rotate the whole basis about P by W. This makes
    body longitude advance rigidly with W.
    """
    x0 = np.array([P[2], 0.0, -P[0]])          # in ICRF x-z plane, perp to P
    if np.linalg.norm(x0) < 1e-9:
        x0 = np.array([0.0, P[2], -P[1]])
    x0 = _unit(x0)
    y0 = _unit(np.cross(P, x0))
    th = math.radians(W_deg)
    return _rot_axis(x0, P, th), _rot_axis(y0, P, th), P


def body_lonlat(v: np.ndarray, P: np.ndarray, W_deg: float) -> tuple[float, float]:
    """Body-frame (lon_deg, lat_deg) of unit ICRF vector ``v``."""
    x, y, z = body_basis(P, W_deg)
    lon = math.degrees(math.atan2(float(v @ y), float(v @ x)))
    lat = math.degrees(math.asin(float(np.clip(v @ z, -1.0, 1.0))))
    return lon, lat


def _to_body(v_icrf: np.ndarray, basis) -> np.ndarray:
    """ICRF unit vector -> body-frame coords (dot with the basis columns)."""
    x, y, z = basis
    return np.array([float(v_icrf @ x), float(v_icrf @ y), float(v_icrf @ z)])


@dataclass(frozen=True)
class Geometry:
    utc: str
    subsolar_lon: float
    subsolar_lat: float
    subearth_lon: float
    subearth_lat: float
    posangle: float            # deg, east of celestial north (gate only)
    phase_angle_deg: float
    illum: float               # (1 + cos i) / 2 in [0, 1]
    distance_km: float
    S_body: np.ndarray         # Moon->Sun, body frame, unit
    E_body: np.ndarray         # Moon->geocenter, body frame, unit (near pole)
    right_body: np.ndarray     # image plane +x, body frame
    up_body: np.ndarray        # image plane +y, body frame

    def to_dict(self) -> dict:
        return {
            "utc": self.utc,
            "subsolar_lon": self.subsolar_lon,
            "subsolar_lat": self.subsolar_lat,
            "subearth_lon": self.subearth_lon,
            "subearth_lat": self.subearth_lat,
            "posangle": self.posangle,
            "phase_angle_deg": self.phase_angle_deg,
            "illum": self.illum,
            "distance_km": self.distance_km,
        }


def geometry(utc: str, ephemeris: str | None = None) -> Geometry:
    """Full lunar geometry for a UTC timestamp like '2025-01-01T00:00'."""
    eph = ephemeris or C.EPHEMERIS
    t = Time(utc, scale="utc")
    jd = float(t.jd)

    sun = get_body_barycentric("Sun", t, ephemeris=eph).xyz.to_value(u.au)
    moon = get_body_barycentric("Moon", t, ephemeris=eph).xyz.to_value(u.au)
    earth = get_body_barycentric("Earth", t, ephemeris=eph).xyz.to_value(u.au)

    S = _unit(sun - moon)        # Moon -> Sun (ICRF)
    E = _unit(earth - moon)      # Moon -> geocenter (ICRF)
    L = _unit(moon - earth)      # geocenter -> Moon (line of sight, = -E)

    P = pole_vector()
    W = prime_meridian(jd)
    basis = body_basis(P, W)

    S_body = _to_body(S, basis)
    E_body = _to_body(E, basis)

    ss_lon, ss_lat = body_lonlat(S, P, W)
    se_lon, se_lat = body_lonlat(E, P, W)

    # Phase angle (Sun-Moon-geocenter) and illumination.
    cos_i = float(np.clip(S @ E, -1.0, 1.0))
    phase_angle_deg = math.degrees(math.acos(cos_i))
    illum = (1.0 + cos_i) / 2.0

    # --- camera basis (celestial-north-up, east-left), ICRF then body frame ---
    N_sky = np.array([0.0, 0.0, 1.0])
    north = _unit(N_sky - float(N_sky @ L) * L)   # celestial north on the sky
    east = _unit(np.cross(north, L))              # east on the sky
    # Screen: up = celestial north. right = west (east is left in the standard
    # view). Sign is a calibration knob (POSANGLE_SIGN) applied below.
    right = -east
    right_body = _to_body(right, basis)
    up_body = _to_body(north, basis)

    # --- position angle (independent cross-check vs the JSON) ---
    # Lunar north pole in ICRF: body (lon=0, lat=90) -> ICRF = basis z column.
    pole_icrf = _unit(basis[2])
    Np = _unit(pole_icrf - float(pole_icrf @ L) * L)
    pa = C.POSANGLE_SIGN * math.degrees(math.atan2(float(Np @ east), float(Np @ north)))
    pa = pa % 360.0

    distance_km = float(np.linalg.norm(moon - earth)) * _AU_KM

    return Geometry(
        utc=utc,
        subsolar_lon=ss_lon,
        subsolar_lat=ss_lat,
        subearth_lon=se_lon,
        subearth_lat=se_lat,
        posangle=pa,
        phase_angle_deg=phase_angle_deg,
        illum=illum,
        distance_km=distance_km,
        S_body=_unit(S_body),
        E_body=_unit(E_body),
        right_body=right_body,
        up_body=up_body,
    )


def main() -> None:
    """CLI: python -m moon.astro <utc> [frame_number]

    With a frame number, also prints deltas vs the local NASA JSON (42-frame
    set) or the full-year /tmp JSON.
    """
    import json
    import os
    import sys

    if len(sys.argv) < 2:
        print("usage: python -m moon.astro <utc> [frame_number]")
        return
    utc = sys.argv[1]
    g = geometry(utc)
    print(f"UTC {utc}")
    print(f"  subsolar   ({g.subsolar_lon:9.3f}, {g.subsolar_lat:9.3f})")
    print(f"  subearth   ({g.subearth_lon:9.3f}, {g.subearth_lat:9.3f})")
    print(f"  posangle   {g.posangle:9.3f}")
    print(f"  phase ang  {g.phase_angle_deg:9.3f}   illum {g.illum:.4f}")
    print(f"  distance   {g.distance_km:9.0f} km")
    if len(sys.argv) > 2:
        n = int(sys.argv[2])
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(here, "moon_phases_2025", f"moon.{n:04d}.json")
        if not os.path.exists(path):
            print(f"  (no local frame {n})")
            return
        with open(path) as f:
            ref = json.load(f)
        d = lambda a, b: min(abs(a - b), 360.0 - abs(a - b))
        print(f"  frame {n} time {ref['time']}")
        print(f"  d subsolar lon {abs(g.subsolar_lon - ref['subsolar_lon']):7.3f} "
              f"(wrap {d(g.subsolar_lon, ref['subsolar_lon']):7.3f})  "
              f"lat {abs(g.subsolar_lat - ref['subsolar_lat']):7.3f}")
        print(f"  d subearth lon {abs(g.subearth_lon - ref['subearth_lon']):7.3f} "
              f"(wrap {d(g.subearth_lon, ref['subearth_lon']):7.3f})  "
              f"lat {abs(g.subearth_lat - ref['subearth_lat']):7.3f}")
        print(f"  d posangle     {d(g.posangle, ref['posangle']):7.3f}")
        print(f"  illum {g.illum:.4f} vs phase {ref['phase']/100.0:.4f}")


if __name__ == "__main__":
    main()
