The image gate
==============

"Physically correct" is not a property a renderer has; it is a
property a *comparison* has. This page is the comparison: the host
image gate that renders the device's exact C code path on the host,
image-diffs the result against NASA's reference frames *and* against
the validated Python prototype, and turns "the moon looks right" into
a per-frame number with a threshold.

The three ways to compute a moon
--------------------------------

The project has three implementations of the same model, and the gate
exploits the *redundancy*:

1. **The C core** (``components/moon/core``) — the device path: cheap
   ERFA ephemeris, float32, the relief gradient cache, the Q15.15
   rotation, the tone LUT. Rendered on the host by ``host_sim/build/moon_sim``
   (the *exact* runtime path, compiled for x86 — no device-specific
   code is in the core).
2. **The Python prototype** (``moon.astro`` + ``moon.renderer``) —
   the *validated* model: PyERFA ephemeris (the ``builtin`` backend's
   own low-order models), float64, vectorized, full-resolution relief
   gradient per pixel (no cache).
3. **NASA's reference** — the SVS 5415 ground-truth frames (:doc:`the reference data <../science/reference-images>`):
   8,760 rendered frames across a full synodic cycle, 400×400
   crops (``final_400x400``), rendered by the CGI Moon Kit.

Each pair fails in a different way, and the gate checks *both* pairs:

.. list-table::
   :header-rows: 1
   :widths: 24 38 38

   * - Comparison
     - What a failure means
     - Threshold
   * - C vs Python
     - the *port* is wrong — a transcription bug between the float64
       reference and the float32 device path (a sign flip, a quadrant
       error, a texture offset). The physics is the same, so any
       error is a bug in one of the two implementations.
     - per-frame MAE ≤ **2**
   * - C vs NASA
     - the *model* is wrong (or the reference is) — the cheap
       ephemeris's subsolar-point error, the relief model, the tone
       LUT fit. This pair carries the model's own residual.
     - per-frame MAE ≤ **12**

The thresholds are asymmetric by construction: the C-vs-Python
comparison is the *porting-fidelity* gate (plan: "within ~2 MAE of
the Python full model"), and the C-vs-NASA comparison is the
*model-accuracy* gate (the prototype's original threshold,
``MAE_MAX = 12``). A frame that fails the first but not the second is
a transcription bug; a frame that fails the second but not the first
is the model's residual (the ephemeris, the reference's own
processing).

The whole gate in one picture — one subject, two references, two
thresholded comparisons, three board textures:

.. graphviz:: ../_graphs/image-gate.dot
   :alt: The gate's dataflow — C core, Python prototype and NASA references converge on two MAE comparisons feeding check_gate.py per target
   :align: center

The 32-frame set
----------------

The gate runs on a **32-frame set** — 8 frames per quarter of the
phase cycle, evenly spaced across the year, so both terminator
positions (waxing/waning) and the full illumination range are
covered. The frames are rendered at the **reference geometry**
(400×400, radius 199.3 px, center 199.3/199.5 — the gate's
pixel-aligned convention, not the device panel's 466/400), and the
C-vs-NASA comparison is *pixel-aligned*: the C frame and the NASA
crop are the same disk, same size, same origin, so the MAE is a
plain per-pixel mean, no registration step.

The gate is run with **``--neutral``** (the neutral gray→RGB565 LUT)
and **``dark_zero = 0``** (the earthshine path) — the *physics*
configuration. The display's artistic constants (the backlit dark
side, the soft terminator, the warm tint — :doc:`the dark side <../science/darkside>`, :doc:`the dark side <../science/darkside>` and :doc:`the artistic pass <../science/art-pass>`) are all
outside this path, which is what makes the art pass
**byte-identical** to the pre-pass run (the A/B proof in :doc:`the artistic pass <../science/art-pass>`).

The run
-------

.. code-block:: bash
   :caption: Running the gate (one command per board — it renders the C frames itself)

   host_sim/.venv/bin/python host_sim/check_gate.py --target s3
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4
   host_sim/.venv/bin/python host_sim/check_gate.py --target knob

