Binary size chronology
======================

.. brief:
   Reader: a developer about to add code or a font subset, who needs to know how much flash is left.
   They already know: what an ESP-IDF partition table and an app binary are.
   After this page they can: read each board's headroom, check a new build against its ceiling, and know what to do when the S3 runs out.
   Page type: reference, with a chronology log.

Every board's app binary must fit its ``factory`` partition, and the S3 is the board where that is tight.
Today the S3 binary is 1,734,304 B against a 2,097,152 B (2048 K) ceiling, which leaves 17 % headroom.
The P4 binary is 1,900,672 B against 9216 K (80 % free), and the knob's is 1,644,832 B against 4096 K (61 % free).
This page is the flash budget log: the current sizes, the partition tables that set the ceilings, the S3's size history, and the policy for when the S3 runs short.

The current binaries
--------------------

The sizes below come from the bench build trees measured on 2026-11-25.
The S3 and P4 trees were configured at the ``0.2.0`` commitizen checkpoint, with the compute progress (:doc:`the compute progress <../implementation/compute-progress>`) and the flash-resident boot movie (:doc:`the boot movie <../implementation/moon-movie>`) in the tree.
The boot movie's ``moon_movie`` reader links ``esp_new_jpeg`` into the app.

Each binary embeds the calcver ``0.2.0``: the root ``CMakeLists`` reads the commitizen ``version`` line from ``.cz.toml``, and ``strings`` on the binary confirms it.
The knob's ``CMakeLists.txt`` repeats the same derivation, because the reader's boot-time calcver check accepts the knob's own movie only when the firmware carries the same version.
The knob has no TF card, so its ``moon_tfcache.c`` is inert.
Docs-only commits after these builds do not change a size.
The knob's build tree is the sibling project's own ``../build/knob`` outside the repo, never ``build/knob``.

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

The S3's 17 % is the number that matters.
The S3 is the board the app grew into (see `The S3 ceiling's chronology`_), and the project rule is to check the binary size after every build and keep the S3 well under its ceiling.
The S3 is below the old 30 % comfort mark because of the family build: the shared hub, the sky events and the settings stack.
The next large feature will probably need the partition widening in rule 3 of `The headroom policy`_.

The P4's 9216 K ``factory`` partition is generous by design: the chip has 32 MB, and the DSI stack and LVGL 9 have never brought the P4 app near a wall.
Its binary header matches the hardware's 32 MB (:ref:`see below <p4-header-32mb>`).
The knob's 4096 K ceiling is twice the S3's, for a board that links the same components minus the codecs and sensors, and its binary has never come close to it.

.. figure:: ../_static/figs/binary_budget.png
   :alt: Left, each board's current binary as a bar inside its factory partition. Right, the S3 binary size over time against its old and new ceilings.
   :figclass: align-center

   The flash budget.
   **Left:** each current binary against its ``factory`` partition (gray is the partition, the filled bar is the app).
   The S3 at 82 % is the live constraint; the P4 at 20 % and the knob at 39 % have headroom by design.
   **Right:** the S3 chronology as a step history: the production build breaking through the original 1392 KiB ceiling, the widening to 2048 K, and the family build growing to 82 % under it.

``docs/tools/gen_binary_figs.py`` draws the left panel from the table above, which it parses from this page.
:doc:`The docs drift check <../reference/build-system>` (``docs/tools/check_docs_drift.py``) compares the same table with the binaries on disk.
The figure, the table and the files on disk are one chain, so a stale size fails the build.

To check a fresh build, compare its size with the table:

.. code-block:: bash

   ls -l build/moon-phase.bin build/p4/moon-phase.bin ../build/knob/knob.bin
   docs/.venv/bin/python docs/tools/check_docs_drift.py

The partition tables
--------------------

The partition tables set the ceilings, and they also record how the asset pipeline grew.
Each table below is the single-sourced fragment that the device pages include as well, so this page and the per-board pages cannot drift apart.
The partition diagrams come with them.

.. include:: ../reference/_fragments/partitions-s3.inc

The ``moon_assets`` row is the one the device loads.
The relief cache's 4 MB build in :doc:`the C core <../implementation/core-c>` comes from that 2 MB DEM (digital elevation model).
The DEM is why the S3's 16 MB chip is a requirement, not a convenience: the 2048×1024 relief DEM alone is 2,097,152 B.
The 2 MB flash layout in the project's git history could not hold both the DEM and the app.
The comment in ``partitions.csv`` records it:
*"the 2MB layout in git history was a config choice, not a hardware limit — the 2048x1024 relief DEM (2,097,152 B) needed the headroom."*

.. include:: ../reference/_fragments/partitions-p4.inc

.. include:: ../reference/_fragments/partitions-knob.inc

