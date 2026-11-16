The Knob board
==============

**Status: moon-display firmware on the bench, on the clock's shared
components** — the ``knob/`` sibling project boots its QSPI panel,
loads the pre-baked moon assets into PSRAM and renders the physical
moon (K7 milestones: K7a asset target + gate green, K7b static
frame, K7c sky-event tint + day tick — its cold-boot synodic sweep
retired 2026-11-14, the clocks having dropped their own the day
before —, K7d
SNTP time with the honest ``no time`` posture, K7e idle ladder +
EXT0 dial wake). The **P0 family seam** (2026-11-09) made it the
third board of the moon-clock family: the project now links the
clock's shared ``components/{clock, ui, wifi, moon}`` behind a
knob-owned ``board``/``imu`` shim. The **P1 parity step** (same day)
put the family's own app on the glass: the shared home screen —
moon image, split-flap HH:MM, alarm row, seconds arc — runs over the
``knob_moon_render.c`` seam (generated ``fonts_knob/`` tier), the
idle ladder dims to the clocks' light-sleep look (LEDC 25 % + clock widget
20 %), and a fixed alarm rule **shows** across real deep sleep via
the internal RTC **TIMER** wake (silent — no codec). The **P2 dial
step** (same day) made the settings surface operable without touch:
hub ring, alarm screen, rollers, the WiFi list (an ``lv_roller`` in
the 37 px SSID font since 2026-11-10 — the label rows read too small
on the 1.8", so the dial *rolls* the list now) and the wheel keyboard
each carry one dial-stepped edit cursor, and a background tap
commits it through the native handlers
(:ref:`dial-semantics-knob`). The **P3 alarm-editing step** (same
day) put the weekday-arc range handles under the dial as well, so a
full rule — hour, minute, start/end day — is set with dial + tap and
saved through the native handler into ``/spiffs/alarm.txt``. The
**P4 battery gauge** (same day) added the battery home-screen
overlay reading GPIO 1 behind the divider on the **placeholder**
LiPo calibration — redone 2026-11-13 as a small iPhone-style
status-bar icon (capsule + nub + fill + docked-bolt heuristic) at
the bottom centre — the gauge ships, the multimeter ritual still
owes it honest numbers.
The earlier
**companion-input** role (milestones K0–K6, UDP frames driving the
clock's screens) was **discarded 2026-11-08** at the owner's
direction — no companion code remains anywhere in the repo, and the
clock lost its listener in the same change. This page records the
bench-observed facts the port started from.

What it is
----------

The **Waveshare ESP32-S3-Knob-Touch-LCD-1.8**
(`product page <https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm>`__)
is a 1.8-inch round-LCD knob: a rotary encoder around a small round
touch display. It is a **dual-MCU** board —

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Chip
     - Role
   * - ESP32-S3R8
     - the app MCU: panel, touch, rotary encoder
   * - ESP32-U4WDH
     - the **knob-side MCU**, a programmable ESP32-class SoC
       (classic-ESP32 core: Wi-Fi/BT, dual-core 240 MHz) — not a
       dumb bridge chip; the schematic gives it its **own encoder
       (EC2)**, its own I2S feed to the DAC, and the CH340-family
       UART bridge that is the board's working USB side

The board has **one** USB-C receptacle, and it is
**orientation-switchable between the two MCUs** (Waveshare wiki,
read 2026-11-07): *"Bidirectional Switchable for ESP32-S3/ESP32"* —
*"the two insertion orientations of the Type-C plug connect to
different download channels"*. One plug rotation is therefore *meant*
to reach the U4WDH (the channel the bench confirmed) and the other
the ESP32-S3; the wiki's own recovery advice is "if programming
fails, flip the Type-C plug", and the S3 side has a manual download
mode as well: *"press and hold the button, power on again to enter
download mode"*.

The user-accessible controls (per the wiki's back-side callout
list): a **power button** (item 5) and an **ESP32-S3R8 BOOT button**
(item 7) — both on the *back* of the board, which is where the
download-mode entry ("press and hold the button, power on again")
happens. There is **no exposed front button**: the top carries only
the dial. The encoders are **dual — one per MCU** ("acting on
ESP32S3 and ESP32 respectively"), so each chip reads its own encoder
and neither knob is a push-button.

Its role in this project is a **family board running the full app**
(K7 milestones → P0–P4): the shared home screen, settings hub and
sleep/timezone/WiFi/alarm screens compile in verbatim; touch works
everywhere natively, and the dial adds a one-cursor edit path
(a detent steps the screen's cursor, a tap commits —
:ref:`dial-semantics-knob <dial-semantics-knob>`). The dial's other
jobs stay from K7e: a detent is activity for the idle ladder on every
screen but home — there it is the **brightness control** (5 % per
detent, floored at the ladder's dim rung, and deliberately not
activity), and after deep sleep it is the wake input (EXT0 on its A
channel). A screen press once aborted the boot sweep (task #28: the
seam's press observer consumed the tap so no hub opened under a
skip); with the sweep retired 2026-11-14 every press is an ordinary
home/hub touch again. The two clock boards and this
one remain independent products — no cross-board traffic; what they
share is the code.

Hardware map (vendor schematic + demo, extracted 2026-11-07)
-------------------------------------------------------------

The facts below come from the vendor's own sources — the five-page
schematic and the per-function ESP-IDF demo downloaded from the wiki
on 2026-11-07 — not from third-party boardsites, whose claims it
corrects: the vendor demo drives the panel with
``esp_lcd_sh8601`` (not the third-party "ST77916"), and the
schematic's LEDA/LEDK nets show a **backlit LCD** (the product
name's "LCD" is literal, not an AMOLED).

The overview below seals the app MCU's side into one box per driven
thing and keeps the board's two silences visible (dashed grey): the
untouched peripherals and the U4WDH peer across the idle inter-MCU
UART. The four sketches after it open the display chain, the
hand-driven inputs, the untouched block with the peer, and the green
wake-up story that makes the knob the family's only *real* deep
sleeper: no RTC IC, so the alarm is an internal-RTC TIMER (never an
early ring), and the wake inputs are the dial's A channel (EXT0), the
touch INT (EXT1) and that TIMER.

.. graphviz:: ../_graphs/knob-wiring.dot
   :alt: Knob board component wiring overview
   :align: center
   :caption: The overview — the app MCU drives five things (SH8601 over QSPI x4, the GPIO 47 backlight PWM, the I²C touch, the EC1 dial, the battery ADC) and ignores two (the dashed peripherals, the dashed U4WDH peer); the green callout is the wake story the last sketch opens.

.. graphviz:: ../_graphs/knob-wiring-display.dot
   :alt: Knob display chain
   :align: center
   :caption: The display chain — QSPI x4 CLK 13, CS 14, D0–D3 15–18, RST 21 to the SH8601 (RGB565; LEDA on 3V3, TE not wired — a backlit LCD, the product name's "LCD" is literal); backlight PWM GPIO 47 drives an AO3400A PMOS, and LEDC is the only dimmer — no 0x51 register like the S3's CO5300.

.. graphviz:: ../_graphs/knob-wiring-io.dot
   :alt: Knob inputs — touch, dial, battery
   :align: center
   :caption: The inputs — CST816S at 0x15 on I²C SDA GPIO11 / SCL GPIO12 with 5K1 pull-ups (INT 9, RST 10; polled, the INT otherwise unused); the EC1 dial A GPIO 8 / B GPIO 7 with 10K pull-ups, one channel per detent, decoded by a 3 ms software poll; the battery "voltage" on GPIO 1 (ADC1_CH0) behind a 10K/10K divider with the top rail drawn 5V — placeholder calibration until the multimeter verdict lands; the DRV2605 haptics (LRA) sits on the bus, dashed — never driven.

.. graphviz:: ../_graphs/knob-wiring-unused.dot
   :alt: Knob untouched peripherals and the U4WDH peer
   :align: center
   :caption: Wired but untouched — PDM mic (CLK 45, DATA 46), SDMMC (CMD 3, CLK 4, D0–D3 5/6/42/2 — pins, no socket), I²S to the CH445P mux → PCM5100APWR DAC, native USB D+/D− 19/20 (the 303a:1001 flash path); and the ESP32-U4WDH across the dashed inter-MCU UART (TX 38 / RX 48 ↔ 23 / 18) — a classic-ESP32 peer at dual 240 MHz with 4 MB flash, its own EC2 (A 19 / B 22), its own I²S (25/26/27) and the CH340-family bridge U10 (1a86:7523) on its UART0, the working USB side in one plug orientation. The two *factory* demos talk over that UART; this firmware never does.

.. graphviz:: ../_graphs/knob-wiring-wake.dot
   :alt: Knob deep-sleep wake inputs
   :align: center
   :caption: The wake inputs — real ``esp_deep_sleep_start`` armed with the internal RTC TIMER (no RTC IC, so the alarm is a timer and can never ring early); EXT0 dial-A GPIO 8 active-LOW is the primary, EXT1 the CST816S INT GPIO 9 ANY_LOW (``KNOB_TOUCH_WAKE``), TIMER the next fixed alarm rule (``KNOB_ALARM_TIMER_WAKE``); a detent that pulses only B (GPIO 7) does not wake — the next detent does.

The U4WDH box is deliberately drawn as a **peer, not a peripheral**:
it runs factory firmware, owns its encoder and the CH340-family USB
bridge, and the only wire between the chips — the inter-MCU UART —
is dashed because this firmware never uses it (the two *factory*
demos talk over it; we do not).

The ESP32-S3 side (vendor-demo-confirmed, schematic cross-checked):

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - Function
     - Pins (S3)
   * - LCD, QSPI x4, 360×360, ``esp_lcd_sh8601``
     - CLK 13, CS 14, D0–3 15–18, RST 21; **TE not wired**;
       backlight GPIO 47 (PWM through an AO3400A PMOS, LEDA tied
       to 3V3 — a backlit LCD panel)
   * - Touch, CST816S at I2C address 0x15
     - SDA 11, SCL 12 (5K1 pull-ups), INT 9 (the vendor demo
       polls, INT unused), RST 10
   * - Encoder EC1 — this chip's dial
     - A = GPIO 8, B = GPIO 7, 10K pull-ups; the vendor's encoder
       library is a software-polling debouncer (no PCNT, no IRQ)
   * - Battery "voltage" ADC
     - GPIO 1 (ADC1_CH0) behind a 10K/10K divider whose top rail
       the schematic draws as **5V** — the *hypothesis* that the
       factory gauge measures VBUS rather than the cell would
       explain the impossible 4.6 V read on the bench while
       charging; multimeter-verify before ever trusting the gauge.
       The P4 firmware gauge rides this channel behind
       ``KNOB_BATTERY_GAUGE`` (fuse-calibrated ``adc_oneshot``,
       placeholder divider + LiPo pair until the verdict lands; the
       docked-VBUS bolt heuristic ``KNOB_BATTERY_CHARGING_MV`` rides
       the same read)
   * - I2S to the DAC mux
     - BCK 39, DIN 41, WS 40 — the U4WDH has its own set (BCK 25,
       DIN 26, WS 27); a CH445P analog mux picks one source for
       the PCM5100APWR DAC
   * - PDM mic / SD card / native USB
     - mic CLK 45, DATA 46; SDMMC CMD 3, CLK 4, D0–3 5/6/42/2;
       USB D+/D− 19/20 — the ``303a:1001`` path

The ESP32-U4WDH side owns its **encoder EC2** (A = GPIO 19,
B = GPIO 22) and the **CH340-family bridge (U10)** the bench found
at ``1a86:7523``: it hangs on the U4's UART0 with an RTS/DTR-driven
IO0/EN auto-boot circuit — which is why that orientation flashes the
U4 like any normal ESP32 board.

A dedicated **inter-MCU UART** exists on the board: S3 TX GPIO 38 /
RX GPIO 48 ↔ U4 GPIO 23 / 18. None of the vendor's per-function
demos uses it — it is how the two *factory* demo firmwares talk to
each other. The firmware ignores it: the knob never talks to the U4,
and its Wi-Fi join exists only for SNTP.

The remaining peripherals, for the record: haptics are a **DRV2605**
on the same I2C bus (SDA 11 / SCL 12) with EN/TRIG lines and an LRA
motor; audio is the **PCM5100APWR** DAC on its own SGM2036 rail.
The DAC source-mux select net is drawn as "I2S SWITCH IN" on the S3
sheet with an impossible "GPIO100" label (the S3 has no GPIO100) —
a drawing quirk, and irrelevant to the MVP (no audio).

USB enumeration behaviour (bench-observed, 2026-11-07)
-------------------------------------------------------

Two MCUs means two possible ways onto the USB bus, and the host sees
a *different* device for each — which makes this board's
identify-before-you-flash procedure stricter, not looser, than the
clock boards' (:doc:`identifying the boards <index>`).

What the bench captured, in order:

1. **The U4WDH side works.** It claims a ``/dev/ttyUSB*`` node via
   the **CH341** kernel driver (``1a86:7523`` "USB Serial") — a
   CH340-family UART converter sitting on the U4WDH's UART, with
   DTR/RTS wired to its boot circuit. ``esptool`` speaks to the
   **U4WDH itself** through it: ``ESP32-U4WDH (revision v3.1)``,
   MAC ``<mac-u4wdh>``, 4 MB flash (stub upload + run clean,
   ``flash_id`` reads the chip) — a normal ESP32-class flashing
   target, factory firmware still present.
2. **The S3R8 side has not enumerated — but the board was unpowered
   for most of those tests.** With the plug rotated 180° in the same
   socket the board went **completely off the bus** — no ``ttyUSB*``,
   no new ``ttyACM*``, no kernel claim. A later bench step explains
   it: the board carries a **built-in battery** (it runs with no
   cable attached at all, so the USB-C doubles as the charge path)
   and the lone exposed control is the back-side power button — most
   of the "silent orientation" captures predate any deliberate
   power-on. Opening the case made the board boot by itself and the
   **factory demo came up on the panel**, then died mid-boot — the
   classic deep-discharge LiPo brown-out. That also corrects an
   earlier note here: the app side is *not* shipped unflashed (the
   demo is pre-installed), and a black panel on this board usually
   means "powered off / flat battery", not "dead".
3. One **transient** in the first session: the U4 node dropped ~25 s
   after enumeration (a clean USB disconnect, no driver error) and
   came back healthy on re-plug; not reproduced since — and the
   battery now offers a mundane suspect (rail sag) for it too.
   Charge before trusting any enumeration result.
4. **The S3 side enumerates — once the board is genuinely stable on
   its rail.** With the power stable (see the screw note below),
   the S3 orientation claimed ``303a:1001`` "USB JTAG/serial debug
   unit" as a ``ttyACM*`` node, and ``esptool`` confirmed the
   silicon: ``ESP32-S3 (QFN56) revision v0.2``, Wi-Fi/BLE,
   **8 MB embedded PSRAM** (the "R8" of ESP32-S3R8),
   ``USB mode: USB-Serial/JTAG``, stub upload + run clean. No BOOT
   press was needed — the whole "S3 side never enumerates" saga was
   flat battery plus a mechanical rail fault.

The mechanical fault is worth its own sentence: the board browned
out on boot (display came up, died mid-boot) while the CNC case
screws were tight, and booted **stably** the moment they were
loosened — overtightened case screws flexing the PCB or pinching a
ground path. Pressing the *display* against the housing also
resets the device outright. And the factory settings menu's
battery gauge read 4.6 V — impossible for a single-cell LiPo
(4.2 V max), so the gauge lies (or the cell is abused) and a
load-sagging supply fits every flaky observation (mid-dump
brown-outs, pressure reboots, "flat" behaviour at full gauge).
If a knob board misbehaves erratically: screw torque, panel
pressure, then the battery connection — before suspecting silicon.

The factory demo's settings menu also carries **"reboot to MSC"**
(the S3 re-enumerates as a USB mass-storage disk — Waveshare's
drag-and-drop ``.bin`` flashing path, and therefore the *restore*
path for the stock firmware) and **"factory reset"**. Keep that
door closed while developing, and remember MSC exists when the
serial path is fighting a weak rail.

The bring-up checklist this whole saga produced, in order:
**charge** (a flat LiPo browns out boot *and* enumeration),
**power on** with the back power button, **known-good data
cable** (a charge-only one reproduces the same total silence),
**case-screw torque** (above) — and only then the manual
download-mode sequence, hold the back-side BOOT while
power-cycling. On this unit the BOOT press was never needed;
both chips answered esptool on the first attempt with stable
power.

One trap this surfaced, in the other direction: a ``1a86:55d3``
"USB Single Serial" (CH9102) ``ttyACM`` node appeared alongside and
*looked* like a second bridge from the knob. It was the P4 4C's own
USB-UART bridge. **udev's name identifies a bridge chip, never a
board** — several Waveshare boards carry WCH bridges, so ``ls /dev/serial/by-id``
cannot tell them apart. The identifying check stays
``esptool.py --port <port> chip_id`` (``ESP32S3`` for this board's
U4 path — and the same single check that already catches a stale
port guess on the clock boards).

Firmware (``knob/`` sibling project)
-------------------------------------

The shipping firmware is the **moon display**: LVGL 9.6,
one full-panel disk baked by the clock's portable moon core under the
family's **shared home screen** — split-flap HH:MM pair cells, colon,
alarm row and seconds arc from ``screens.c``'s
``create_user_widget_clock``, laid out at 360 px through ``UI_PX()``
with the generated ``fonts_knob/`` tier (74/37/25 px = the S3's
96/48/32 baselines scaled by 360/466 — 236 glyphs at the smallest
bill, 44 KiB, and the one face that is not a pure baseline re-bake:
the regular icon set inherits the P4's clock glyph it never draws,
:ref:`the type sketch <type-family-ui>`, :ref:`the glyph ladder
<glyph-ladder-perf>`). The earlier K7 caption row
("Nov 8 17:29" through ``KNOB_TZ``) still carries the display in the
legacy own-screen build (``KNOB_UI_ENABLE=n``) — with P2 shipped the
shared TZ screen owns the zone on the family app and ``KNOB_TZ``
survives only in that legacy caption. Around that, the sky-event
tint with a 1 Hz day tick (every boot and wake shows the static
current phase — the cold-boot synodic sweep retired 2026-11-14,
making the clock's wake policy the whole display policy),
that re-bakes once on a UTC-date or event-signature change, SNTP
time over the shared ``components/wifi`` join with the clock's NVS
last-known-instant snapshot (K7d on the shared ``clock_sntp`` path —
the board has no RTC), and an idle ladder —
backlight dim, then deep sleep woken by a dial detent **or a screen
tap** (K7e, ``knob_power``; since P2 the two rungs read the
``settings.txt`` store, so the sleep screens' rollers are the real
editors). The settings hub and its screens are operable by dial +
tap alone (:ref:`dial-semantics-knob <dial-semantics-knob>`). How the pieces run —
frame path, opening act, ladder, dial — is
:doc:`the knob display <../implementation/knob-display>`.
Layout and commands:

* ESP-IDF project at ``knob/`` — ``main/`` plus ``components/
  {board, imu, knob_board, knob_ui, knob_moon, knob_power,
  knob_time, knob_battery}`` (plus the silent ``esp_codec_dev``
  shim) and the shared ``../components/{clock, ui, wifi,
  moon}`` brought in through ``EXTRA_COMPONENT_DIRS`` (individual
  dirs, never the parent — the shims own the ``board``/``imu``
  names). The seam's
  ``board``/``imu`` shims answer the board-specific symbol surface
  the way the P4's BSP does (panel handle + ``bsp/esp-bsp.h``
  provider + IO-expander-to-NULL; a fixed-upright IMU — there is no
  chip), and the shim's Kconfig carries the ``choice BOARD``
  (``BOARD_KNOB`` default; ``BOARD_S3``/``BOARD_P4`` stay
  selectable-but-unused so the shared Kconfig's ``if BOARD_P4``
  clauses parse — ESP-IDF 5.5 errors on undefined symbols there).
  The shared dirs are added **individually, never the parent
  ``components/``**: IDF 5.5.1 excludes components by name only, so
  the shims must own the names ``board``/``imu``. ``knob_moon`` is
  glue only — the shared ``components/moon`` compiles the core TUs
  once and picks ``generated/knob`` through a ``CONFIG_BOARD_KNOB``
  branch, so ``check_gate.py --target knob`` still describes exactly
  the physics the panel shows. (The seam as a whole — three projects,
  two CMake passes, literal requirements, silent shims — is drawn in
  :doc:`the build system <../reference/build-system>`.)
* Moon assets: the pipeline's third target —
  ``--target s3 p4 knob`` bakes albedo 864×432 + DEM 1024×512 into
  the 2560 KiB ``moon_assets`` partition of ``knob/partitions.csv``
  (manifest existence and blob fit are configure-time
  ``FATAL_ERROR``\\ s in ``knob/CMakeLists.txt`` — *stricter than the
  clock, where a missing blob is only a warning*: the knob has no SD
  socket, so a knob without its pipeline output has no moon at all;
  a hand-deleted ``.bin`` beside a live manifest still only warns,
  per the guard's own comment. The generated
  ``components/moon/generated/knob/moon_consts.h`` is tracked).
* K7c threading mirrors the clock's moon pipeline minus the AMOLED
  machinery: a producer task bakes into a PSRAM double buffer, a swap
  task applies each frame as one atomic ``lv_refr_now`` under the
  LVGL mutex, and the producer only reuses a buffer after the swap's
  ack. The producer is CPU1-pinned and TWDT-unregistered
  (``xTaskCreatePinnedToCoreWithCaps``, the clock's bake-producer
  posture — the re-bake/hourly tasks) because the core's classify
  scan runs ~25 s end to end; everything long on it yields (the scan
  on a 20 ms wall-clock budget, the bake by rows), so
  ``knob/sdkconfig.defaults`` **keeps**
  ``CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1`` on with
  ``CONFIG_ESP_TASK_WDT_PANIC`` — the 2026-11-11 bench lesson: an
  unyielding stretch must die as a backtrace, not a naked reset.
  Its **stack is internal RAM**, not PSRAM (``MALLOC_CAP_INTERNAL``,
  12 KiB on the same ``WithCaps`` call): the day-tick path writes
  spiffs, and a task whose stack lives in PSRAM must never be the
  running task while a flash write has the caches down — the
  condition IDF asserts outright, and the one the 2026-11-16 coredump
  caught on this board
  (:doc:`the knob display <../implementation/knob-display>`).
  Every frame bakes crisp at 360 with the event passed —
  the boot sweep's reduced-size intermediates retired with the sweep
  on 2026-11-14, so the one crisp path is all that is left (the
  frame is byte-identical to what the sweep's final frame always
  was). Measured on the S3R8 (bench, 2026-11-08): crisp bake
  ~0.8 s, 0 TWDT. The current-phase frame carries the sky event
  through the shared tint LUT — a frozen 2025-03-14
  total-lunar-eclipse epoch showed the blood tint on glass, and a
  pinned 23:59:30Z epoch produced exactly one day-tick re-bake after
  midnight.
* K7d time — on the clock's shared time path (P0 family seam): the
  board has **no RTC**, so the display arms only on SNTP truth.
  ``components/clock`` runs its software path (the P4's posture:
  ``CONFIG_CLOCK_USE_PCF85063A=n`` in ``knob/sdkconfig.defaults``,
  boot log ``No external RTC IC``); its ``clock_sntp`` is the
  one-shot lwIP POLL over the shared ``components/wifi`` join, and
  the answer counts only with a plausible epoch (> 2021-01-01) — the
  clock's never-a-phantom-alarm discipline applied to the moon: with
  no fix and no snapshot (dead AP, no uplink, implausible answer, a
  never-synced board) the glass honestly reads ``no time`` and
  **no phase is ever baked from a guess**. Every trusted answer is
  snapshotted to the P4's NVS pair — namespace ``clock``/key
  ``epoch`` (plus an explicit ``clock_nvs_save_epoch`` before every
  deep sleep): a wake whose join + NTP window runs long arms the
  static phase immediately from the snapshot + uptime (log
  ``arming static phase on NVS snapshot``) instead of sitting dark on
  an instant the board already had, and the late fix re-bakes — the
  clock's ``clock_sntp`` snapshot discipline, with the plausibility
  floor still enforced on the read. A wake that finds the shared
  namespace empty but a **legacy** ``knob_time``/``epoch`` snapshot
  (pre-seam firmware) plausible adopts it once, logged as *last
  known, untrusted*, and promotes it into the shared namespace. A
  small boot-arm task owns the sequence (restore the snapshot → wait
  up to 15 s for the bounded join → one fix within
  ``KNOB_TIME_BOOT_SYNC_MS``; late fix = static current phase;
  no fix = arm on the snapshot only if one
  exists, keep polling either way — the moon appears the moment time
  lands — and the late loop re-kicks ``esp_wifi_connect()``: the
  shared join's retries are bounded, so it carries the retired
  ``knob_link``'s background rejoin). While awake, the knob's fixes
  are the boot fix + the late fix; the clocks' hourly on-demand
  refresh (``clock_sntp_needs_sync``) is app-layer and lands with
  the shared screens — a bench board parked awake with
  ``KNOB_SLEEP_ALLOW=n`` runs on its last fix. The server is the
  clock's ``CLOCK_NTP_SERVER`` (default ``pool.ntp.org``; the
  ``KNOB_NTP_SERVER`` key went with ``knob_time``'s SNTP code).
  Bench builds set ``KNOB_TIME_USE_TEST_EPOCH`` to freeze
  ``KNOB_MOON_TEST_EPOCH`` as "now" without SNTP — what keeps the
  K7b/K7c acceptance epochs reproducible. Measured on the bench
  (2026-11-08, the posture the seam preserves): join → fix in
  ≈4–11 s, the synced epoch agreeing with the host UTC clock within
  ~2 s; a late fix still re-baked the static current phase on the
  real now (illum 1.7 % — three hours past that week's new moon),
  0 TWDT.
  With a dead server (``ntp.invalid.example``) on a board with an
  empty NVS the boot logged ``no NTP fix and no snapshot — 'no time'
  stays on glass`` and the glass stayed ``no time`` with zero bake
  lines — the refusal never guesses a phase (with a snapshot stored
  it arms on the last known instant, logged as such).
* K7e power (``knob/components/knob_power``): an idle-ladder supervisor
  (500 ms poll) dims the backlight (LEDC duty ``KNOB_DIM_DUTY``,
  default **64** = 25 %, the P1 dim-parity value matching the clocks'
  light-sleep panel; a registered dim observer drops the shared clock
  widget to ``UI_CLOCK_ALPHA_PCT`` and hides the seconds arc — it is a
  sibling of the clock container, the opacity never cascades to it) after
  ``settings_idle_timeout_sec`` (``settings.txt`` ``idle_timeout_sec``,
  default 60, clamped 60..1800 — since P2 the **store owns both
  rungs**, live from the sleep screens' rollers; the Kconfig
  ``KNOB_DIM_AFTER_S`` (15)/``KNOB_SLEEP_TIMEOUT_S`` (60) are seed
  fallbacks) and enters ``esp_deep_sleep_start`` one
  ``deep_sleep_countdown_sec`` (default 60) after THAT — the clocks'
  additive posture, both measured from the last touch/dial activity;
  any activity un-dims and restarts the ladder.
  Activity = touch contacts (pushed from the indev read) and dial
  detents **on non-home screens only** (the encoder count polled
  there; on home the dial is the brightness control, so a detent
  there deliberately never un-dims — only *brightening* from a dark
  panel counts as wake intent); the **boot arm** and the boot bake
  (the static first frame) **hold** the ladder —
  a presentation is never interrupted and a never-yet-presented board
  is never slept over (the hold starts with ``boot_arm_task``, so a
  slow join + NTP window can no longer dim at 60 s and sleep at 120 s
  over a black panel; only a board that will never present leaves the
  ladder free). Deep sleep first saves the shared ``clock``/``epoch``
  snapshot (``clock_nvs_save_epoch``), then arms **EXT0 on dial-A
  GPIO 8** (active-LOW) plus — under
  ``KNOB_TOUCH_WAKE`` (default ``y``) — **EXT1 ANY_LOW on the CST816S
  INT (GPIO 9)**, so a screen tap wakes like a detent (one touch
  status read before sleeping clears a pending INT pulse — the
  boot-loop guard; a rejected pad logs and stays dial-only). Under
  ``KNOB_ALARM_TIMER_WAKE`` (default ``y``) the sleep **also arms the
  S3 internal RTC timer** for the next fixed alarm rule
  (``KNOB_ALARM_MAX_SLEEP_GAP_S`` 82800 bounds the horizon; a rule
  inside 60 s defers the sleep for the awake esp_timer; no plausible
  epoch arms no phantom timer), and the wake logs a cause the legend
  spells **0**=power-on, **2**=EXT0 dial, **3**=EXT1 tap,
  **4**=TIMER alarm — a cause-4 wake within ``KNOB_ALARM_GRACE_S``
  (300) of the recomputed rule shows the silent alarm visual; an
  early one inside ``KNOB_ALARM_LATE_MAX_S`` (900) holds to the rule
  at 1 Hz (never a ring early); anything further off wakes like a
  dial turn. The S3
  RTCIO verdict the
  plan asked for came back **accepted**, device-verified 2026-11-08
  on the production build (P1-era, ladder still on the Kconfig pair —
  the store-driven rungs re-measure with the P2 bench pass) — the
  boot presentation held the ladder
  whole (no dim/sleep lines while presenting), dim fired at idle 15.1 s,
  ``EXT0 GPIO 8 accepted, deep sleep now`` at idle 60.1 s, and the
  USB-JTAG node vanished from the host moments later: a real deep
  sleep, not a hang. First-detent caveat, as predicted: the
  EC1 pulses ONE channel per detent, so a detent that happens to
  pulse only B (GPIO 7) does **not** wake — the next detent in either
  direction pulses A and does. The back-side power button is a
  hardware rail switch, not a firmware input (nothing to map; verify
  its feel on the bench). ``KNOB_SLEEP_ALLOW=n`` is the sanctioned
  **log-only** mode: the would-be sleep and the EXT0 verdict are
  logged, then the board settles **stable-dark** (backlight 0, ladder
  frozen) — it looks slept but stays reachable over USB-JTAG; a touch
  or brightening the dial wakes it.
* The **battery gauge ships on placeholder calibration only**: the
  schematic's 10K/10K top rail (drawn as 5V) may measure VBUS rather
  than the cell, so the multimeter ritual **with the user present**
  still owes the ``adc_oneshot`` GPIO 1 gauge its honest numbers
  (gauge mV vs multimeter at the divider top, USB-plugged vs
  battery-only; a gauge not agreeing within ~50 mV needs recalibration,
  and the VBUS verdict goes here). The charging bolt is the documented
  consequence of that ambiguity: ``KNOB_BATTERY_CHARGING_MV`` reads a
  cell value far over the 4200 mV ceiling — the docked-USB signature
  (bench ~4522 mV) — as "on the dock" (0 disables the bolt). Until the
  verdict lands the icon reads plausible, not verified. The docked
  fill takes the **theme's moon color** —
  ``theme_colors[active_theme_index][0]``, the seconds-arc ink, read
  live on every minute tick so a theme change propagates (user
  2026-11-14, replacing the reference iOS green); the fill is white
  normally and red at or below ``KNOB_BATTERY_LOW_PCT``.
* Own build dir, lock and config (like the P4): ``idf.py -C knob -B
  ../build/knob build`` from the repo root; ``dependencies.lock.knob``
  and ``knob/sdkconfig`` are gitignored. Flash with ``app-flash``
  (spiffs-preserving) after ``esptool.py --port <port> chip_id``
  identifies the board (it is the bench's only ESP32-S3 while the
  AMOLED is unplugged).
* Wi-Fi credentials come from ``knob/spiffs/wifi_credentials.txt``
  (gitignored — the **clock's filename**: the shared
  ``components/wifi`` reads it; the example file is committed, and a
  full ``idf.py flash`` — not ``app-flash`` — is what carries a
  rename like this into ``storage``). The join is connectivity only
  — no peer protocol; it exists for the SNTP fixes (and the
  settings Wi-Fi screen with the parity phases).
* ``knob/spiffs/alarm.txt`` is the **clock's tracked rules file**,
  same contents — it is what the shared alarm scheduler mounts at
  ``/spiffs/alarm.txt`` (without it boot logged ``ESP_ERR_NOT_FOUND``
  and the "Loaded N alarm rules from SPIFFS" path never ran; boot
  stayed healthy either way). The scheduler is loaded and its rules
  parsed, and since P1 the alarm **presents**: awake, the shared
  esp_timer fires the silent alarm visual; asleep, the TIMER wake
  above shows it (codec-less, so it never rings — *editing* the rule
  on glass comes with P3). The shared clock keeps advancing under the
  alarm visual — HH:MM ticks on the alarm screen too (the missing
  ``clock_minute_enable_interrupt`` gate that froze it there while
  ringing was caught and fixed on 2026-11-15,
  :doc:`the knob display <../implementation/knob-display>`).
* The dial is a **UP/DOWN pulse pair**, not quadrature: each detent
  pulses ONE channel (A = GPIO 8, B = GPIO 7, 10K pull-ups) and the
  pulsed channel IS the direction. The decoder mirrors the vendor's
  ``bidi_switch_knob`` 1:1 (3 ms poll, two-tick debounce, A rise =
  +1, B rise = −1) — every quadrature-shaped attempt discarded half
  the detents (bench, 2026-11-08). The K7 settings editor is its
  first real job; it is idle today.

The U4WDH coprocessor still runs factory firmware; its encoder and
the battery ADC pin are unexposed by the current build.
