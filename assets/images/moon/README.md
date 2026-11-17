# Moon assets — pipeline + acquisition

Everything the `moon_assets` partition and the image gate need, rebuilt
from NASA's published data. **Nothing binary in this directory is
committed** — one script fetches and generates it all:

```bash
python3 assets/images/moon/get_assets.py            # bake all three boards
python3 assets/images/moon/get_assets.py --refs     # + image-gate references
```

It is idempotent and resumable: re-running costs one hash pass per file and
a bake (minutes). It bootstraps its own `.venv` (uv when available), needs
only a stdlib Python 3 on the host, plus `openssl` (NASA TLS fallback) and
ImageMagick (`magick`/`convert`, for `--refs` crops).

## What lands where

| Path | Comes from | Tracked? |
|---|---|---|
| `moon/` (package), `get_assets.py`, `requirements.txt`, research scripts | this repo | **yes** |
| `moon-kit/lroc_color_16bit_srgb_4k.tif` | NASA SVS 4720, sha256-pinned | no |
| `moon-kit/ldem_16.tif` | NASA SVS 4720, sha256-pinned | no |
| `tone_lut.bin` | raw dump of `moon/constants.py::TONE_LUT` | no |
| `out/<target>/moon_assets.bin` + manifest | `python -m moon.assets` (bake) | no |
| `components/moon/generated/<target>/moon_consts.h` | same bake (tracked — it is a *source*) | **yes** |
| `mooninfo_2025.json` (+ resumable `.mooninfo/` cache) | NASA dialamoon API, 8760 calls | no |
| `moon_phases_2025/*.tif`, `final_400x400/*.png` | NASA SVS 5415, cropped by ImageMagick | no |
| `.venv/` | `requirements.txt` | no |

The bake reads the albedo from `moon-kit/` and the DEM from `$MOON_HEIGHT`
→ `moon-kit/ldem_16.tif` → legacy `/tmp/moon-research/ldem_16.tif` (in
that order), so a fresh clone needs no `/tmp` scratch dir.

## Provenance and pins

* **CGI Moon Kit** — <https://svs.gsfc.nasa.gov/4720/>, served under
  `…/vis/a000000/a004700/a004720/`:
  * `lroc_color_16bit_srgb_4k.tif` — 4096×2048 16-bit sRGB LROC albedo,
    61,891,324 B, sha256 `9731fa8af425b6c2f88f277ecca82bf8c603f3743894f64ed7b25c5bfefa22ff`
  * `ldem_16.tif` — the LOLA displacement map at **16 pixels per degree**
    (5760×2880 float32 km — the name is px/deg, not a size; the bake
    bilinearly resizes it to each board's DEM row), 66,378,634 B,
    sha256 `1ea42bf44f7e9d694f79c3afa7145f97fbf06cc67372067d9fe73dce43bad796`
* **Moon Phases 2025** — <https://svs.gsfc.nasa.gov/5415/>, frames served
  under `…/vis/a000000/a005400/a005415/frames/1920x1080_16x9_30p/plain/moon.NNNN.tif`
  (1-based over the 8760 hours). `--refs` fetches only the frames the gate
  selector picks plus the five quarter hours the docs strip uses, and crops
  each with
  `convert … -alpha on -background none -trim +repage -filter Lanczos -resize 400x400 -gravity center -extent 400x400 PNG32:…`.
* **dialamoon API** — `https://svs.gsfc.nasa.gov/api/dialamoon/<iso>`;
  one call per hour, folded into the nested record schema the gate expects
  (see `get_assets.py::_fetch_hour`).

TLS note: `svs.gsfc.nasa.gov` intermittently sends the leaf certificate
without its Let's Encrypt intermediate, which plain OpenSSL clients reject.
`get_assets.py` then follows the leaf's AIA *CA Issuers* URI over http
(exactly what a browser does) and retries with the assembled chain. The
checksum pins cover integrity regardless of the transport quirk.

If NASA ever moves a pinned file, `get_assets.py` **refuses** it (sha256
mismatch) rather than baking unknown bytes — re-verify upstream and update
`SOURCES` in the script.

## The bake itself

`python -m moon.assets --target s3 p4 knob` (inside `.venv`) renders the
per-target albedo (u8) + relief DEM (int8, 254 steps) into
`out/<target>/moon_assets.bin`, its manifest, the tracked C header, and the
host-sim copy; sizes come from `moon/constants.py::TARGETS` (s3 864/2048×1024,
p4 2048/2048×1024, knob 864/1024×512). The root and knob `CMakeLists.txt`
fail the configure on any width/manifest/partition mismatch.

After touching the pipeline, prove the renders against NASA's answer key —
all three boards:

```bash
host_sim/.venv/bin/python host_sim/check_gate.py --target s3
host_sim/.venv/bin/python host_sim/check_gate.py --target p4
host_sim/.venv/bin/python host_sim/check_gate.py --target knob
```

The research scripts (`alpha_mask.py`, `term_*.py`) and `moon/calibrate.py`
document how the body frame and terminator tuning were fitted; they are
not part of the acquisition path.
