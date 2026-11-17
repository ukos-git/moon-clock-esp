"""Unit-sphere sampling and equirect texture/height lookup (vectorized).

The Moon is convex: for every pixel inside the disk there is exactly one
ray-sphere hit, so the "camera" basis gives the surface point directly:

    r = u * right + v * up + w * E        (body frame, unit)

with u = x/R, v = y/R (image plane, y up), w = sqrt(1 - u^2 - v^2).
``u, v, w`` is the *base* (flat sphere) direction; the flat-sphere
terminator test is ``d = r . S``.

This module is the exact math the C kernel ports 1:1.
"""
from __future__ import annotations

import math

import numpy as np


def disk_grid(size: int, radius_px: float, cx: float, cy: float):
    """Pixel grid for a disk of ``radius_px`` centered at (cx, cy) in a
    ``size``x``size`` image. Returns (u, v, w, mask):

    u, v in [-1, 1] (image plane, v up: row 0 is top),
    w = sqrt(1 - u^2 - v^2) (0 outside the disk),
    mask = inside the disk.
    """
    x = np.arange(size, dtype=np.float64)
    u = (x - cx) / radius_px
    v = (cy - np.arange(size, dtype=np.float64)) / radius_px  # v up (row 0 top)
    uu, vv = np.meshgrid(u, v)
    r2 = uu * uu + vv * vv
    mask = r2 <= 1.0
    w = np.zeros_like(r2)
    w[mask] = np.sqrt(1.0 - r2[mask])
    return uu, vv, w, mask


def equirect_bilinear(sample_map: np.ndarray, lon_deg: np.ndarray,
                      lat_deg: np.ndarray) -> np.ndarray:
    """Bilinear sample of an equirect map (row 0 = north, col 0 = lon 0).

    ``sample_map`` is (H, W); lon in [0, 360) (wraps), lat in [-90, 90]
    (clamped). Returns the sampled value with the same shape as ``lon_deg``.
    """
    H, W = sample_map.shape
    # lon -> col position [0, W), wrapping at the seam
    col = (np.mod(lon_deg, 360.0) / 360.0) * W
    row = (90.0 - lat_deg) / 180.0 * H
    c0 = np.floor(col).astype(np.int64) % W
    r0 = np.clip(np.floor(row).astype(np.int64), 0, H - 1)
    c1 = (c0 + 1) % W
    r1 = np.clip(r0 + 1, 0, H - 1)
    fc = col - np.floor(col)
    fr = row - np.floor(row)
    v00 = sample_map[r0, c0]
    v01 = sample_map[r0, c1]
    v10 = sample_map[r1, c0]
    v11 = sample_map[r1, c1]
    return (v00 * (1 - fc) + v01 * fc) * (1 - fr) + (v10 * (1 - fc) + v11 * fc) * fr


def tangent_normal(r: np.ndarray, h_slope_u: np.ndarray, h_slope_v: np.ndarray,
                   w: np.ndarray) -> np.ndarray:
    """Exact 3D surface normal for a radially displaced sphere.

    r = (u, v, w) is the base unit direction in some orthonormal basis with
    +z = the basis point of the pixel (flat normal). The displaced point is
    Q = (1 + H) r where H is the height (fraction of radius); its gradient in
    the image plane is (h_slope_u, h_slope_v) in *radius units* (i.e. dH per
    unit u / unit v). Returns the normalized normal in the same basis.

    Small-slope (valid in the terminator band): N ~ normalize(r + (du, dv, 0)).
    """
    u = r[..., 0]
    v = r[..., 1]
    # N = normalize( r + tangent-derivative correction ), exact form:
    # the two tangent directions at r are e_u = (-v, u, 0)/sqrt(u^2+v^2) and
    # e_v = (w*u, w*v, -(u^2+v^2))/sqrt(u^2+v^2) (east/north on the sphere).
    r2 = r[..., 0] ** 2 + r[..., 1] ** 2
    s = np.sqrt(np.maximum(r2, 1e-12))
    eu = np.stack([-v, u, np.zeros_like(u)], axis=-1) / s[..., None]
    ev = np.stack([w * u, w * v, -(u * u + v * v)], axis=-1) / s[..., None]
    grad = h_slope_u[..., None] * eu + h_slope_v[..., None] * ev
    n = r + grad
    return n / np.linalg.norm(n, axis=-1, keepdims=True)
