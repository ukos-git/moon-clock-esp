"""Single source of truth for the moon geometry/rendering constants.

These values are shared between the Python prototype (this package) and the C
ESP component. The C header ``moon_consts.h`` is GENERATED from this file by
``moon/assets.py`` so the two never drift.

TWO GROUPS:

1. **Calibrated body frame** (``POLE_*``, ``PRIME_MER_*``): the IAU_Moon
   rotation fitted empirically against NASA SVS 5415 per-frame subsolar/
   subearth geometry by ``python -m moon.calibrate``. astropy ships no
   ``IAU_Moon`` frame, so the pole and prime meridian are derived directly
   from the ground truth we validate against (self-consistent). The pole is a
   constant (its 18.6-yr nodal precession leaves ~0.2 deg rms latitude
   residual -- 0.6 px at the disk edge, invisible in the image gate); the
   prime meridian is uniform at the sidereal rate.

2. **Tuning knobs** (``POSANGLE_SIGN``, disk geometry): calibrated in
   Phase 1 against the NASA reference frames, then frozen.
"""
from __future__ import annotations

# --- Calibrated body frame (from `python -m moon.calibrate`) ---------------
# ICRF rotation pole of the IAU_Moon frame (deg), constant over the project's
# date range. The 18.6-yr nodal precession is not modeled (see module docstring).
POLE_RA_ICRF = 269.839977
POLE_DEC_ICRF = 68.110423
# Prime meridian W(deg) = PRIME_MER_W0 + PRIME_MER_RATE_DEG_DAY * (JD - J2000).
# J2000 = JD 2451545.0 = 2000-01-01T12:00.
PRIME_MER_W0 = 40.017251
PRIME_MER_RATE_DEG_DAY = 13.17617251
J2000_JD = 2451545.0

# --- Geometry / rendering ---------------------------------------------------
# Disk radius in pixels. Reference frames are 400x400; the on-device panel is
# 466x466 (the 400 disk is centered, ~33 px margin).
DISK_RADIUS_PX = 200.0
IMAGE_SIZE = 400  # reference + asset resolution

# Moon physical radius, meters (CGI Moon Kit uses 1737.4 km).
MOON_RADIUS_M = 1_737_400.0

# LOLA height is stored in meters. Relief used for the terminator normal is
# scaled by RELIEF_SCALE so crater relief reads correctly at disk resolution.
RELIEF_SCALE = 1.0

# Terminator band half-width, radians in the base-sphere dot space (d = r.S).
# RESEARCH-ONLY: the prototype's band_only render variant
# (renderer.render(band_only=True)) blends the relief normal only inside
# |d| <= t. The validated DEVICE model is full-disk displaced-normal
# Lambert (assets.render_from_assets / moon_bake.c) and does NOT use this
# band -- the displaced normal makes the lit/dark call everywhere. The old
# 0.02 band with a clipped relief term left the terminator 3-7x too soft
# (terminator-local |grad| ~4-8 vs NASA's ~27), which the band width
# could not fix; only unclipped full-disk relief + a 2048x1024 DEM did.
TERMINATOR_BAND_RAD = 0.02

# Signed position-angle roll convention (+1 / -1) so the disk matches NASA.
POSANGLE_SIGN = 1.0

# The LROC albedo map (and the LOLA DEM, same CGI Moon Kit convention) is
# georeferenced 180 deg of longitude away from the IAU_Moon body frame:
# texture_lon = (body_lon + TEXTURE_LON_OFFSET_DEG) % 360. Verified by an
# edge-NCC placement scan vs the NASA frames: a sharp peak at exactly 180
# (NCC 0.45 vs <= 0.18 elsewhere), constant across all primary phases, no
# longitude mirror or latitude flip needed.
TEXTURE_LON_OFFSET_DEG = 180.0

