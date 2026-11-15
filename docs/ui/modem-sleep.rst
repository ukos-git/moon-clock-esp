The airplane screen (modem sleep)
=================================

The modem-sleep screen is the awake state at rest: the current moon phase, the clock, and the seconds arc. Any tap opens the settings hub (the screen-root handler ``event_handler_cb_home_tap``; the clock container and its labels are recursively non-clickable so touches reach the root). When the idle timeout (``idle_timeout_sec``, seeded by ``CLOCK_IDLE_TIMEOUT_SEC``) expires, the device enters light sleep and this screen is replaced by the :doc:`light-sleep <light-sleep>` screen.

.. figure:: ../img/screens/annot/modem-sleep_s3.png
   :alt: the airplane screen (modem sleep) — the S3 panel (466, annotated)
   :align: center

   The airplane screen (modem sleep) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/modem-sleep_p4.png
   :alt: the airplane screen (modem sleep) — the P4 panel (720, annotated)
   :align: center

   The airplane screen (modem sleep) — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/modem_sleep.png
   :alt: the airplane screen (modem sleep) — the knob panel (360)
   :align: center

   The airplane screen (modem sleep) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; the moon sits at the seam's geometry — r 150 at cy 180, limb inside the seconds arc; the knob project's battery gauge is not part of the sim set)

Elements
--------

.. list-table::
   :header-rows: 1
   :widths: 4 24 44 28

   * - #
     - Element
     - Purpose
     - Code / option

   * - 1
     - Moon — the current phase
     - Baked statically at the current phase — the home is this frame from the first frame of boot (the boot-only sweep ``UI_MOON_ANIM_ENABLE`` used to end on it; retired 2026-11-13). Wakes show it static — no re-animation.
     - :cfunc:`moon_screen_prepare` (the moon component), :cfunc:`tick_screen_modem_sleep`

   * - 2
     - Clock HH:MM
     - The ONE shared clock widget (layout contract 2 in :cfile:`components/ui/screens.h`): one set of objects, reparented onto this screen's ``modem_sleep_clock`` container; ticked by the minute tick and the screen switch, never screen-locally.
     - :cfunc:`create_user_widget_clock`, :cfunc:`tick_screen_modem_sleep`

   * - 3
     - Seconds arc
     - The thin ring pushed to the outer edge (outer radius 223, 10 px from the panel rim): indicator 0–60 s, rotated 270° so 0 s sits at 12 o'clock. Disabled (non-clickable, the theme's ``add_style_moon_arc``).
     - :cfunc:`create_screen_modem_sleep` (:cfile:`components/ui/screens.c`)

   * - 4
     - Tap anywhere
     - Opens the settings hub. The whole screen is one touch target — the moon image and the clock pass-through are non-clickable, so the tap lands on the screen root's handler.
     - ``event_handler_cb_home_tap`` (also the :doc:`wake path <../implementation/clock-sleep>`'s idle-wake entry), option ``idle_timeout_sec`` / ``CLOCK_IDLE_TIMEOUT_SEC``
