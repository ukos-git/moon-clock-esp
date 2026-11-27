Round keyboard v2 shipped
   The v2 12-sector hybrid wheel was approved and implemented in :commit:`47844eed`.
   It replaced the v1 two-frame wheel (an inner ring grouped in threes plus an outer reveal at twice the angle), whose 2× overlap needed a nearest-centre tie-break.
   v1 passed raw degrees to ``lv_obj_transform_rotation`` (which takes 0.1° units), a latent bug that under-rotated its ticks 10×; the v2 rewrite fixed it.
   v1 also had an in-ring exit; v2 dropped it, because the xmark glyph became the discard key.
   The alternatives study behind the choice is the research report of 2026-11-01 (37 sources).
   See :doc:`/keyboard` and :doc:`/keyboard-lab`.

Keyboard bottom band re-paired
   The v2 proposal put a ⊗ commit and a trash-can discard in the bottom band.
   :commit:`f8c8d95b` (2026-11-08) switched to the house circle-check for commit and circle-xmark for discard: X means discard, the check means accept.
   The bar-built trash can read badly on the panel and was dropped.
   The operable widgets on the keyboard pages still show the proposal's pairing.
   See :doc:`/keyboard`.
