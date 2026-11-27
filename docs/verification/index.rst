Verification and performance
============================

.. brief:
   Reader: a developer who wants to reproduce a claim or change a number.
   They already know: the project and its build/flash commands.
   After this page they can: pick the right evidence page for a claim.
   Page type: reference (section index).

This section holds the evidence behind the claims elsewhere in the
docs: that the Moon matches NASA, that the boards really behave as
described, how fast each step runs, and how the firmware size has
changed.

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Page
     - What it shows
   * - :doc:`image-gate`
     - The automated test that compares the clock's Moon with NASA's,
       pixel by pixel, for all three boards.
   * - :doc:`device-verify`
     - How behaviour on real hardware is checked from serial logs.
   * - :doc:`demo-mode`
     - The scripted tour that runs every screen on all three boards at
       once, for filming and comparison.
   * - :doc:`performance`
     - Timings for every stage on the PC and on each board, with the
       commands that produce them.
   * - :doc:`binary`
     - The firmware size per board, its limit, and how it got there.

.. toctree::
   :hidden:

   image-gate
   device-verify
   demo-mode
   performance
   binary
