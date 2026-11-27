The airplane screen (modem sleep)
=================================

.. brief:
   Reader: a firmware developer working on the home screen.
   They already know: C, LVGL basics, the sleep states in outline.
   After this page they can: name every element of the home screen and find its code.
   Page type: reference.

The airplane screen is the home screen while the clock is awake and the radio is off.
It shows the current moon phase, the time, the next alarm and a seconds arc.

How it behaves
--------------

A tap anywhere opens the :doc:`settings hub <settings-hub>`.
Only taps while the clock is awake count: the handler checks ``!clock_is_idle()``.
The tap that wakes the clock lands on a sleep screen's root, which has no handler, so opening the hub takes a second, deliberate tap.

When the idle timeout expires, the clock enters light sleep and the :doc:`light-sleep <light-sleep>` screen replaces this one.
The timeout is the ``idle_timeout_sec`` setting, seeded by ``CLOCK_IDLE_TIMEOUT_SEC``.

Screenshots
-----------

.. figure:: ../img/screens/annot/modem-sleep_s3.png
   :alt: The airplane screen on the S3 panel, with numbered callouts on each element.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/modem-sleep_p4.png
   :alt: The airplane screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/modem_sleep.png
   :alt: The airplane screen on the knob's 360 px panel, without callouts.
   :align: center

   Knob, 360×360: the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``.
   The moon sits at the seam's geometry (r 150 at cy 180), with the limb inside the seconds arc.
   The knob project's battery gauge is not part of the simulator set.

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
     - Moon (the current phase)
     - Baked at the current phase and shown static. The home screen shows this frame from the first frame of boot, and a wake shows it again without animation.
     - :cfunc:`moon_screen_prepare` (the moon component), :cfunc:`tick_screen_modem_sleep`

   * - 2
     - Clock HH:MM
     - The one shared clock widget (layout contract 2 in :cfile:`components/ui/screens.h`). One set of objects is reparented onto this screen's ``modem_sleep_clock`` container. The minute tick and the screen switch update it; the screen never ticks it locally.
     - :cfunc:`create_user_widget_clock`, :cfunc:`tick_screen_modem_sleep`

   * - 3
     - Seconds arc
     - The thin ring at the outer edge (outer radius 223, 10 px from the panel rim). The indicator runs 0–60 s and is rotated 270° so 0 s sits at 12 o'clock. It is disabled and not clickable (the theme's ``add_style_moon_arc``).
     - :cfunc:`create_screen_modem_sleep` (:cfile:`components/ui/screens.c`)

   * - 4
     - Tap anywhere
     - Opens the settings hub. The whole screen is one touch target: the moon image and the clock are not clickable, so the tap lands on the screen root's handler. Awake taps only (``!clock_is_idle()``); the wake tap never opens the hub.
     - ``event_handler_cb_home_tap`` (also the :doc:`wake path <../implementation/clock-sleep>`'s idle-wake entry), option ``idle_timeout_sec`` / ``CLOCK_IDLE_TIMEOUT_SEC``

   * - 5
     - Alarm row (next fire)
     - The next scheduled alarm, under the clock. It is the third element of the shared clock widget and reads the scheduler cache, so the tick does no I2C. The hour drops its leading zero (a 07:05 rule reads "7:05"); minutes stay zero-padded, which reads cleaner against the HH:MM above. A fire more than 24 h away carries a two-letter weekday prefix ("Su 20:00" on Saturday); one inside 24 h reads plain. With no scheduled rule the row reads "--:--" with no prefix.
     - ``refresh_alarm_display`` (:cfile:`components/ui/actions.c`), ``clock_alarm_get_next_scheduled`` (the ``clock`` component)

Implementation notes
--------------------

The clock container and its labels are recursively non-clickable, so every touch reaches the screen root.

The figure below puts the three panels' disk geometry side by side.
It shows the seconds arc's pad (``UI_PX(10)``), where the arc ink lands, and the moon disk's limb gap inside it.
The arc's centre radius from ``get_center`` is the stroke's *outer* edge, and the 12 px stroke is inked inward from it.
``docs/tools/gen_disk_geom.py`` computes every value with the same ``UI_PX`` integer arithmetic as the firmware.

.. figure:: ../_static/figs/disk_geom.png
   :alt: Disk geometry on the S3, P4 and knob panels: the seconds arc ink band, the gap between the moon's limb and the arc, and the disk diameters.
   :align: center
   :width: 100%

   The seconds arc ink band is 212..223 on the S3, 334..345 on the P4 and 162..173 on the knob.
   The limb-to-ink gaps are 12, 9 and 12 px, and each disk diameter is given as a percent of the panel.
   The limb always clears the arc ink; the knob's shared-home seam (disk r 150 at cy 180) is set to exactly that rule (:doc:`knob display <../implementation/knob-display>`).

Where to go next
----------------

- :doc:`The settings hub <settings-hub>`: where a tap on this screen leads.
- :doc:`Light sleep <light-sleep>`: what replaces this screen after the idle timeout.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the idle and wake logic behind both.
