The image gate
==============

.. brief:
   Reader: a developer changing the moon bake path, or a curious reader who wants the evidence behind "physically correct".
   They already know: C, a little Python, what a mean absolute error is.
   After this page they can: run the gate for all three boards, read its table, and tell a porting bug from a model residual.
   Page type: explanation, with one how-to block.

The image gate renders the device's own C moon code on the host and compares each frame with two references.
The first reference is NASA's ground-truth frames; the second is the validated Python prototype.
On the current 32-frame set the C frames differ from NASA by a mean of 6.68 gray levels (worst frame 8.90), against a per-frame limit of 12.
They differ from the Python prototype by a mean of 1.03 gray levels (worst 1.31), against a limit of 2.
Every frame passes both limits, so the gate prints ``PASS``.

"Physically correct" is a property of a comparison, not of a renderer.
This page describes that comparison: what it renders, what it compares against, how to run it, and what its numbers mean.

Run the gate
------------

Run the gate after any change to the moon bake path, once per board.
Each board is compared against its own texture.

1. Make sure the host simulator for each board is built (see :doc:`getting-started <../getting-started>`).
   Each board has its own build directory: ``build/``, ``build-p4/`` and ``build-knob/``, each configured with ``-DMOON_ASSETS_TARGET=<t>``.
2. Run the gate for each board:

   .. code-block:: bash
      :caption: Running the gate (one command per board; it renders the C frames itself)

      host_sim/.venv/bin/python host_sim/check_gate.py --target s3
      host_sim/.venv/bin/python host_sim/check_gate.py --target p4
      host_sim/.venv/bin/python host_sim/check_gate.py --target knob

3. Read the exit code.
   ``0`` means pass, ``1`` means a frame broke a limit, and ``2`` means the simulator failed or is stale.
   A stale binary is one built against the other board's texture: a guard on the simulator's startup line catches it.
   If you see exit ``2`` after switching boards, rebuild that board's simulator.

