The universal housing
=====================

A single 3D-printed desk stand for the whole moon-clock family: one common
**chassis** (base, feet, backstop strut, and the 18°-tilted cradle faceplate)
with four **swappable seat cartridges** — one each for the S3 AMOLED 1.75
(466), the P4 4C (720), the knob 1.8 (360, in its stock CNC case), and the
proposed Moon v1 board (a stub seat from nominal values until its KiCad STEP
lands — :doc:`the closed loop <housing>` below). One cartridge at a time;
the seat choice is the board you want on the desk.

**Status (A1 complete, 2026-11-19):** the parametric builder, the single
source of truth (``hardware/housing/params.json``), the in-scene audit,
the STL dimension audit and the render set are all green. The A2 step —
the first 0.2 mm PETG test print, fitting all three boards, and recording
the measured per-seat clearances back into ``params.json`` — is pending and
user-gated; the Moon v1 seat stays a stub until Phase B's STEP regenerates
it.

What it is
----------

The model is one script, ``hardware/housing/build_housing.py``: a
pure-``bpy`` builder that reads **every** dimension from ``params.json``
(units: mm) and boolean-unions two object families.

- **``housing_chassis``** — base disc (Ø134, sized by the P4), four rubber
  feet, a backstop strut, and the faceplate disc (r 66) tilted 18° back
  from vertical. A central collar recess (r 65.5, 5 mm deep) is cut into the
  faceplate back; that is the cartridge socket.
- **``cart_s3`` / ``cart_p4`` / ``cart_knob`` / ``cart_moonv1``** — each
  cartridge is a collar ring (r 65.8, a 0.2 mm press fit into the r 65.5
  recess) + a web annulus + the seat face ring, with the board **pocket**
  (board radius + 0.20 mm side tolerance, depth = board stack + clearance),
  a **USB-C through-channel** at the seat's measured angle, and — except the
  knob — one **anti-flex screw boss** with an M2.5 clearance hole (r 2.6).

All geometry is built in the faceplate's own coordinate frame (the plane the
board sits in), not in raw XY: a raw-XY offset misses the tilted plane and
lands a cut or boss ~1.8 mm off at the P4's 45° mounting holes.

Anti-flex mounting rule
-----------------------

.. note::

   **Highest-priority mechanical rule** (from the knob's brown-out field
   history — the stock CNC case's overtightened screws flexed the PCB and
   pinched a ground; pressing the display against the housing reset it,
   :doc:`the knob <devices/knob>`): exactly **one** M2.5 screw per seat —
   the knob seat is press-fit only, zero screws — printed boss with a 0.10 mm
   clearance thread, **max 0.5 N·m**, landing on a vendor mounting hole on a
   non-display, non-battery board corner. Never screw through or clamp on
   the display.

