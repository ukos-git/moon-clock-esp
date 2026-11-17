"""Texture loading: true 16-bit LROC albedo and LOLA height.

The albedo ``lroc_color_16bit_srgb_4k.tif`` is a 16-bit sRGB RGB TIFF.
PIL silently decimates it to 8-bit on load (crushing the range to ~79..255
and discarding the maria/crater contrast), so it must be read with
``tifffile`` (+ ``imagecodecs`` for the LZW tiles) and linearized here.

The LOLA DEM (``ldem_16.tif``) is float32 in **kilometers**; the renderer
expects meters, so the loader converts.
"""
from __future__ import annotations

import os

import numpy as np
import tifffile

from . import constants as C

_HGT_KM_TO_M = 1000.0


def _srgb_linear(x: np.ndarray) -> np.ndarray:
    """16-bit sRGB code (0..1) -> linear light (0..1)."""
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def _here() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def albedo_path() -> str:
    return os.path.join(_here(), "moon-kit", "lroc_color_16bit_srgb_4k.tif")


def height_path() -> str:
    """LOLA DEM resolution order: $MOON_HEIGHT, the moon-kit dir beside this
    package (what assets/images/moon/get_assets.py fetches), then the legacy
    research scratch dir (kept for old checkouts that built there)."""
    env = os.environ.get("MOON_HEIGHT")
    if env:
        return env
    local = os.path.join(_here(), "moon-kit", "ldem_16.tif")
    if os.path.exists(local):
        return local
    return "/tmp/moon-research/ldem_16.tif"


def load_albedo(path: str | None = None) -> np.ndarray:
    """LROC albedo as equirect (H, W) float in [0, 255] (linear-light * 255).

    Row 0 = north, col 0 = longitude 0 (the map's own origin; see
    ``constants.TEXTURE_LON_OFFSET_DEG`` for its offset vs the body frame).
    """
    p = path or albedo_path()
    a16 = tifffile.imread(p).astype(np.float64) / 65535.0
    lin = _srgb_linear(a16)
    alb = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    return (np.clip(alb, 0.0, 1.0) * 255.0)


def load_height(path: str | None = None) -> np.ndarray:
    """LOLA height as equirect (H, W) float in **meters**."""
    from PIL import Image
    p = path or height_path()
    hgt = np.asarray(Image.open(p), dtype=np.float64)
    return hgt * _HGT_KM_TO_M


def load_textures(alb_path: str | None = None,
                  hgt_path: str | None = None) -> tuple[np.ndarray, np.ndarray]:
    return load_albedo(alb_path), load_height(hgt_path)