# Earthshine model for the dark side. The reference's dark limb is not flat
# black: it is a smooth anti-solar gradient (Earthlight) that is
# PHASE-INDEPENDENT (measured flat across illum 0.04..0.91) -- the stylized
# "dark zone" of the NASA frames, not physically scaled earthlight. Fitted
# against 14 reference frames (775k dark-side px) as
#   R = A*x + B*x^2 + C*x^3,  x = max(0, -d),
# d = r_hat . S (base-sphere dot, S = Moon->Sun): +1 sub-solar, 0 at the
# terminator, -1 at the anti-solar point. So x is 0 on the lit side and grows
# 0..1 across the dark side. R is in 8-bit output units (plateau ~16 at the
# anti-solar point); fit MAE 3.8, residual < 0.2 per x-bin. R(0) = 0 by
# construction, so it blends smoothly with the lit side at the terminator
# (no separate band needed). On device the dark zone is thus a pure 1-D
# function of x -- no phase factor at runtime.
EARTHSHINE_A = 30.455
EARTHSHINE_B = -11.733
EARTHSHINE_C = -2.615

# 256-entry 8-bit tone LUT (one gather/px, identical on device).
# Maps pure-sunlight albedo*L (16-bit-linear albedo, see ``moon.textures``;
# the earthshine term is ADDED back by the caller AFTER this gather) to the
# reference's "optimized for aesthetics" brightness (gamma + highlight
# compression). Bin 0 is 0: the pure dark side gets no sunlight, its
# brightness is earthshine() alone. Fit by `python -m moon.validate --fit`
# (MAE 6.3 over 12 frames), then frozen here.
TONE_LUT = (
    0, 27, 32, 38, 44, 49, 53, 57, 61, 65, 68, 71,
    74, 77, 80, 82, 85, 87, 90, 92, 94, 96, 98, 100,
    102, 104, 106, 108, 110, 111, 112, 114, 116, 117, 119, 120,
    122, 123, 124, 126, 127, 128, 129, 130, 131, 132, 134, 135,
    136, 137, 139, 140, 141, 143, 144, 146, 147, 148, 150, 151,
    153, 154, 155, 156, 158, 159, 160, 162, 163, 164, 165, 167,
    168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 186, 187, 188, 189, 190,
    191, 192, 193, 193, 194, 195, 196, 197, 197, 198, 199, 200,
    201, 202, 202, 203, 203, 204, 205, 206, 207, 207, 208, 209,
    209, 210, 211, 211, 212, 213, 214, 214, 215, 216, 216, 217,
    218, 219, 219, 220, 221, 222, 222, 223, 223, 224, 225, 225,
    226, 226, 226, 227, 227, 228, 228, 229, 230, 231, 231, 231,
    230, 230, 231, 232, 233, 233, 234, 233, 234, 235, 235, 235,
    235, 234, 235, 235, 238, 239, 239, 237, 235, 236, 239, 237,
    237, 237, 239, 235, 231, 228, 233, 236, 241, 243, 240, 242,
    236, 239, 241, 247, 248, 240, 241, 236, 244, 244, 251, 247,
    249, 236, 237, 228, 232, 236, 244, 252, 250, 243, 244, 247,
    255, 253, 251, 250, 252, 252, 250, 240, 238, 236, 243, 243,
    243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243,
    243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243,
    243, 243, 243, 243,
)

# --- Validation gate --------------------------------------------------------
# The latitude floor (~0.55 deg) is the 18.6-yr nodal precession of the pole,
# not a bug; longitude/phase/posangle are tight. Gate each geometry field.
GEO_SUBSOLAR_TOL_DEG = 0.6
GEO_SUBEARTH_TOL_DEG = 0.6
GEO_POSANGLE_TOL_DEG = 2.0

# MAE: per-frame, over the whole disk (radiometric, 0..255).
MAE_MAX = 12.0
# SSIM: frame-set MEAN, over the content region (disk & ref > 8, see
# ``metrics.CONTENT_REF_FLOOR``). Whole-disk SSIM is dominated by the black
# background and the near-black earthshine floor (a faint gradient scores
# ~0.88), so it is not a meaningful structural gate; the content region is.
SSIM_MIN = 0.89