The in-scene audit enforces the keep-outs: the screw landing must be one of
the seat's (vendor) mounting holes, at least 2 mm inside the pocket edge,
and — on self-designed boards only (Moon v1, where the layout is ours) —
outside the display circle + 1 mm. On the vendor boards the holes are the
manufacturer's back-side mounting points: the display is a separate front
module, so a back-side screw at the 0.5 N·m limit is safe even when the hole
sits inside the display circle (the S3's three holes do).

Geometry (single-sourced in ``params.json``)
--------------------------------------------

The table below is the A1 geometry; every row carries a provenance in
``params.json`` (``vendor`` = Waveshare dimension drawing/STEP,
``product-image`` = official photo, ``docs`` = the fitted circles in
``docs/tools/fit.json`` / ``fit_p4.json``, ``derived`` = computed,
``assumed`` = to be confirmed by the A2 test print, then moved to
``measured``).

.. list-table::
   :header-rows: 1
   :widths: 26 22 12 12 12 16

   * - Seat
     - Board / case OD
     - Pocket radius
     - Pocket depth
     - Screw landing
     - USB-C (angle, radial)
   * - S3 AMOLED 1.75
     - Ø48.96 (vendor)
     - 24.68 = 24.48 + tol
     - 10.60 (stack 10.40)
     - (18.0, 0.0) of 3× Ø2.00
     - 270°, r 14 (assumed)
   * - P4 4C
     - Ø126.00 (vendor)
     - 63.20 = 63.00 + tol
     - 12.20 (board 9.00 + connector relief)
     - (37.50, −37.50) of 4× Ø4.00
     - 180°, r 56 (assumed)
   * - Knob 1.8 (stock case)
     - Ø66.00 × 22.00 (product-image)
     - 33.20 = 33.00 + tol
     - 22.20 (case 22.00)
     - none (press fit)
     - 180°, r 30 (assumed)
   * - Moon v1 (stub)
     - ≤ Ø55.00 (assumed)
     - 27.70 = 27.50 + tol
     - 5.20 (stack 5.00)
     - (25.0, 0.0) — outside display keep-out
     - 270°, r 22 (assumed)

Chassis and print parameters: base OD **134.0** (P4 126 + 4 mm wall + 2 mm
lip), wall 4.0, lip 2.0, tilt **18°** (15–20° design band), height cap
**160 mm**, feet Ø14 × 1.5. Print profile: FDM 0.2 mm PETG, 30 % infill,
3 walls, **``tol_side`` 0.20 mm** (band 0.15–0.25 — A2 records the measured
per-seat clearance and tunes it). The knob seat is a well for the **stock
CNC case** (the user's unit lives in it), so its pocket is Ø66.40 × 22.20
and it carries no screw — the stock case's own screw pattern is the
documented brown-out hazard.

The S3 AMOLED has **no on-board cell** (board stack is PCB + display module
only); the P4 4C rides its battery on a rear header (the pocket depth clears
it); both seats therefore have no battery bay. The Moon v1 board will carry
its LiPo in a housing bay (MX1.25 pad on the board) — sized at A2.

Build and gates
---------------

Everything runs headless; the scripts are MCP-agnostic (the Blender MCP
add-on can drive ``build_housing.py`` interactively, but the committed
pipeline is the command line). One flatpak note: the Blender sandbox cannot
see ``/tmp``, so all outputs go under the project tree.

.. code-block:: bash

   # build the scene + export the five STLs (in-scene audit runs; exit
   # 0 = clean, 1 = hard-fail list, 2 = build error)
   flatpak run org.blender.Blender -b -P hardware/housing/build_housing.py \
       -- --out hardware/housing/stl

   # STL dimension audit: re-reads the exported STLs and checks the
   # critical radii / holes / USB-C channels within 0.05 mm of
   # params.json (catches a Blender unit bug or a broken selection)
   python3 -I hardware/housing/audit_stls.py

   # render set -> hardware/housing/renders/ (gitignored; this page
   # embeds the files on disk, like the other generated media)
   flatpak run org.blender.Blender -b -P hardware/housing/render.py

The two audits are the gates: ``build_housing.py`` returns a hard-fail list
(pocket radius vs board outline, screw keep-outs, pocket depth vs board
stack, chassis fit, height cap) and ``audit_stls.py`` re-measures the
**exported** STLs in the faceplane frame (collar r 65.8, face/pocket
radii, the r 2.6 screw hole, the USB-C channel cut away at the seat angle
and intact 90° away, plus a unit sanity band of 10–300 mm per axis). The
STL exporter writes world-space millimetres — verified against the
evaluated scene geometry — so the audit runs on the raw values.

Renders
-------

.. figure:: ../hardware/housing/renders/render_hero_s3.png
   :width: 420
   :alt: The housing with the S3 cartridge fitted
   :align: center

   The chassis with the S3 seat cartridge. (Render; ``render.py``.)

.. figure:: ../hardware/housing/renders/render_hero_p4.png
   :width: 420
   :alt: The housing with the P4 4C cartridge fitted
   :align: center

   The P4 4C seat — the cartridge is as big as the board.

.. figure:: ../hardware/housing/renders/render_chassis.png
   :width: 420
   :alt: The bare chassis
   :align: center

   The bare chassis (feet, backstop strut, collar recess on the
   faceplate back).

The full set (chassis, the four hero shots with a fitted cartridge, and one
cartridge-only shot per seat) regenerates from ``render.py``; like all
generated media in this repo it is gitignored build output, never committed.

Prior art (and why none fit)
----------------------------

Community cases exist for the first two boards, but **none fit all three**,
and none carry the anti-flex rule or the swappable-seat idea:

- **S3 AMOLED 1.75** — Printables' "simple case" (1614880) and custom-fit
  cases (1878047); MakerWorld's case with interchangeable stands +
  removable rear plate (3399509), and one with speaker + 10×40×40 battery
  (2477655). Some target the "-G" GPS variant — verify the SKU. Zion Brock's
  *Orb* (a 3D-printed desktop instrument with a rotary knob) is the closest
  prior art to a desk-stand moon clock.
- **P4 4C** — Printables' 4C desk stand (1520845) and MakerWorld's "Pebble"
  weather-clock case (3274860), both documented on Waveshare's own resources
  page.
- **Knob 1.8** — no community cases found (Printables/Thingiverse/MakerWorld
  came up empty); the user's unit is in the stock CNC case, which is what
  the knob seat replaces.

Next: the A2 test print
-----------------------

The A2 step (user-gated) prints the chassis + all four cartridges at the
current ``tol_side`` 0.20, fits the three physical boards (and the stock
knob case), and records the measured clearance per seat into
``params.json``'s ``measured`` fields — then ``tol_side`` is tuned per seat,
the STLs regenerate, and both audits re-run. The Moon v1 seat is
intentionally a stub here: Phase B's ``kicad-cli pcb export step`` feeds the
real board outline into the same ``params.json`` → builder → audit loop, and
the regenerated seat ships in that phase's commit.
