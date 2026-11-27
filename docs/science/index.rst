The science
===========

.. brief:
   Reader: a developer who wants the model with the maths behind it.
   They already know: vectors, the dot product and basic trigonometry.
   After this page they can: pick the right science page and read order.
   Page type: reference (section index).

This section explains how the clock computes a picture of the Moon and
why the result can be trusted. It is the detailed version of
:doc:`How the clock works <../how-it-works>`: the same ideas, with the
vectors, equations and C code behind them.

You need vectors, the dot product and basic trigonometry. Each page
starts with a plain-language summary, so you can read the summaries
first and come back for the maths.

Read the pages in order. Each one builds on the one before.

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Page
     - What it answers
   * - :doc:`reference-images`
     - Where the NASA maps and reference frames come from, and two
       loading mistakes that would quietly spoil the picture.
   * - :doc:`geometry`
     - For any moment: where is the Sun, where is the Earth, and how is
       the Moon turned?
   * - :doc:`lighting`
     - How bright is each pixel? The cosine law, the terrain, the
       albedo and the tone curve.
   * - :doc:`darkside`
     - What the night side should look like, and why the clock shows it
       a little brighter than nature.
   * - :doc:`art-pass`
     - A display-only pass that goes beyond NASA's look: more contrast
       on the night side, a soft terminator and a warm tint. The tests
       run without it.
   * - :doc:`interactive`
     - The whole model running in your browser, with its main
       parameters on sliders.

.. toctree::
   :hidden:

   reference-images
   geometry
   lighting
   darkside
   art-pass
   interactive
