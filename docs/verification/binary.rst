Binary size chronology
======================

The last budget of the product is *flash*: the app must fit its
``factory`` partition with headroom, and every change that adds code
has to earn its bytes. This page is the budget log — the current
binary sizes, the partition tables that define the ceilings, and the
chronology of the sizes that got the app where it is.

The current binaries
--------------------

Measured from the build trees (2026-11-25): the S3 and P4 trees were
configured at the ``0.2.0`` commitizen checkpoint — the first ``cz
bump`` (the pre-bump fallback, the ``git describe`` short hash, no
longer applies) — with this wave's code in the tree (the
**compute-progress repurpose** and the **flash-resident boot movie**:
the strip-by-strip bake reveal and the hourly fade-to-black retired,
the seconds arc now doubles as the bake's progress bar and the boot
shows the quick phase disc —
:doc:`the compute progress <../implementation/compute-progress>`, and
the ``moon_movie`` reader links ``esp_new_jpeg`` into the app and
re-sources the boot animation from the precomputed synodic cycle —
:doc:`the boot movie <../implementation/moon-movie>`) —
and built from
it (the sizes below are those bench binaries; each embeds the calcver
``0.2.0`` — the commitizen ``version`` line the root ``CMakeLists``
reads from ``.cz.toml``, verified by ``strings``. The knob's
``CMakeLists.txt`` replicates the derive for the sibling — the
reader's boot-time calcver check accepts the knob's own movie only
when the firmware is stamped with the same version — and the knob has
no TF card, so its ``moon_tfcache.c`` is inert there). Docs-only
commits after these builds do not move a size. The knob's tree is the
sibling project's own ``../build/knob`` outside the repo, never
``build/knob``):

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
     - 1,734,304 B (0x1a76a0)
     - 2,097,152 B (2048 K)
     - 362,848 B (17 %)
   * - Waveshare ESP32-P4-WIFI6-Touch-LCD-XC (720)
     - ``build/p4/moon-phase.bin``
     - 1,900,672 B (0x1d0080)
     - 9,437,184 B (9216 K)
     - 7,536,512 B (80 %)
   * - Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (360)
     - ``../build/knob/knob.bin``
     - 1,644,832 B (0x191920)
     - 4,194,304 B (4096 K)
     - 2,549,472 B (61 %)

The three numbers are asymmetric on purpose, and the S3's 17 % is the
meaningful one: the S3 is the board the app *grew into* (the
chronology below), and the project rule is to **check the binary size
after every build** and keep the S3 comfortably under its ceiling.
The S3 has outgrown the old 30 % comfort mark with the family build
(the shared hub, the sky events, the settings stack — see the
chronology); the honest reading is that the *next* meaningful feature
likely triggers the partition widening of rule 3 below. The P4's
9216 K ``factory`` is generous by design (the 32 MB chip, the DSI
stack, LVGL 9 — the P4 app has never been close to a wall), and its
header now matches the hardware (the boot-movie wave bumped it to the
real 32 MB; the old 16 MB header's size-mismatch warning is gone —
:ref:`below <p4-header-32mb>`). The knob's 4096 K ceiling (twice the
S3's, for a board that links the same components minus the codecs and
sensors) has never been approached either.

.. figure:: ../_static/figs/binary_budget.png
   :alt: current binaries against their factory ceilings, and the S3 chronology
   :figclass: align-center

   The budget, drawn. **Left:** each current binary against its
   ``factory`` partition (gray = the partition, filled = the app) —
   the S3's 82 % is the live constraint, the P4's 20 % and the knob's
   39 % are headroom by design. The bars are **parsed from the table
   above** (``docs/tools/gen_binary_figs.py`` reads this page), and
   :doc:`the docs drift check <../reference/build-system>`
   (``docs/tools/check_docs_drift.py``) verifies the page against the
   binaries on disk — figure, table and disk are one chain.
   **Right:** the chronology below as a step history — the production
   build punching through the original 1392 KiB ceiling, the widening
   to 2048 K, and the family build growing to 82 % under it.

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
This page is the *app* side of the flash budget; the data side —
every row's occupancy against its reservation, the shipped blobs and
the what-ifs their CSV comments keep — is
:ref:`the storage ladder <storage-ladder-perf>`.

