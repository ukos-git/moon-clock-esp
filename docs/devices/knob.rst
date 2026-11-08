The Knob board
==============

**Status: standalone moon-display firmware on the bench** — the
``knob/`` sibling project boots its QSPI panel, loads the pre-baked
moon assets into PSRAM and renders the physical moon (K7 milestones:
K7a asset target + gate green, K7b static frame). The earlier
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

Its role in this project is a **fully-working moon display on its
own** (task #24, the K7 milestones). What it does **not** do is
input for the clock: the two boards are independent products that
share only the moon core and the asset pipeline (the dial is
initialized and idle until the K7 settings editor gives it a job).

Hardware map (vendor schematic + demo, extracted 2026-11-07)
-------------------------------------------------------------

The facts below come from the vendor's own sources — the five-page
schematic and the per-function ESP-IDF demo downloaded from the wiki
on 2026-11-07 — not from third-party boardsites, whose claims it
corrects: the vendor demo drives the panel with
``esp_lcd_sh8601`` (not the third-party "ST77916"), and the
schematic's LEDA/LEDK nets show a **backlit LCD** (the product
name's "LCD" is literal, not an AMOLED).

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
       charging; multimeter-verify before ever trusting the gauge
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
each other. The firmware ignores it: the knob is a standalone
device, and its Wi-Fi join exists only for SNTP.

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

The shipping firmware is the **standalone moon display**: LVGL 9.6,
one full-panel disk baked by the clock's portable moon core, a
caption row naming the baked instant, and a Wi-Fi STA join kept for
SNTP (K7d). Layout and commands:

* ESP-IDF project at ``knob/`` — ``main/`` plus ``components/
  {knob_board, knob_ui, knob_link, knob_moon}`` (panel/touch/dial
  shim; the moon-view glue — one ``lv_image`` over a PSRAM RGB565
  square through the partial flush; the Wi-Fi join; and
  ``knob_moon``, which compiles the clock's ``components/moon/core``
  TUs plus the ESP asset backend **verbatim** through relative
  source paths — the same mechanism host_sim uses, so
  ``check_gate.py --target knob`` describes exactly the physics the
  panel shows).
* Moon assets: the pipeline's third target —
  ``--target s3 p4 knob`` bakes albedo 864×432 + DEM 1024×512 into
  the 2560 KiB ``moon_assets`` partition of ``knob/partitions.csv``
  (blob existence and fit are configure-time ``FATAL_ERROR``\\ s in
  ``knob/CMakeLists.txt``; the generated
  ``components/moon/generated/knob/moon_consts.h`` is tracked).
* Own build dir, lock and config (like the P4): ``idf.py -C knob -B
  ../build/knob build`` from the repo root; ``dependencies.lock.knob``
  and ``knob/sdkconfig`` are gitignored. Flash with ``app-flash``
  (spiffs-preserving) after ``esptool.py --port <port> chip_id``
  identifies the board (it is the bench's only ESP32-S3 while the
  AMOLED is unplugged).
* Wi-Fi credentials come from ``knob/spiffs/knob_credentials.txt``
  (gitignored, clock pattern; the example file is committed). The
  join is connectivity only — there is no peer protocol any more.
* The dial is a **UP/DOWN pulse pair**, not quadrature: each detent
  pulses ONE channel (A = GPIO 8, B = GPIO 7, 10K pull-ups) and the
  pulsed channel IS the direction. The decoder mirrors the vendor's
  ``bidi_switch_knob`` 1:1 (3 ms poll, two-tick debounce, A rise =
  +1, B rise = −1) — every quadrature-shaped attempt discarded half
  the detents (bench, 2026-11-08). The K7 settings editor is its
  first real job; it is idle today.

The U4WDH coprocessor still runs factory firmware; its encoder and
the battery ADC pin are unexposed by the current build.
