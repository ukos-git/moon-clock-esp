The timezone
============

The timezone: one roller over the curated zone table — :cfile:`components/clock/clock_tz.c`'s 67 named IANA zones, shown by reference city ("Berlin", "Tokyo", "New York", …). The generic UTC±hh:mm offsets were retired 2026-11-17: a zone must carry a DST rule *and* the reference lat/lon the moon's observer view is computed from, which the fixed offsets never had. Releasing the roller applies the zone (``clock_tz_apply``: the P4 re-derives its local digits from the same epoch via the POSIX TZ env var + ``tzset``; the S3 rewrites the PCF85063A's *local* digits epoch-consistently before the switch), re-arms the alarm, persists the zone **name** (``timezone = Europe/Berlin`` in the store — a legacy numeric ``timezone_idx`` file migrates once at load), and re-gathers the moon in place for the new city (``moon_ui_tz_changed`` — no re-bake, :doc:`observer view <../implementation/observer-view>`). The option list is :cfile:`components/clock/clock_tz.h`'s; reached by the hub's globe icon on BOTH boards (on the knob it applies and persists as here; its fixed north-up viewport keeps the moon un-tilted — the ``moon_ui_tz_changed`` seam is a no-op there, :doc:`../implementation/knob-display`).

.. figure:: ../img/screens/annot/settings-tz_s3.png
   :alt: the timezone — the S3 panel (466, annotated)
   :align: center

   The timezone — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-tz_p4.png
   :alt: the timezone — the P4 panel (720, annotated)
   :align: center

   The timezone — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_tz.png
   :alt: the timezone — the knob panel (360)
   :align: center

   The timezone — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; raw panel capture, :doc:`the UI index <index>`)

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
     - Caption (TIMEZONE)
     - The 32 px SSID font.
     - :cfunc:`create_screen_settings_tz`

   * - 2
     - City roller
     - The 32 px SSID font (the only font covering letters + spaces), centred and wide enough for the longest labels (Johannesburg, Buenos Aires); the selected index IS the table index — the option strings are generated from the table in order, no string parsing. Refresh selects ``clock_tz_index_of(settings_timezone())``, falling back to the default index when the stored name is not in the table (a custom POSIX zone).
     - ``event_handler_cb_settings_tz_commit`` → :cfunc:`clock_tz_apply` (:cfile:`components/clock/clock_tz.c`) → ``settings_set_timezone`` + ``settings_store_save`` + :cfunc:`moon_ui_tz_changed` — key ``timezone`` (the store, the zone's IANA name), ``CLOCK_DEFAULT_TIMEZONE`` (the seed)

   * - 3
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
