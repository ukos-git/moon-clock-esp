"""Registration of the rendered disk to the NASA reference crop.

Reuses the phase_cross_correlation approach from the existing alpha_mask.py.
The render and the reference share the same 400x400 frame and near-identical
disk geometry (measured center ~199.3,199.5, r ~199.3), but a small sub-pixel
shift / scale can remain. We estimate the integer+subpixel shift that aligns
the rendered disk to the reference so the metrics measure geometry+texture,
not registration error.
"""
from __future__ import annotations

import numpy as np
from skimage.registration import phase_cross_correlation


def register_shift(render: np.ndarray, reference: np.ndarray,
                   upsample: float = 10.0) -> tuple[np.ndarray, tuple[float, float]]:
    """Return (shifted_render, (dy, dx)).

    shift is the (row, col) offset that moves ``render`` onto ``reference``.
    ``shifted_render`` is render shifted by that amount.
    """
    r = render.astype(np.float64)
    f = reference.astype(np.float64)
    # Use the disk-luminance signal (both are ~0 outside the disk).
    shift, _, _ = phase_cross_correlation(f, r, upsample_factor=upsample)
    # skimage convention: shifted = shift(render, shift) moves content by -shift
    from scipy.ndimage import shift as ndshift
    shifted = ndshift(r, shift, order=3, mode="constant", cval=0.0)
    return shifted, shift


def apply_shift(img: np.ndarray, shift: np.ndarray) -> np.ndarray:
    from scipy.ndimage import shift as ndshift
    return ndshift(img.astype(np.float64), shift, order=3, mode="constant",
                   cval=0.0)
