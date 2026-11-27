Compute progress replaces the bake reveal
   The bake reveal (``UI_MOON_BAKE_REVEAL`` / ``KNOB_UI_BAKE_REVEAL``, added :commit:`5008dade`) showed a computing frame as a black canvas plus one atomic strip band per 50 ms tick.
   That was the "atomicity-per-state" second exception to the atomic-frame rule.
   :commit:`ee4e3efd` deleted its display side (band state machine, pacing, heal path, the hourly fade choreography); the strip loop and its rows watermark survived as the progress source for the seconds arc.
   The same change added the phase disc and stopped the hourly refresh from riding the fade-to-black, so the old frame stays up the whole cycle.
   The 2026-11-14/15 bench numbers of the reveal remain the accepted bake-cost measurements.
   See :doc:`/implementation/compute-progress`.

Observer view added
   ``UI_MOON_OBSERVER_ENABLE`` (:commit:`d177a8fb`) rotates the north-up frame to the parallactic angle of the time zone's reference city; the same change replaced the fixed UTC±hh:mm offsets with the 67 named IANA zones, which alone can carry the reference lat/lon the view is computed from.
   See :doc:`/implementation/observer-view` and :doc:`/ui/settings-tz`.

Burn-in accepted
   With the spin gone the S3 shows the Moon statically, and a faint burned-in Moon is expected on the AMOLED panel.
   The user's call (2026-11-14, :commit:`eed1d856`) is that it is a feature, not a bug, because it will likely not disturb reading the display.
   See :doc:`/implementation/animations`.

Boot replay added
   ``UI_MOON_BOOT_REPLAY`` (:commit:`dabbf475`) brought back a boot presentation as pure playback of stored TF hour frames, with no per-frame bake.
   It took the place of the computed boot sweep and set the new-moon cache policy (nothing older than the last new moon survives on the card).
   On :commit:`7693bffa` the presentation was re-sourced: the stored hours became the fallback for a flash-resident boot movie (:doc:`/implementation/moon-movie`), and the card's role narrowed to the hourly refresh cache.
   See :doc:`/implementation/animations`.

Knob cold-boot sweep retired
   The knob sibling dropped its own cold-boot synodic sweep (:commit:`e8efbee3`); with no SD socket it boots to the single static frame.

Spin and keep-mask removed
   The spin was the burn-in rotation: a once-baked frame re-projected by the Q15.15 gather at 8 fps and written directly to the panel, bypassing LVGL for that frame.
   Its keep-mask was a full-screen ``lv_snapshot`` overlay that kept the clock legible while the Moon rotated beneath the direct writes.
   Both went in :commit:`79937e37`; the home Moon is plain LVGL, shown static.
   The full mechanism is in the appendix below.
   See :doc:`/implementation/architecture`.

Dusk removed
   The dusk was the light-sleep entry: a constant-projection pole spin to the dark side, a backlight dim to 25 %, and the RTC countdown armed on the last frame.
   :commit:`f7740500` replaced it with a static dim frame; the shade map has host-side callers only since then, and sleep depth follows the timer instead of the animation.
   The full mechanism is in the appendix below.
   See :doc:`/implementation/animations` and :doc:`/science/darkside`.

Boot sweep removed
   The clocks' boot animation played the last synodic month: 1344 card-less, tick-displayed frames, always ending on the live full-resolution final bake.
   The clocks dropped it in :commit:`51119200`; the sweep code (``moon_sweep_task`` / ``moon_sweep_interrupt`` / ``s_sweep_*``) and its Kconfig (``UI_MOON_ANIM_*``) are gone from the tree, and the hourly refresh replaced it as the home's live motion.
   The full mechanism is in the appendix below.
   See :doc:`/implementation/animations` and :doc:`/implementation/knob-display`.

P4 moon disk upscaled
   The P4's moon disk grew from 618 to 650 px (``MOON_PX`` 650, ``MOON_R`` 325) in the seam work of :commit:`f23f9c77`.
   See :doc:`/devices/p4`.