``check_gate.py`` renders the C frames (``moon_sim``), loads the NASA
crops and the Python prototype's frames, computes the per-frame MAE
and a content-windowed SSIM, and prints the table. There is no
separate "render the frames first" step: the gate writes its frame
list under ``--out`` (default ``/tmp/moon-sim-gate``) and invokes the
simulator itself (``--list frames.txt --size 400 --neutral --out
<out>``). The ``--target`` picks both the texture sizes (from
``constants.TARGETS``) and the simulator build directory
(``build/``, ``build-p4/``, ``build-knob/`` — each configured with
``-DMOON_ASSETS_TARGET=<t>``; see :doc:`getting-started <../getting-started>`),
and a startup-line **stale-binary guard** makes the gate exit ``2``
when the binary was built against the other target's texture. Other
flags: ``--sim`` (explicit binary), ``--frames`` (default 32),
``--json`` (the NASA ``mooninfo_2025.json`` path). Exit codes: ``0``
pass, ``1`` gate fail, ``2`` simulator failure / stale binary. The
current output (re-run 2026-11-02, the art pass in-tree):

.. code-block:: text
   :caption: Host image gate, 32-frame set (2026-11-02)

   == C-simulator gate (32-frame set) ==
     frame  phase    C-NASA   SSIM   C-Python
      715    2.00    4.95  0.950    0.60
     7136   12.50    5.29  0.945    0.75
     1993   25.00    5.22  0.934    0.94
     4357   37.50    6.40  0.921    1.02
     2259   50.02    7.42  0.897    1.25
     4420   62.50    6.73  0.911    1.16
      905   75.02    7.72  0.870    1.31
     1817   89.99    8.64  0.873    1.24
        1    1.47    4.90  0.951    0.62
      365   94.52    8.90  0.869    1.27
      729    4.27    5.00  0.950    0.62
     1093   91.94    8.75  0.869    1.24
     1457    9.75    5.18  0.944    0.69
     1821   89.02    8.51  0.878    1.24
     2185   17.83    5.62  0.933    0.87
     2549   85.12    8.61  0.872    1.28
     2913   26.93    6.12  0.920    1.00
     3277   79.44    7.94  0.886    1.25
     3641   35.30    6.43  0.915    1.05
     4005   71.13    8.66  0.838    1.17
     4369   42.30    6.44  0.927    1.05
     4733   59.87    7.68  0.877    1.11
     5097   48.28    6.73  0.924    1.10
     5461   46.86    6.57  0.904    1.05
     5825   53.95    6.98  0.904    1.13
     6189   34.73    5.61  0.920    0.95
     6553   60.01    6.76  0.900    1.15
     6917   25.53    5.38  0.929    0.84
     7281   67.00    6.85  0.895    1.17
     7645   19.46    5.24  0.931    0.78
     8009   75.08    7.27  0.876    1.21
     8373   15.51    5.13  0.934    0.74
     C vs NASA : mean MAE 6.68 (max 8.90, 0 frames > 12)  mean content SSIM 0.908
     C vs Python: mean MAE 1.03 (max 1.31, 0 frames > 2)
     gate: C-NASA per-frame MAE <= 12  AND  C-Python per-frame MAE <= 2
     ->  PASS

The same gate, seen as pictures: four rows of the set as
NASA / device / residual. The third panel of each row is
``|C − NASA| × 8`` after the same sub-pixel registration the MAE uses
— so the bright seam along the terminator is precisely the
subsolar-offset error the table's "error grows with illumination" note
explains, amplified eightfold. ``docs/nasa_figs.py`` rebuilds these
from a fresh gate run (the two rows at 75 % and 90 % sit in the
table's upper MAE band; the 25 % and 50 % rows show the residual
shrinking toward the crescent):

.. figure:: ../img/nasa/nasa_vs_c_p025.png
   :alt: NASA vs C runtime vs residual at phase 25 (first quarter)
   :figclass: align-center

   Phase 25.0 (NASA frame 1993) — C−NASA MAE 5.22 in the table above.

.. figure:: ../img/nasa/nasa_vs_c_p050.png
   :alt: NASA vs C runtime vs residual at phase 50 (half moon)
   :figclass: align-center

   Phase 50.0 (NASA frame 2259) — C−NASA MAE 7.42.

.. figure:: ../img/nasa/nasa_vs_c_p075.png
   :alt: NASA vs C runtime vs residual at phase 75 (gibbous)
   :figclass: align-center

   Phase 75.0 (NASA frame 905) — C−NASA MAE 7.72.

