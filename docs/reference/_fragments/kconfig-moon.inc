.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no headings, ../<section>/<page> :doc: targets).

One key, menu *Moon Phase Asset Configuration* (``components/moon/Kconfig``
— the knob's build compiles the same header tier through its own
``--target knob`` assets):

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Key
     - Default
     - Meaning
   * - ``MOON_ALBEDO_W``
     - ``2048`` if ``BOARD_P4`` ·
       ``864`` otherwise
       (864..4096)
     - The equirect 2:1 albedo texture width (height = width/2) baked
       from the 4096×2048 16-bit LROC source by the asset pipeline and
       flashed into the ``moon_assets`` partition. Each board defaults
       to the best resolution its flash + PSRAM budget allows: 864 on
       the S3 (2,470,400 B of the 2560 K partition — the max the PSRAM
       pool can spare), 2048 on the P4 (4,194,304 B of the 5120 K
       partition, matching the 2048-wide relief DEM grid). The width
       must be **even**, and the root ``CMakeLists`` (replicated by
       ``knob/CMakeLists.txt``) ``FATAL_ERROR``s on an odd width, on a
       manifest width different from this value, or on a blob that
       overflows the partition. The Kconfig only selects which
       pre-baked texture the build flashes — after changing it,
       regenerate the target: ``cd assets/images/moon &&
       .venv/bin/python -m moon.assets --target <t>``
       (:doc:`the Python API <../reference/python-api>`)
