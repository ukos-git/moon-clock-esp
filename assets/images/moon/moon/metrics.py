"""Image metrics: MAE / MSE / SSIM (8-bit luminance) vs the reference.

MAE / MSE are over the whole disk (the reference's alpha mask). SSIM is over
the **content region** (disk & ref > 8): the whole-disk / whole-image SSIM is
dominated by the black background and the near-black, low-contrast earthshine
dark side, which SSIM penalizes even when it matches (a faint gradient scores
~0.88). Restricting SSIM to where there is actual structural signal (the lit
disk + visible earthshine) makes it a meaningful structural gate. All metrics
are 8-bit so they reflect the on-device 8-bit -> RGB565 look, not float
headroom.
"""
from __future__ import annotations

import numpy as np
from skimage.metrics import structural_similarity

CONTENT_REF_FLOOR = 8.0  # 8-bit ref floor for the SSIM content region


def _mae(a, b):
    return float(np.abs(a - b).mean())


def _mse(a, b):
    return float(np.mean((a - b) ** 2))


def to_gray(x) -> np.ndarray:
    """PIL image or (H,W[,3]) array -> float64 luminance."""
    x = np.asarray(x.convert("L") if hasattr(x, "convert") else x)
    if x.ndim == 3:
        r, g, b = (x[..., i].astype(float) for i in range(3))
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    return x.astype(np.float64)


def disk_metrics(src: np.ndarray, reference_rgba,
                 shifted: np.ndarray | None = None) -> dict:
    """MAE / MSE / SSIM of ``src`` (or ``shifted``) vs ``reference_rgba``.

    ``reference_rgba``: the NASA 400x400 RGBA crop (PIL or array). The disk
    mask comes from its alpha; both sides are compared as 8-bit luminance.
    """
    ref_rgba = reference_rgba if not hasattr(reference_rgba, "convert") \
        else np.asarray(reference_rgba)
    disk = ref_rgba[..., 3] > 128
    ref = to_gray(reference_rgba)
    s = shifted if shifted is not None else src
    s = np.clip(np.round(np.asarray(s, dtype=np.float64)), 0, 255)

    mae = _mae(ref[disk], s[disk])
    mse = _mse(ref[disk], s[disk])
    # SSIM over the content region (actual moon signal); outside it both sides
    # are set to the neutral mid-gray so the window statistics are stable.
    content = disk & (ref > CONTENT_REF_FLOOR)
    ssim = float(structural_similarity(
        np.where(content, ref, 128.0), np.where(content, s, 128.0),
        data_range=255.0, win_size=15, channel_axis=None))
    return {"mae": mae, "mse": mse, "ssim": ssim, "n_px": int(disk.sum())}