You do not need a separate render step.
The gate writes its frame list under ``--out`` (default ``/tmp/moon-sim-gate``) and calls the simulator itself (``--list frames.txt --size 400 --neutral --out <out>``).
``--target`` picks both the texture sizes (from ``constants.TARGETS``) and the simulator build directory.
The other flags are ``--sim`` (an explicit simulator binary), ``--frames`` (default 32) and ``--json`` (the path to NASA's ``mooninfo_2025.json``).

One host run costs about 30 s of C rendering for 32 frames and about 2 s of numpy for the Python frames.
That is cheap enough to run after every bake-path change, which is the point: a gate you only run quarterly is a gate you only believe quarterly.

Three ways to compute the same moon
-----------------------------------

The project has three implementations of the same model, and the gate uses that redundancy:

1. **The C core** (``components/moon/core``) is the device path.
   It uses the cheap ERFA ephemeris, float32 arithmetic, the relief gradient cache, the Q15.15 rotation and the tone lookup table (LUT).
   ``host_sim/build/moon_sim`` renders it on the host.
   This is the exact runtime path compiled for x86, because the core contains no device-specific code.
2. **The Python prototype** (``moon.astro`` + ``moon.renderer``) is the validated model.
   It uses the PyERFA ephemeris (the ``builtin`` backend's own low-order models), float64, vectorized code, and the full-resolution relief gradient per pixel (no cache).
3. **NASA's reference** is the SVS 5415 ground-truth set (:doc:`the reference data <../science/reference-images>`).
   It has 8,760 frames across a full synodic cycle, rendered by the CGI Moon Kit; the gate uses the 400×400 crops (``final_400x400``).

Each pair of implementations fails in a different way, and the gate checks both pairs:

.. list-table::
   :header-rows: 1
   :widths: 24 38 38

   * - Comparison
     - What a failure means
     - Threshold
   * - C vs Python
     - The port is wrong: a transcription bug between the float64 reference and the float32 device path (a sign flip, a quadrant error, a texture offset).
       The physics is the same in both, so any error is a bug in one of the two implementations.
     - per-frame MAE ≤ **2**
   * - C vs NASA
     - The model is wrong, or the reference is.
       Candidates are the cheap ephemeris's subsolar-point error, the relief model and the tone LUT fit.
       This pair carries the model's own residual.
     - per-frame MAE ≤ **12**

MAE is the mean absolute error per pixel, in 8-bit gray levels.
The two thresholds differ on purpose.
C vs Python is the porting-fidelity gate (the plan's target: "within ~2 MAE of the Python full model").
C vs NASA is the model-accuracy gate, and its limit is the prototype's original threshold, ``MAE_MAX = 12``.
A frame that fails only the first is a transcription bug.
A frame that fails only the second shows the model's residual: the ephemeris, or the reference's own processing.

The three figures below draw the gate.
The first is the whole gate as one box: one subject, two references, two thresholded comparisons, three board textures.
The second opens the dataflow, and the third the thresholds and the verdict.

.. graphviz:: ../_graphs/image-gate.dot
   :alt: The host image gate as one box. The C core is the subject, compared with two references by two MAE checks, with one verdict per board.
   :caption: One subject (the x86 build of the device's own C core), two references, two thresholded MAE comparisons, three board textures. The gate runs on the 32-frame set at the reference geometry.
   :align: center

.. graphviz:: ../_graphs/image-gate-references.dot
   :alt: Dataflow from the C core, the Python model and the NASA frames into the two MAE comparisons.
   :caption: The subject is the device path (float32, the cheap ERFA ephemeris, the relief-gradient cache). It is compared with the float64 Python model (``moon.astro`` + ``moon.renderer``, the same low-order ephemeris, so a disagreement is a porting bug, not physics) and with NASA SVS 5415 CGI Moon Kit ground truth (8,760 frames). The ``final_400x400`` crops are pixel-aligned to the C frames, so the MAE needs no registration step.
   :align: center

.. graphviz:: ../_graphs/image-gate-thresholds.dot
   :alt: The two MAE thresholds, the shared render geometry, the reported SSIM and the per-board check_gate verdict.
   :caption: C vs Python (per-frame MAE ≤ 2, current mean 1.03) is the porting-fidelity gate; C vs NASA (≤ 12, mean 6.68) is the model-accuracy gate. All three implementations render one geometry (400×400, r 199.3, c 199.3/199.5) with ``--neutral`` and ``dark_zero = 0``. SSIM (mean 0.908) is reported, not gated. ``check_gate.py --target s3|p4|knob`` compares each board with its own texture; exit 2 means a stale simulator binary.
   :align: center

The 32-frame set and the render configuration
---------------------------------------------

The gate runs on a 32-frame set: 8 frames per quarter of the phase cycle, spread evenly across the year.
The set covers both terminator positions (waxing and waning) and the full illumination range.

All frames render at the reference geometry: 400×400 pixels, radius 199.3 px, center 199.3/199.5.
This is the gate's pixel-aligned convention, not the device panel's 466/400.
The C frame and the NASA crop show the same disk at the same size and origin, so the MAE is a plain per-pixel mean with no registration step.

The gate runs the physics configuration: ``--neutral`` (the neutral gray-to-RGB565 LUT) and ``dark_zero = 0`` (the earthshine path).
The display's artistic constants sit outside this path: the backlit dark side, the soft terminator and the warm tint (:doc:`the dark side <../science/darkside>` and :doc:`the artistic pass <../science/art-pass>`).
That is why the art pass leaves the gate's output byte-identical to the run without it, which is the A/B proof in :doc:`the artistic pass <../science/art-pass>`.

What the gate runs
------------------

``check_gate.py`` renders the C frames with ``moon_sim``, loads the NASA crops and the Python prototype's frames, and computes two numbers per frame.
The first is the MAE; the second is a content-windowed SSIM (structural similarity).
It then prints the table below.

The Python leg makes the gate the prototype's heaviest user in the repo.
:cfile:`host_sim/check_gate.py` imports six of the package's twelve modules: it is the blue hub of :ref:`the package in one picture <python-surface>`.

This is the current output, from a run on 2026-11-02 with the art pass in the tree:

.. code-block:: text
   :caption: Host image gate, 32-frame set (2026-11-02). The check_gate imports are at host_sim/check_gate.py:53-59.

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

The same gate as pictures
-------------------------

The four figures below show four rows of the set as NASA, device and residual.
The residual panel is ``|C − NASA| × 8`` after the same sub-pixel registration the MAE uses.
The bright seam along the terminator is the subsolar-offset error that `Reading the table`_ explains, amplified eight times.
``docs/nasa_figs.py`` rebuilds these figures from a fresh gate run.
The rows at 75 % and 90 % sit in the table's upper MAE band; the 25 % and 50 % rows show the residual shrinking toward the crescent.

.. figure:: ../img/nasa/nasa_vs_c_p025.png
   :alt: NASA frame, C frame and residual side by side at phase 25, first quarter.
   :figclass: align-center

   Phase 25.0 (NASA frame 1993): C−NASA MAE 5.22 in the table above.

.. figure:: ../img/nasa/nasa_vs_c_p050.png
   :alt: NASA frame, C frame and residual side by side at phase 50, half moon.
   :figclass: align-center

   Phase 50.0 (NASA frame 2259): C−NASA MAE 7.42.

.. figure:: ../img/nasa/nasa_vs_c_p075.png
   :alt: NASA frame, C frame and residual side by side at phase 75, gibbous.
   :figclass: align-center

   Phase 75.0 (NASA frame 905): C−NASA MAE 7.72.

.. figure:: ../img/nasa/nasa_vs_c_p090.png
   :alt: NASA frame, C frame and residual side by side at phase 90, gibbous toward full.
   :figclass: align-center

   Phase 90.0 (NASA frame 1817): C−NASA MAE 8.64.
   The residual covers the whole lit disk here, where the model error is largest.

Reading the table
-----------------

The numbers show two facts about the structure of the error.

The C-NASA error grows with illumination: 4.9–5.3 at the crescent, 8.6–8.9 near full.
That is the model residual.
The cheap ephemeris's subsolar-point error (about 0.43° at most over the set) moves the terminator by a few pixels.
At full moon the whole disk is lit, so a subsolar offset changes the Lambert shading of every lit pixel.
At a crescent most of the disk is dark in both images, so the error stays in the thin lit sliver.

The C-Python error is small and does not depend on phase: 0.6–1.31, mean 1.03.
The Python prototype uses the same low-order ephemeris, because PyERFA's ``builtin`` backend has the same models the C core vendors.
The two implementations therefore share the subsolar offset, and the C-Python comparison measures the port alone.
The mean of about 1 gray level is the floor set by float64-to-float32 and by the relief cache's quantization: the 0.003-rad slope quantization, the 257-entry atan table and the 8-bit albedo.
That floor is the best the device path can reach against its own reference; the C-NASA column is the model's accuracy.

The SSIM column (content-windowed, reported for context) follows the same structure: 0.95 at the crescent, 0.84–0.87 near full.
The gate does not use it; the MAE decides.
Watch the SSIM when a change moves the structure of the image.
A shift of the terminator position drops the SSIM far more than the MAE, because the SSIM's window correlation concentrates on the edges.

What the gate catches, and what it cannot
-----------------------------------------

The gate is the project's regression net.
These are the bugs it has caught, in order:

- The 180° texture offset (:doc:`the geometry <../science/geometry>`).
  A mirrored terminator gives a large per-pixel error, and the gate failed on every frame until the offset was in place.
- The terminator band regression.
  A band-only relief blend (:doc:`the lighting model <../science/lighting>`) left the terminator 3–7× too soft.
  The MAE rose at the gibbous frames, where the band is widest, and fell at the crescent.
  That pattern points to a terminator contrast error, not an ephemeris error.
- The tone LUT's non-monotonic tail.
  A monotonicity "fix" to the LUT's highlight rolloff moved the brightest pixels off the reference's compression.
  The C-NASA MAE rose at the full-moon frames, and the tail was reverted.

The gate cannot catch three kinds of error:

- An error that is symmetric across the reference, such as a uniform brightness offset that lands inside the 12-MAE band at the test frames.
- A change that is correct at the 32 test frames but wrong between them.
  The 32 frames are a sample, not a proof.
  The 8,760-frame set is the proof, and the prototype's original calibration ran the full set.
- Anything in the display path, which the gate deliberately leaves out, such as :doc:`the artistic pass <../science/art-pass>`.

So the gate is a gate, not a certificate.
It shows that this change did not move the physics path's numbers off the reference, on this set, within these thresholds.

Where the gate sits in the project's rules
------------------------------------------

The project's standing rule (CLAUDE.md) is: if a change touches the moon bake path, the host image gate must pass before the docs' numbers can be trusted.
The gate is the precondition for the numbers on :doc:`performance <performance>`.
A bake that runs faster but renders the wrong moon is not faster; it is wrong.
The gate is also the precondition for the claim of :doc:`the artistic pass <../science/art-pass>`.
That pass's byte-identical proof is the gate's ``PASS``, re-run with ``--neutral`` after the art constants went in.

Where to go next
----------------

- :doc:`On-device verification <device-verify>` is the other half of validation: not whether the math renders correctly, but whether the device runs correctly.
  Its acceptance grammar (the capture flow, the three line classes, the acceptance triple, and the incidents the log could not see) is drawn there (``docs/_graphs/device-verify.dot``).
- :doc:`The reference data <../science/reference-images>` describes the NASA frames the gate compares against.
- :doc:`Performance <performance>` has the timing numbers the gate makes trustworthy.
