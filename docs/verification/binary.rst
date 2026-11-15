Binary size chronology
======================

The last budget of the product is *flash*: the app must fit its
``factory`` partition with headroom, and every change that adds code
has to earn its bytes. This page is the budget log — the current
binary sizes, the partition tables that define the ceilings, and the
chronology of the sizes that got the app where it is.

The current binaries
--------------------

Measured from the checked-in build trees (2026-11-15, HEAD ``830adbd`` —
the recent commits are docs-only, so these are the family-build
binaries):

.. list-table::
   :header-rows: 1
   :widths: 12 18 14 18 18

   * - Board
     - Binary
     - Size
     - ``factory`` ceiling
     - Headroom
   * - Waveshare ESP32-S3-Touch-AMOLED-1.75 (466)
     - ``build/moon-phase.bin``
     - 1,706,960 B (0x1a0bd0)
     - 2,097,152 B (2048 K)
     - 390,192 B (19 %)
   * - Waveshare ESP32-P4-WIFI6-Touch-LCD-XC (720)
     - ``build/p4/moon-phase.bin``
     - 1,859,488 B (0x1c5fa0)
     - 9,437,184 B (9216 K)
     - 7,577,696 B (80 %)
   * - Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (360)
     - ``../build/knob/knob.bin``
     - 1,556,144 B (0x17bcb0)
     - 4,194,304 B (4096 K)
     - 2,638,160 B (63 %)

The three numbers are asymmetric on purpose, and the S3's 19 % is the
meaningful one: the S3 is the board the app *grew into* (the
chronology below), and the project rule is to **check the binary size
after every build** and keep the S3 comfortably under its ceiling.
The S3 has outgrown the old 30 % comfort mark with the family build
(the shared hub, the sky events, the settings stack — see the
chronology); the honest reading is that the *next* meaningful feature
likely triggers the partition widening of rule 3 below. The P4's
9216 K ``factory`` is generous by design (the 32 MB chip, the DSI
stack, LVGL 9 — the P4 app has never been close to a wall), and its
one quirk is the header: the P4 binary's header still declares a
16 MB flash, so esptool prints a *size-mismatch warning* on flash;
the warning is expected and the flash succeeds (the hardware is
32 MB). The knob's 4096 K ceiling (twice the S3's, for a board that
links the same components minus the codecs and sensors) has never
been approached either.

The partition tables
--------------------

The tables are the ceilings, and they are the interesting part —
they encode the history of the asset pipeline, not just the app.
Each table below is the **single-sourced fragment** the device pages
include (the same file in both places, so this page and the per-board
pages cannot drift; the partition diagrams come along with them):

.. include:: ../reference/_fragments/partitions-s3.inc

