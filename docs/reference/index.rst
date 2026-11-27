Reference
=========

.. brief:
   Reader: a developer looking up an option, constant, layout or API.
   They already know: the project and its build.
   After this page they can: find the exact reference page for a fact.
   Page type: reference (section index).

Look-up material: the build system, every compile-time option, the
constants, the flash layout, the stored settings, and the APIs. These
pages describe; they do not teach. For explanations, see :doc:`the
firmware <../implementation/index>` and :doc:`the science
<../science/index>`.

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Page
     - Contents
   * - :doc:`build-system`
     - How the boards share one source tree, the configuration files,
       the asset pipeline and the simulators.
   * - :doc:`kconfig-board`, :doc:`kconfig-clock`, :doc:`kconfig-ui`,
       :doc:`kconfig-knob`
     - Every Kconfig option with its default and range.
   * - :doc:`moon-consts`
     - The renderer's constants and where they are defined.
   * - :doc:`partitions`
     - The flash partition table of each board.
   * - :doc:`spiffs-store`
     - The settings and credential files the firmware keeps on flash.
   * - :doc:`python-api`
     - The Python reference model used to validate the C code.
   * - :doc:`c-api`
     - The public C headers of the Moon library.
   * - :doc:`sources`
     - The papers, datasets and tools the docs cite.

The :doc:`glossary <../glossary>` defines the terms used across the site.

.. toctree::
   :hidden:

   build-system
   kconfig-board
   kconfig-clock
   kconfig-ui
   kconfig-knob
   moon-consts
   partitions
   spiffs-store
   python-api
   c-api
   sources