The S3 ceiling's chronology
---------------------------

The ``factory`` size is the one that has *moved*, and its history is
the app's growth history:

.. code-block:: text
   :caption: The S3 factory partition, 2026

   ceiling   1,425,408 B  (1392 KiB)  the original layout's app ceiling — the
                                              "old 1,425,408 B" figure recorded in
                                              the project's invariants (CLAUDE.md,
                                              the partitions.csv comment)
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
   binary    1,511,280 B  (0x170f70)  the 2026-11-04 M4-era take — 72% of the
                                              widened ceiling; the same day's P4
                                              sibling measured 1,605,984 B
                                              (0x188160), docs/devices/p4.rst
   binary    1,706,992 B  (0x1a0bf0)  the 2026-11-15 take, built at
                                              HEAD b0b9e68 + that pass's two
                                              comment-only C edits — the
                                              three-board family build
                                              (shared hub + sky events + the
                                              settings stack over the 11-02
                                              base); 19% under the ceiling
   binary    1,714,384 B  (0x1a28d0)  the 2026-11-16 take — the 17th screen
                                              (settings-display) + the ring
                                              cap over the 11-15 base
   binary    1,714,832 B  (0x1a2a90)  the 2026-11-16 take — the wifi-screen
                                              switch-frame pass (the traffic
                                              hold no longer eats the
                                              screen-switch's atomic frame)
                                              over the 17th-screen base
   binary    1,714,848 B  (0x1a2aa0)  the draw_wait io-error tally plus
                                              the storm-pressure no-mem
                                              ride-out (``ESP_ERR_NO_MEM``
                                              strips retry while the panel
                                              watchdog stands down) over the
                                              switch-frame-pass base
   binary    1,721,968 B  (0x1a4670)  (measured 2026-11-17) — the
                                              timezone rework over the
                                              io-tally base: 67 curated
                                              IANA zones persisted by name
                                              (clock_tz), the observer-view
                                              gather in every ``s_rgb``
                                              build leg
                                              (``moon_viewport_rot_deg`` +
                                              ``moon_rotate``) and the
                                              tz-change re-gather seam; 18%
                                              under the ceiling — rule 3
                                              below is now the next expected
                                              step, not a last resort
   binary    1,722,272 B  (0x1a47a0)  the 2026-11-20 take — the 0.2.0
                                              commitizen checkpoint (the
                                              first ``cz bump``): the
                                              calcver string moves 0.1.0 →
                                              0.2.0 — the commitizen version
                                              line replaces the pre-bump
                                              git-describe key as the
                                              TF-cache folder prefix — and
                                              the key is one same-length
                                              string, so the size stands at
                                              the table's number above
   binary    1,721,472 B  (0x1a4480)  the 2026-11-24 take — the
                                              compute-progress repurpose:
                                              the strip-by-strip bake
                                              reveal and the hourly
                                              fade-to-black retired (their
                                              state machine, pacing and
                                              Kconfig options deleted),
                                              replaced by the rows-
                                              completed watermark the
                                              seconds arc reads (−800 B
                                              net — the seam + the quick
                                              phase disc against the
                                              retired reveal path); 18%
                                              under the ceiling
   binary    1,722,896 B  (0x1a4a10)  the 2026-11-25 take (M2) — the
                                              boot-movie reader module
                                              (the ``moon_movie``
                                              partition's flash-resident
                                              synodic cycle: header probe
                                              + calcver check + q90 gray
                                              JPEG decode, ``esp_new_jpeg``
                                              linked into the app)
                                              compiled in ahead of the
                                              boot re-source; the movie's
                                              10 MB lives in its own
                                              partition row, not here
                                              (+1,424 B net — the reader
                                              + the codec); 18% under
                                              the ceiling
   binary    1,726,912 B  (0x1a59c0)  the 2026-11-25 take (M3) — the
                                              boot re-source: the
                                              presentation's source ladder
                                              (flash movie first, card the
                                              fallback), the movie's
                                              loop + catch-up play, and the
                                              final live bake split out
                                              into its own core-1 task
                                              that runs IN PARALLEL with
                                              the looping movie (+4,016 B
                                              net — the bake task + the
                                              ladder/loop logic against the
                                              single-task replay); 18%
                                              under the ceiling
   binary    1,734,304 B  (0x1a76a0)  current (measured 2026-11-25) —
                                              the sleep-timer kill
                                              switches: the two
                                              ``*_enable`` store keys, the
                                              live rung gates (``clock_idle.c``
                                              idle check, the countdown arm,
                                              the knob's supervisor), the
                                              power-off glyph into the three
                                              solid font tiers, and the
                                              power-off toggle + locked
                                              roller on both sleep screens
                                              (+7,392 B net — the glyph's
                                              three raster tiers dominate);
                                              17% under the ceiling

The widening is the lesson: the app *hit* the ceiling (the production
build at 1,451,472 B exceeded the 1,425,408 B ceiling by ~26 KB),
and the fix was not to *shrink* the app (the coredump/trace facility
was not shrinkable) but to *widen the partition* — a change the 16 MB
chip made free, because the other partitions (``moon_assets`` 2560 K,
``storage`` 64 K, the fixed 64 K prefix) left the chip's remaining
~11 MB unused (today the ``moon_movie`` row owns that space). The
invariant in CLAUDE.md records the new ceiling
(**S3 factory 2,097,152 B / 2048 K**) and the rule that the binary
size is checked after every build — the ceiling is a *recorded*
number, not a hardware limit, and the check is what keeps it honest.

.. _p4-header-32mb:

The P4's 32 MB header
---------------------

The P4 binary's header used to declare a 16 MB flash even though the
hardware is 32 MB — a shared-defaults leftover (the ``ESPTOOLPY
_FLASHSIZE_16MB`` pin lived in the board-neutral ``sdkconfig.defaults``
because the P4 build inherited it), and it produced a *size-mismatch
warning* on every ``idf.py flash``:

.. code-block:: text
   :caption: The (now-gone) warning, recorded for history

   Warning: The chip has 32MB of flash, but the binary's header
   says 16MB. This is not an error; the flash will proceed.

The boot-movie wave fixed it properly (:commit:`1703e278`): the flash-size pin
moved out of the shared defaults into the per-target files — the S3
keeps 16 MB (its chip), the P4 took its real 32 MB
(``sdkconfig.defaults.esp32p4``) — and the P4's ``partitions_p4.csv``
grew the ``moon_movie`` LAST row (18304 K), so the table now fills the
whole 32 MB chip (32768 K including the 64 K prefix) and the movie
partition's row starts at 14464 K, **past the old 16 MB mark**. The
header now matches the hardware, the size-mismatch warning is gone,
and a data partition living above 16 MB reads back correctly on the
real 32 MB chip (bench-verified). (The knob needs no such note: its
chip *is* 16 MB, header and hardware agree.)

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
   a new font subset, a new animation — the font bill is priced
   glyph-by-glyph at :ref:`the glyph ladder <glyph-ladder-perf>`)
   will *hit* the ceiling — the
   same situation the 1,451,472 B production build was in — and rule
   3 is the answer, not a code shrink under protest.
3. **Widen the partition, not the chip.** The 16 MB chip's leftover
   is now spoken for by the ``moon_movie`` row (11648 K, the boot
   movie's synodic cycle) — so the ``factory`` ceiling is a *config*
   choice, and if the app outgrows it again the fix is a
   ``partitions.csv`` edit (a tracked file, a reviewed commit): widen
   ``factory``, shrink ``moon_movie``, re-run the movie generator (it
   lands on a coarser frame step) — not a code shrink. The shrink is
   the last resort — the coredump/trace facility, the font subsets,
   and the animation code are not optional.
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