The ``moon_assets`` row is the one the *device* loads (the
relief cache's 4 MB build in :doc:`the C core <../implementation/core-c>` comes from that 2 MB DEM),
and it explains why the S3's 16 MB chip was a *requirement*, not a
convenience: the 2048×1024 relief DEM alone is 2,097,152 B — the
2 MB flash layout in the project's git history could not hold the
DEM *and* the app. The comment in ``partitions.csv`` records it:
*"the 2MB layout in git history was a config choice, not a hardware
limit — the 2048x1024 relief DEM (2,097,152 B) needed the headroom."*

.. include:: ../reference/_fragments/partitions-p4.inc

.. include:: ../reference/_fragments/partitions-knob.inc

The three ``moon_assets`` rows carry **per-board blobs**: the asset
pipeline (:doc:`the Python API <../reference/python-api>`) bakes one
blob per board from the same LROC/LOLA sources under
``assets/images/moon/out/{s3,p4,knob}/`` — the 864-wide albedo on the
S3 and knob, the 2048-wide one on the P4 (``MOON_ALBEDO_W``,
:doc:`the moon constants <../reference/moon-consts>`), the knob's DEM
halved to 1024×512. The boards differ only in the *display* pipeline
(the bake size, the strip geometry); the source data is identical.
The root ``CMakeLists`` (replicated by the knob's own) checks the
manifest width and the partition fit at configure time — a blob
baked for another board is a build-time ``FATAL_ERROR``, not a
boot-time surprise (:doc:`the build system <../reference/build-system>`).

The S3 ceiling's chronology
---------------------------

The ``factory`` size is the one that has *moved*, and its history is
the app's growth history:

.. code-block:: text
   :caption: The S3 factory partition, 2026

   ceiling   1,392,000 B  (1392 K)   the original layout's app ceiling
   ceiling   1,425,408 B  (1392 K usable)  the "old 1,425,408 B" figure
                                              recorded in the project's invariants
   binary    1,451,472 B  (0x1625d0)  the production build that outgrew the
                                              1,425,408 B ceiling (coredump/trace
                                              facility restore) — the ceiling
                                              had to be widened
   ceiling   2,097,152 B  (2048 K)   the widened factory partition (the 16 MB
                                              chip makes the widening free;
                                              consumers find every partition by
                                              label, not offset)
   binary    1,462,320 B  (0x165030)  the wheel v2 build (the circular
                                              keyboard's widened font + code)
   binary    1,462,560 B  (0x165120)  the 2026-11-02 take — +240 B over
                                              wheel v2; 30% under the ceiling
   binary    1,706,960 B  (0x1a0bd0)  current (2026-11-15, HEAD 830adbd) —
                                              the three-board family build
                                              (shared hub + sky events + the
                                              settings stack over the 11-02
                                              base); 19% under the ceiling —
                                              rule 3 below is now the next
                                              expected step, not a last resort

The widening is the lesson: the app *hit* the ceiling (the production
build at 1,451,472 B exceeded the 1,425,408 B ceiling by ~26 KB),
and the fix was not to *shrink* the app (the coredump/trace facility
was not shrinkable) but to *widen the partition* — a change the 16 MB
chip made free, because the other partitions (``moon_assets`` 2560 K,
``storage`` 64 K, the fixed 80 K) leave the chip's remaining ~11 MB
unused. The invariant in CLAUDE.md records the new ceiling
(**S3 factory 2,097,152 B / 2048 K**) and the rule that the binary
size is checked after every build — the ceiling is a *recorded*
number, not a hardware limit, and the check is what keeps it honest.

The P4's header quirk
---------------------

The P4 binary's header declares a 16 MB flash even though the
hardware is 32 MB. This is an esptool artifact (the header's
``flash_size`` field is set to the 16 MB value the S3 build uses,
and the P4 build does not override it), and it produces a
*size-mismatch warning* on every ``idf.py flash``:

.. code-block:: text
   :caption: The expected warning (P4 flash)

   Warning: The chip has 32MB of flash, but the binary's header
   says 16MB. This is not an error; the flash will proceed.

The warning is *expected and correct to ignore* — the flash
succeeds, the app runs, and the partition table (14428 K used of
32 MB) is unaffected. The only reason to care is the *documentation*:
a reader who sees the warning and assumes a corruption would be
wrong, and these docs record the expected-warning so the next person
who flashes the P4 does not chase a ghost. (The knob needs no such
note: its chip *is* 16 MB, header and hardware agree.)

The headroom policy
-------------------

The project's policy for the S3 (the binding budget):

1. **Check after every build.** The binary size is a *measured*
   number (``ls -l build/moon-phase.bin``), recorded in the commit
   that produced it. A build that grows the S3 binary by more than a
   few KB without a recorded reason is a regression to investigate,
   not a fact to accept.
2. **Keep 30 %+ headroom — as a trend alarm, honestly recorded.**
   The family build landed at **390,192 B (19 %)** on 2026-11-15,
   under the comfort mark: the chronology above is the record of how
   it got there, not a number to sandbag. A build that lands at 90 %
   of the ceiling is the signal that the next feature (a new screen,
   a new font subset, a new animation) will *hit* the ceiling — the
   same situation the 1,451,472 B production build was in — and rule
   3 is the answer, not a code shrink under protest.
3. **Widen the partition, not the chip.** The 16 MB chip has ~11 MB
   of unused space; the ``factory`` ceiling is a *config* choice.
   If the app outgrows the ceiling again, the fix is a
   ``partitions.csv`` edit (a tracked file, a reviewed commit), not
   a code shrink. The shrink is the last resort — the coredump/trace
   facility, the font subsets, and the animation code are not
   optional.
4. **The P4 and the knob are not the budget.** The P4's 9216 K
   ceiling is generous by design and the knob got a 4096 K one from
   the start; both binaries are checked (the rule is
   "check after every build," not "check the S3 after every build")
   but neither is the *binding* constraint. The S3 is the board the
   app has to fit; the P4 and the knob are boards the app *fits
   into*.

The flash budget is the last of the four budgets the product runs on
(the compute budget, :doc:`performance <performance>`; the panel-write budget, :doc:`the display pipeline <../implementation/display>`;
the flash budget, this page). The reference section is the
material: the input-method research the product's circular keyboard
came from (:doc:`the round keyboard <../keyboard>`), the Python and C APIs
use (:doc:`the Python API <../reference/python-api>` and :doc:`the C API <../reference/c-api>`), the build system and Kconfig that keep the
board family buildable — the two clock boards from one tree, the
knob as a sibling project linking the shared components
(:doc:`the build system <../reference/build-system>`), and the glossary and
references (:doc:`the glossary <../glossary>`).