# Number of intermediate frames spread across a synodic month (8 primary phases
# are added on top).
NUM_INTERMEDIATE_FRAMES = 24

# --- Ephemeris --------------------------------------------------------------
# astropy ephemeris: 'builtin' (PyERFA) -- validated against the NASA JSON on
# phase/distance/declination; sufficient for the geometry. (DE432s is a
# fallback if the gate shows > 0.5 deg subsolar error; not needed.)
EPHEMERIS = "builtin"

# --- Per-board asset targets (consumed by ``moon/assets.py``) ---------------
# The equirect albedo size is PER-BOARD: the boards differ in flash
# (moon_assets partition) and PSRAM budget, and each defaults to the best
# resolution that fits it (resolution over speed). The clock's Kconfig
# default (MOON_ALBEDO_W) mirrors the s3/p4 rows; the bake size is a
# compiled-in #define (MOON_PX in components/ui/moon_ui.c -- the old
# UI_MOON_ANIM_BAKE_PX Kconfig key died with the sweep 2026-11-13);
# the knob (sibling project knob/, K7 task #24) has NO Kconfig mirror --
# its single truth is the generated header + manifest, guarded in
# knob/CMakeLists.txt. The albedo is equirect 2:1 (width = 2 * height); the
# relief DEM is 2048x1024 on the clock boards (the resolution at which the
# displaced-normal terminator contrast matches the NASA frames) and halved
# on the knob -- the gate is the arbiter, see the knob row.
#
#   "alb":          equirect albedo size (w, h) baked from the 4096x2048
#                   16-bit LROC source;
#   "partition_kib": the board's moon_assets partition size (KiB) the
#                   combined albedo+DEM blob must fit in;
#   "psram_mib":    the board's PSRAM chip size (MiB) -- used for the
#                   steady-state asset-budget warning (albedo + DEM + the
#                   two dusk shade maps, which are albedo-sized).
TARGETS = {
    "s3": {  # Waveshare ESP32-S3-Touch-AMOLED-1.75 (466 px panel, 16 MB
             # flash, 8 MB PSRAM). 864 is the max the S3 PSRAM pool can
             # spare; 1024 (equirect 1024x512) would fill the 2560K
             # partition exactly but adds ~0.45 MB of asset PSRAM the pool
             # cannot spare -- not recommended.
        "alb": (864, 432),
        "dem": (2048, 1024),
        "partition_kib": 2560,
        "psram_mib": 8,
    },
    "p4": {  # Waveshare ESP32-P4-WIFI6-Touch-LCD-XC "4C" (720 px panel,
             # 32 MB flash, 32 MB PSRAM). 2048 matches the DEM grid (1.57x
             # oversampling at the 650 px disk).
        "alb": (2048, 1024),
        "dem": (2048, 1024),
        "partition_kib": 5120,
        "psram_mib": 32,
    },
    "knob": {  # Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (360 px backlit LCD,
               # 16 MB flash, 8 MB octal PSRAM) -- the standalone moon
               # display of K7 (sibling project knob/). Albedo stays 864:
               # byte-identical to the proven s3 bake, and 512 would
               # visibly undersample the 360 px disk (resolution over
               # speed). The DEM is halved to 1024x512 -- the relief cache
               # costs 4+2 MiB of a PSRAM pool this battery device shares
               # with WiFi + LVGL. If the gate shows the sharp-terminator
               # contrast needs the 2048 grid, raise "dem" to (2048, 1024):
               # the 2560K partition is sized for BOTH bakes so the
               # partition table never has to change (a partition change
               # means a full flash on the flaky bench unit).
        "alb": (864, 432),
        "dem": (1024, 512),
        "partition_kib": 2560,
        "psram_mib": 8,
    },
}
# Tracked location of the generated header per board (the rest of the
# per-target output stays gitignored under assets/images/moon/out/<t>/).
# Repo-root relative.
HEADER_PATHS = {
    "s3": "components/moon/generated/s3/moon_consts.h",
    "p4": "components/moon/generated/p4/moon_consts.h",
    "knob": "components/moon/generated/knob/moon_consts.h",
}
