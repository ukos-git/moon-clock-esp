"""Per-frame displaced-sphere shading (the C kernel's exact math).

Given a :class:`moon.astro.Geometry` (body-frame sub-solar / sub-earth /
camera vectors) and the two equirect maps (albedo, height), render the moon
disk as a 400x400 image. Orthographic "view from Earth": the camera looks
down ``-E_body``; the disk is the full visible hemisphere.

Surface model (matches how NASA builds these frames):

* Base unit direction in the body frame ``r_hat = u*right + v*up + w*E``.
* Radial displacement ``s = h / R`` (h in meters, R in meters) from LOLA.
* Surface normal ``N = normalize((1+s) r_hat - e_east*ds_east - e_north*ds_north)``
  where ``ds_east / ds_north`` are the radial-displacement gradients per unit
  arc (from the height gradient). This is the mountain-relief term.
* Lambert lighting ``L = max(0, N . S_body)`` (S_body = unit sub-solar dir).
* Pixel = albedo * L, on the disk; 0 outside. The dark side is NOT flat
  black: the caller adds ``earthshine(d)`` (a fitted phase-independent
  anti-solar gradient, the reference's Earthlight) before the tone LUT,
  exactly as the C kernel does (see ``earthshine`` / ``tonemap`` below).

Two lighting modes:
* ``band_only=False`` -- full N.S everywhere (the ground-truth model).
* ``band_only=True``  -- flat L=1 on the lit side, 0 on the dark side, and the
  relief N.S only in the thin terminator band ``|r_hat.S| <= t``. This is the
  compute-light device strategy; the gate measures how much it costs.

A global ``gain`` calibrates the brightness scale to the reference (the
reference is "optimized for aesthetics"; the raw albedo*L is physical).
"""
from __future__ import annotations

import numpy as np

from . import constants as C
from .sphere import disk_grid, equirect_bilinear
from .astro import Geometry

# Reference disk geometry (measured from the NASA 400x400 crops).
REF_SIZE = 400
REF_RADIUS = 199.3
REF_CX = 199.3
REF_CY = 199.5


def _tangents(lon: np.ndarray, lat: np.ndarray):
    """Unit east / north tangent vectors on the unit sphere (body frame)."""
    lo = np.radians(lon)
    la = np.radians(lat)
    cl, sl = np.cos(la), np.sin(la)
    co, so = np.cos(lo), np.sin(lo)
    # east = d r_hat / d lon ; north = d r_hat / d lat
    east = np.stack([-cl * so, cl * co, np.zeros_like(lo)], axis=-1)
    north = np.stack([-sl * co, -sl * so, np.ones_like(lo) * cl], axis=-1)
    return east, north


def render(g: Geometry, albedo: np.ndarray, height: np.ndarray,
           size: int = REF_SIZE, radius: float = REF_RADIUS,
           cx: float = REF_CX, cy: float = REF_CY,
           band_only: bool = False, band_rad: float = C.TERMINATOR_BAND_RAD,
           gain: float = 1.0, relief_scale: float = C.RELIEF_SCALE
           ) -> tuple[np.ndarray, np.ndarray]:
    """Render the moon disk. Returns ``(raw, dterm)``:

    ``raw``   -- (size, size) float64 in [0, 255], the tone-LUT input
                 (``albedo * L * gain`` on the disk; 0 outside).
    ``dterm`` -- (size, size) float64 base-sphere dot ``d = r_hat . S``
                 (0 at the terminator, +1 at the anti-solar point); the
                 earthshine term (see ``earthshine``) is applied on top by
                 the caller (the same split the C kernel uses).
    """
    u, v, w, mask = disk_grid(size, radius, cx, cy)
    if not mask.any():
        return (np.zeros((size, size), dtype=np.float64),
                np.zeros((size, size), dtype=np.float64))

    # Surface direction in the body frame (unit). right/up/E are orthonormal.
    r_img = np.stack([u, v, w], axis=-1)                      # (H, W, 3)
    r_body = (u[..., None] * g.right_body[None, None, :]
              + v[..., None] * g.up_body[None, None, :]
              + w[..., None] * g.E_body[None, None, :])

    # Body-frame lon/lat (pole is +z in the body frame by construction).
    lon_body = np.degrees(np.arctan2(r_body[..., 1], r_body[..., 0])) % 360.0
    lat_body = np.degrees(np.arcsin(np.clip(r_body[..., 2], -1.0, 1.0)))

    # Texture georeferencing: the map's longitude origin is offset from the
    # body frame by constants.TEXTURE_LON_OFFSET_DEG (see the comment there).
    lon_tex = (lon_body + C.TEXTURE_LON_OFFSET_DEG) % 360.0

    # Texture sample.
    A = equirect_bilinear(albedo, lon_tex, lat_body)
    Hm = equirect_bilinear(height, lon_tex, lat_body)            # meters

    Rm = C.MOON_RADIUS_M
    s = relief_scale * Hm / Rm                                   # radial fraction
    lat_rad = np.radians(lat_body)

    # Height gradient per radian of arc (central difference on the bilinear map).
    d = 0.5  # deg step
    hp = equirect_bilinear(height, lon_tex + d, lat_body)
    hm = equirect_bilinear(height, lon_tex - d, lat_body)
    np_ = equirect_bilinear(height, lon_tex, lat_body + d)
    nm = equirect_bilinear(height, lon_tex, lat_body - d)
    dh_dlon = (hp - hm) / (2.0 * d) * np.pi / 180.0             # m / rad
    dh_dlat = (np_ - nm) / (2.0 * d) * np.pi / 180.0            # m / rad

    east, north = _tangents(lon_body, lat_body)
    # ds/darc per unit arc (dimensionless): divide height gradient (m/rad)
    # by radius (m) -> 1/rad, i.e. per unit arc.
    ds_east = relief_scale * (dh_dlon * np.cos(lat_rad)) / Rm
    ds_north = relief_scale * dh_dlat / Rm

    N = r_body + (-ds_east)[..., None] * east + (-ds_north)[..., None] * north
    N = N / np.linalg.norm(N, axis=-1, keepdims=True)

    # Lambert lighting. The flat-sphere dot d = r_hat . S is the terminator
    # zone test and the earthshine driver; compute it once, always.
    dterm = r_body @ g.S_body
    L_full = np.clip(N @ g.S_body, 0.0, None)
    if band_only:
        L = np.where(dterm > band_rad, 1.0, np.where(dterm < -band_rad, 0.0, L_full))
    else:
        L = L_full

    I = A * L * gain
    out = np.zeros((size, size), dtype=np.float64)
    out[mask] = I[mask]
    d_full = np.zeros((size, size), dtype=np.float64)
    d_full[mask] = dterm[mask]
    return out, d_full


