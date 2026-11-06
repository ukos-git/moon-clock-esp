The timezone
============

The timezone / UTC offset: one roller over ALL offsets — every 30-minute UTC offset (UTC−12:00 … UTC+14:00, indices 0–52) plus Europe/Berlin (index 53, DST-aware, the Kconfig default). Releasing the roller applies the zone (``clock_tz_apply``: the P4 re-derives its local digits from the same epoch via the POSIX TZ env var + ``tzset``; the S3 rewrites the PCF85063A's *local* digits epoch-consistently before the switch), re-arms the alarm, and persists the selection. The option list is :cfile:`components/clock/clock_tz.h`'s; reached by the hub's globe icon on BOTH boards.

.. figure:: ../img/screens/annot/settings-tz_s3.png
   :alt: the timezone — the S3 panel (466, annotated)
   :align: center

   The timezone — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-tz_p4.png
   :alt: the timezone — the P4 panel (720, annotated)
   :align: center

   The timezone — P4 720×720 (the same UI_PX layout, scaled)

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
     - Caption (TIMEZONE / UTC OFFSET)
     - The 32 px SSID font.
     - :cfunc:`create_screen_settings_tz`

   * - 2
     - Offset roller
     - The 32 px SSID font (the only font covering letters + ``+``/``:``/``/``), centred and wide enough for "Europe/Berlin"; the selected index IS the option index (the options were generated in index order — no string parsing).
     - ``event_handler_cb_settings_tz_commit`` → :cfunc:`clock_tz_apply` (:cfile:`components/clock/clock_tz.c`) — option ``timezone_idx`` (the store), ``CLOCK_DEFAULT_TIMEZONE`` (the seed)

   * - 3
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