.. figure:: ../img/nasa/nasa_vs_c_p090.png
   :alt: NASA vs C runtime vs residual at phase 90 (gibbous toward full)
   :figclass: align-center

   Phase 90.0 (NASA frame 1817) — C−NASA MAE 8.64; the residual
   covers the whole lit disk here, exactly where the model error is
   largest.

Reading the table
-----------------

Two structure-of-error facts are visible in the numbers:

- **The C-NASA error grows with illumination** (4.9–5.3 at the
  crescent, 8.6–8.9 near full). That is the *model* residual: the
  cheap ephemeris's subsolar-point error (~0.43° max over the set)
  moves the terminator by a few pixels, and at full moon the whole
  disk is lit, so a subsolar offset shifts *every* lit pixel's
  Lambert. At a crescent, most of the disk is dark (both images), so
  the error is confined to the thin lit sliver.
- **The C-Python error is small and phase-independent** (0.6–1.31,
  mean 1.03). The Python prototype uses the *same* low-order ephemeris
  (PyERFA's ``builtin`` backend is the same models the C core
  vendors), so the two implementations share the subsolar offset —
  what the C-Python comparison measures is the *port* alone, and the
  1-gray mean is the float64→float32 + relief-cache-quantization
  floor (the 0.003-rad slope quantization, the 257-entry atan table,
  the 8-bit albedo). That floor is the *best case* the device path
  can achieve against its own reference; the C-NASA column is the
  model's truth.

The SSIM (content-windowed, reported for context) tracks the same
structure: 0.95 at the crescent, 0.84–0.87 near full. It is not
gated — the MAE is — but it is the number to watch when a change
moves the *structure* of the image (a terminator position shift
drops the SSIM far more than the MAE, because the SSIM's window
correlation is where the edges are).

What the gate catches (and what it cannot)
------------------------------------------

The gate is the project's regression net. It has caught, in order:

- **The 180° texture offset** (:doc:`the geometry <../science/geometry>`) — a mirrored terminator is
  a large per-pixel error; the gate failed on every frame until the
  offset was added.
- **The terminator band regression** — the old band-only relief blend
  (:doc:`the lighting model <../science/lighting>`) left the terminator 3–7× too soft; the gate's MAE rose
  at the gibbous frames (where the band is widest) and fell at the
  crescent, which is exactly the signature of a terminator contrast
  error and not of an ephemeris error.
- **The LUT's non-monotonic tail** — a monotonicity "fix" to the tone
  LUT's highlight rolloff moved the brightest pixels off the
  reference's compression and the C-NASA MAE rose at the full-moon
  frames; the tail was reverted.

What it *cannot* catch: an error that is *symmetric* across the
reference (a uniform brightness offset that happens to land inside
the 12-MAE band at the test frames), a change that is *correct at
the 32 test frames* but wrong between them (the 32 frames are a
sample, not a proof — the 8,760-frame set is the proof, and the
prototype's original calibration ran the full set), and anything in
the *display* path the gate is deliberately configured out of (the
art pass, :doc:`the artistic pass <../science/art-pass>`). The gate is a gate, not a certificate: it
certifies that *this* change did not move the *physics* path's
numbers off the reference, on *this* set, within *these* thresholds.

The gate's role in the project's rules
--------------------------------------

The project's standing rule (CLAUDE.md): **if a change touches the
moon bake path, the host image gate must pass before the doc's
numbers are trustworthy.** The gate is the precondition for the
performance page's (:doc:`performance <performance>`) numbers — a bake that runs faster
but renders the wrong moon is not faster, it is wrong. The gate is
also the precondition for the *art* pass's claim (:doc:`the artistic pass <../science/art-pass>`): the
pass's "byte-identical" proof *is* the gate's PASS, re-run with
``--neutral`` after the constants landed.

The gate's cost is one host render of 32 frames (~30 s of C time) and
the Python prototype's 32 frames (~2 s of numpy) — cheap enough to
run after *every* bake-path change, which is the point. A gate you
only run quarterly is a gate you only believe quarterly.

The next page is the other half of validation: not "does the math
render correctly" but "does the *device* run correctly" — the
on-device verification (:doc:`on-device verification <device-verify>`),
whose acceptance grammar — the capture flow, the three line classes,
the acceptance triple, and the incidents that were invisible to the
log — is drawn there (``docs/_graphs/device-verify.dot``).
