The pomodoro interval
=====================

Two shared clock-style rollers side by side (hours 0–23, minutes 0–59) seed from the stored interval rule (H = N / 60, M = N % 60; a sub-hour rule shows hours 0). The rollers **are** the display (the old separate "H:MM" preview label is gone); only a RELEASED persists (``minutes_total = H*60+M``, 0 rejected — the current rule stays), keeping the SPIFFS write + RTC reschedule off the drag path. The display sits exactly where the home clock's "HH:MM" would be (the hour slot's glyph centre at screen x 153, the minute slot at 303, the colon at 223 — the home label's own glyph centres).

.. figure:: ../img/screens/annot/settings-pomodoro_s3.png
   :alt: the pomodoro interval — the S3 panel (466, annotated)
   :align: center

   The pomodoro interval — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-pomodoro_p4.png
   :alt: the pomodoro interval — the P4 panel (720, annotated)
   :align: center

   The pomodoro interval — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_pomodoro.png
   :alt: the pomodoro interval — the knob panel (360)
   :align: center

   The pomodoro interval — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; raw panel capture, :doc:`the UI index <index>`)

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
     - Caption (REPEAT INTERVAL)
     - The 32 px SSID font; the clock label has none — this screen needs one to say what the value IS.
     - :cfunc:`create_screen_settings_pomodoro`

   * - 2
     - Hour roller (00–23)
     - The 96 px bold-numbers font at 110 px tall, a 140 px wide box (Montserrat's digits are PROPORTIONAL — the worst-case "44" pair advances 132 px, which the old 120 px box clipped).
     - :cfunc:`settings_create_roller`, ``event_handler_cb_settings_pomodoro_released``

   * - 3
     - Minute roller (00–59)
     - Same style, the minute slot; the release persists via ``clock_alarm_set_pomodoro_interval`` (the RTC reschedule stays off the drag path).
     - :cfunc:`settings_create_roller`

   * - 4
     - Exit → alarm settings
     - The shared bottom-centre exit back to the CALLING level — the alarm screen (not the hub).
     - :cfunc:`settings_sub_screen_prepare`

The cycle behind the rollers
-----------------------------

The rollers are the top of a chain that ends in a hardware interrupt.
A RELEASED writes the **one** rule file whole
(:cfile:`components/clock/clock_alarm_scheduler.c`): the interval rule
is ``*/N * * * *`` — an *alarm* rule with a minute interval, not a
separate subsystem — so the two entries on the alarm screen stay
mutually exclusive by construction, and a 0 (``00:00``) never reaches
the file (``clock_alarm_set_pomodoro_interval`` also gates
``1..1440``; the screen's 0-reject is the friendly half of that gate).
The parser's pomodoro corner is the minute field's ``*/N``, which may
span the **whole day** — N up to 1440, the grid anchored at midnight:
the next ring is the first total-minutes-of-day multiple of N strictly
after now, and once today's grid is exhausted it is ``00:00`` of the
next day (an N ≤ 59 rule instead rings at multiples of N *within* the
hour; minute-and-hour intervals together are rejected). The arming
funnel caches the computed next ring and arms the PCF85063A ALM on the
S3 or the ``esp_timer`` one-shot on the P4/knob, and the home alarm
row is a pure **view of that cache** — ``H:MM`` with an unpadded hour,
a 2-letter weekday prefix only while the ring is more than 24 h away,
``--:--`` when nothing is scheduled; the minute tick re-reads it
because the prefix flips exactly at the 24 h boundary. On the knob the
dial commit *sends* ``LV_EVENT_RELEASED`` to the focused roller — the
drag path and the detent path are the same commit. The fire itself (the
shared 1300 ms poll, the trigger, the ring, the fire-then-reschedule)
is :doc:`the alarm lifecycle <../implementation/clock-sleep>`;
these figures own the settings-side machine and the grid arithmetic —
the cycle as a chain first, then the two boxes opened:

.. graphviz:: ../_graphs/pomodoro-cycle.dot
   :alt: The pomodoro cycle as a chain — alarm-rules screen, stopwatch entry, release commit, rule setter, day grid, arming funnel, home row, fire note
   :caption: The whole settings-side machine in one chain: a roller release rewrites the one rule file, the day-grid arithmetic caches the next ring, the home row reads the cache. The FIRE note is the alarm lifecycle's — the interval rule is just the other rule kind that lands there (same 1300 ms poll, same fire-then-reschedule).
   :align: center

.. graphviz:: ../_graphs/pomodoro-cycle-edit.dot
   :alt: The settings machine opened — three glyphs, master tap, mutual-exclusion note, twin rollers with seeding, the dial that sends RELEASED
   :caption: The glyphs' colours come straight from ``clock_alarm_rule_types()``, derived from the PARSED rules so the glyphs cannot disagree with the file; the entries are MUTUALLY EXCLUSIVE (each setter rewrites ``alarm.txt`` with its single rule); the twin rollers are the display (96 px font at the home clock's x-centres 153/303, colon 223 — the separate H:MM preview label is GONE); only a RELEASED persists, and the knob dial's commit *sends* ``LV_EVENT_RELEASED`` so drag and detent share the path.
   :align: center

.. graphviz:: ../_graphs/pomodoro-cycle-grid.dot
   :alt: The rule arithmetic opened — setter gate, full replace, */N day grid anchored at midnight, arming funnel, home row view
   :caption: The setter gates ``1..1440`` and renders ``*/N * * * *`` into a FULL replace + reload; the parser's pomodoro corner is the minute field's ``*/N`` spanning the whole day (N > 59 anchors the grid at midnight, N ≤ 59 rings within the hour, both intervals together are rejected); the single arming funnel caches the nearest ring then arms the PCF85063A ALM (S3) or the ``esp_timer`` one-shot (P4/knob), and the home row stays a pure VIEW of that cache.
   :align: center
