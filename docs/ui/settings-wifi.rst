The network list
================

The AP scan list: the "NETWORKS" header (the WiFi icon) above one ``lv_roller`` — the family's list since 2026-11-11 (user decision; the knob asked for a roller on 2026-11-10 because the label rows read too small at 360 px, and the clocks adopted the same widget so there is exactly one Wi-Fi selection UI — the old 7 rows + left-margin sel bar are gone). The window's centre **is** the cursor: the SELECTED entry renders white, its neighbours grey, no highlight box — the same cursor language the rows used. A background scan (``wifi_scan_begin``) runs while the screen is open; the roller's options render when the scan finishes (the 10 ms tick polls ``wifi_scan_in_progress()``). While the scan runs — and when it found nothing — the roller is hidden and a **centred placeholder label** carries the line ("SCANNING" / "NO APS FOUND"); it is deliberately not a roller option, because a one-option NORMAL-mode roller clamps its scroll at 0 and would pin the single line to the top of the window (user 2026-11-11: "scanning is not centered"). A bounded 4-attempt / 1 s retry covers a first scan that returns 0 APs. Entries are font-safe: the SSID font covers only space, ``-``, ``:``, digits and UPPERCASE — the SSID is uppercased and truncated to 14 chars + ``+``, the strength carries the "DBM" suffix (lowercase dBm would render as missing-glyph boxes). Entries are **centre-aligned** (user 2026-11-11: a LEFT-aligned column read as "ragged left, not centered as I want it") — an entry wider than the roller clips symmetrically at the panel's chord, but entries are already truncated, and the raw, original-case SSID still reaches the keyboard (the display transform is cosmetic). Tapping the roller's centre entry — or the knob's dial commit, which re-sends exactly that ``CLICKED`` — enters the :doc:`wheel keyboard <settings-wifi-password>` with that AP's SSID prefilled (the credential keeps its original case).

Two LVGL geometries matter in ``create_screen_settings_wifi``: the tier
face (the ``_48_ssid`` file, baked per tier — 37 px knob / 48 px S3 /
74 px P4) and the border width must be set BEFORE
``lv_roller_set_visible_row_count``, which sizes the roller's height
from the font CURRENTLY set on the MAIN part (LVGL 9.6
``lv_roller.c:241`` — fonts afterwards froze the height at the default
font's and clipped the window to ~1.5 rows); and the clocks widen the
roller to ``UI_PX(430)`` (the round panel's chord at the list's band)
where the knob keeps its ``UI_PX(300)``. Visible rows: 4 on the knob,
3 on the clocks (the bigger face, the same band between title and the
bottom-centre exit).

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
     - Roller — centre tap → the wheel
     - The family ``lv_roller`` (NORMAL mode — clamps at the ends like a real roller, no wrap on a finite AP list); the window-centre tap prefills the raw SSID (case kept) and loads the wheel screen, then refreshes it (the handler's log line names the SSID length).
     - ``event_handler_cb_wifi_roller``, :cfunc:`settings_wheel_prefill_ssid` — the credentials land in :cfile:`spiffs/wifi_credentials.txt` (gitignored, runtime-owned)

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
