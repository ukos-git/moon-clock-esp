The network list
================

The AP scan list: the "NETWORKS" header (the WiFi icon) and 7 rows at 24 px steps (the first row at absolute y 185 — the band between the title and the exit button; the old 12 rows at 30 px ran off the panel). A background scan (``wifi_scan_begin``) runs while the screen is open; the rows render when the scan finishes (the 10 ms tick polls ``wifi_scan_in_progress()``), with a "SCANNING" placeholder in the meantime — and a bounded 4-attempt / 1 s retry when the first scan returns 0 APs (a first open that would otherwise render "NO APS FOUND" once). The selected row is marked by a white bar in the left margin (the SSID font has no bullet glyph, so the indicator is a plain object) plus the white-on-grey text colour. Row text is font-safe: the SSID font covers only space, ``-``, ``:``, digits and UPPERCASE — so the strength is shown as "DBM" (lowercase dBm would render as missing-glyph boxes). A tap on a row enters the :doc:`wheel keyboard <settings-wifi-password>` with that AP's SSID prefilled (the raw SSID — the row display uppercases for font-safety only, the credential must keep the original case).

.. figure:: ../img/screens/annot/settings-wifi_s3.png
   :alt: the network list — the S3 panel (466, annotated)
   :align: center

   The network list — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-wifi_p4.png
   :alt: the network list — the P4 panel (720, annotated)
   :align: center

   The network list — P4 720×720 (the same UI_PX layout, scaled)

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
     - Header (WiFi icon)
     - The 96 px solid WiFi glyph above the title.
     - :cfunc:`create_screen_settings_wifi`

   * - 2
     - Title (NETWORKS)
     - The 32 px SSID font.
     - :cfunc:`create_screen_settings_wifi`

   * - 3
     - AP row — tap → the wheel
     - Seven rows (the ``WIFI_SETTINGS_ROWS`` band budget); the row tap prefills the SSID (case kept) and loads the wheel screen, then refreshes it (the row handler's log line names the SSID length).
     - ``event_handler_cb_wifi_row``, :cfunc:`settings_wheel_prefill_ssid` — the credentials land in :cfile:`spiffs/wifi_credentials.txt` (gitignored, runtime-owned)

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