def to_uint8(img: np.ndarray, max_val: float = 255.0) -> np.ndarray:
    return np.clip(np.round(img), 0, max_val).astype(np.uint8)


def earthshine(d: np.ndarray) -> np.ndarray:
    """Earthlight on the dark side, in 8-bit units (a C-portable expression).

    ``d`` is the base-sphere dot r_hat . S (S = Moon->Sun): +1 at the
    sub-solar point (brightest), 0 at the terminator, -1 at the anti-solar
    point (center of the dark side). The reference's earthshine is a
    phase-independent gradient peaking at the anti-solar point and fading to
    0 at the terminator, so the driver is ``x = max(0, -d)``: 0 on the lit
    side (d > 0), growing 0..1 across the dark side. Zero on the lit side,
    so it adds nothing there and blends smoothly at the terminator. A pure
    1-D function of x -- no phase factor (constants.EARTHSHINE_*).
    """
    x = np.maximum(0.0, -d)
    return (C.EARTHSHINE_A * x
            + C.EARTHSHINE_B * x * x
            + C.EARTHSHINE_C * x * x * x)


def tonemap(raw: np.ndarray, dterm: np.ndarray,
            lut: np.ndarray) -> np.ndarray:
    """Reference tone: earthshine, then the frozen 256-entry LUT (the exact
    device order: ``lut[clamp8(raw)] + es``)."""
    es = earthshine(dterm)
    idx = np.clip(np.round(raw), 0, 255).astype(np.intp)
    return np.clip(np.asarray(lut, dtype=np.float64)[idx] + es, 0, 255)


def save_png(img: np.ndarray, path: str, alpha: np.ndarray | None = None) -> None:
    from PIL import Image
    if alpha is None:
        a = Image.fromarray(to_uint8(img), mode="L")
        a = a.convert("RGBA")
        a.putalpha(255)
    else:
        a = Image.fromarray(np.stack([to_uint8(img)] * 3 + [alpha], axis=-1).astype(np.uint8),
                            mode="RGBA")
    a.save(path)


def reference_alpha(size: int = REF_SIZE, radius: float = REF_RADIUS,
                    cx: float = REF_CX, cy: float = REF_CY) -> np.ndarray:
    """Soft-edge alpha mask matching the reference disk (for saving)."""
    u, v, w, mask = disk_grid(size, radius, cx, cy)
    r = np.hypot(u, v)
    # 1px feather at the edge
    a = np.clip((1.0 - r) / 0.0075 + 1.0, 0.0, 1.0)
    a = np.clip((1.0 - (r - (1.0 - 2.0 / radius))) / (2.0 / radius), 0.0, 1.0)
    a[~mask] = 0.0
    return (a * 255).astype(np.uint8)