The three ``moon_assets`` rows hold per-board blobs.
The asset pipeline (:doc:`the Python API <../reference/python-api>`) bakes one blob per board from the same LROC/LOLA sources, under ``assets/images/moon/out/{s3,p4,knob}/``.
The S3 and the knob get the 864-wide albedo, the P4 the 2048-wide one (``MOON_ALBEDO_W``, :doc:`the moon constants <../reference/moon-consts>`), and the knob's DEM is halved to 1024×512.
The boards differ only in the display pipeline (the bake size and the strip geometry); the source data is the same.

The root ``CMakeLists`` (and the knob's copy of its guards) checks the manifest width and the partition fit at configure time.
A blob baked for another board is a build-time ``FATAL_ERROR``, not a surprise at boot (:doc:`the build system <../reference/build-system>`).

This page covers the app side of the flash budget.
The data side (every row's occupancy against its reservation, the shipped blobs, and the what-ifs in the CSV comments) is :ref:`the storage ladder <storage-ladder-perf>`.

.. _p4-header-32mb:

The P4's 32 MB header
---------------------

The P4 binary's header declares 32 MB of flash, which matches the hardware.
The flash-size setting lives in the per-target defaults files: ``sdkconfig.defaults.esp32s3`` keeps 16 MB for the S3's chip, and ``sdkconfig.defaults.esp32p4`` sets 32 MB.

``partitions_p4.csv`` ends with the ``moon_movie`` row (18304 K).
The table fills the whole 32 MB chip (32768 K including the 64 K prefix), and the movie row starts at 14464 K, above the 16 MB mark.
A data partition above 16 MB reads back correctly on the real 32 MB chip (bench-verified).
The knob needs no such note: its chip has 16 MB, and header and hardware agree.

The P4 header declared 16 MB until :commit:`1703e278`; see :doc:`/project/history`.

The S3 ceiling's chronology
---------------------------

The S3 ``factory`` size is the ceiling that has moved, and its history is the app's growth history.
Each line is a measured binary or a ceiling change, oldest first:

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

The widening is the lesson of this log.
The production build at 1,451,472 B exceeded the 1,425,408 B ceiling by about 26 KB.
The fix was not to shrink the app, because the coredump/trace facility could not shrink.
The fix was to widen the partition, which the 16 MB chip made free.
The other partitions (``moon_assets`` 2560 K, ``storage`` 64 K, and the fixed 64 K prefix) left about 11 MB of the chip unused; the ``moon_movie`` row owns that space today.

The invariant in CLAUDE.md records the ceiling (S3 factory 2,097,152 B / 2048 K) and the rule to check the binary size after every build.
The ceiling is a recorded number, not a hardware limit, and the check after every build is what keeps it honest.

The headroom policy
-------------------

The S3 is the binding budget, and the project follows four rules for it:

1. Check after every build.
   The binary size is a measured number (``ls -l build/moon-phase.bin``), recorded in the commit that produced it.
   A build that grows the S3 binary by more than a few KB without a recorded reason is a regression to investigate, not a fact to accept.
2. Keep 30 % or more headroom as a trend alarm, and record honestly when you miss it.
   The family build landed at 390,192 B (19 %) on 2026-11-15, under the comfort mark.
   The chronology above records how it got there; it is not a number to sandbag.
   A build at 90 % of the ceiling signals that the next feature will hit the ceiling, as the 1,451,472 B production build did.
   That feature might be a new screen, a new font subset or a new animation; :ref:`the glyph ladder <glyph-ladder-perf>` prices the fonts glyph by glyph.
   Rule 3 is the answer then, not a reluctant code shrink.
3. Widen the partition, not the chip.
   The ``moon_movie`` row (11648 K, the boot movie's synodic cycle) uses the 16 MB chip's leftover space, so the ``factory`` ceiling is a configuration choice.
   If the app outgrows it again, edit ``partitions.csv`` in a reviewed commit: widen ``factory``, shrink ``moon_movie``, and re-run the movie generator (it lands on a coarser frame step).
   Shrinking code is the last resort; the coredump/trace facility, the font subsets and the animation code are not optional.
4. The P4 and the knob are not the budget.
   The P4's 9216 K ceiling is generous by design, and the knob has had a 4096 K ceiling from the start.
   Both binaries are checked (the rule is "check after every build", not "check the S3 after every build"), but neither is the binding constraint.
   The S3 is the board the app has to fit; the P4 and the knob are boards the app fits into.

Where to go next
----------------

Flash is the last of the four budgets the product runs on.
The others are the compute budget (:doc:`performance <performance>`) and the panel-write budget (:doc:`the display pipeline <../implementation/display>`).

- :doc:`The build system <../reference/build-system>` covers the build and Kconfig that keep the board family buildable: the two clock boards from one tree, and the knob as a sibling project linking the shared components.
- The reference section holds the rest of the material: the input-method research behind the circular keyboard (:doc:`the round keyboard <../keyboard>`), the APIs (:doc:`the Python API <../reference/python-api>` and :doc:`the C API <../reference/c-api>`), and :doc:`the glossary <../glossary>`.
