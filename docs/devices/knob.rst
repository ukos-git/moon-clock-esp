The Knob board (bring-up pending)
=================================

**Status: hardware on the bench, firmware port not started.** This
page records what the board has been observed to do on the host USB
bus, so the port work starts from facts instead of a re-derivation.

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
       dumb bridge chip; it owns the board's working USB side
       through a CH340-family UART converter; the product
       description puts the rotary encoder on this chip (verify
       against the schematic at bring-up)

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

Its intended role in this project is a **companion input device**:
rotary-encoder letter selection for the Wi-Fi keyboard
(:doc:`../keyboard`) and date/time/timer adjustment on the clock —
turning instead of tapping the wheel. The port is tracked as its own
work item.

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

The open question is therefore not a defect but an unmet
expectation: with the plug in the **S3 orientation** the bench saw
*nothing* enumerate — no ``ttyUSB*``, no ``ttyACM*``. Given the
battery discovery, the checklist in order: **charge the board**
(a flat LiPo browns out boot, and nothing enumerates below the
supervisor threshold), **power it on** with the back power button,
then a **known-good data cable** (a charge-only cable reproduces
the same total silence), and only then the manual download-mode
sequence — hold the **back-side BOOT button** while power-cycling
(the knob is a dial, not a button). If the factory demo runs, it
may also enumerate on its own (its "PC secondary screen" feature
implies native-USB CDC use) — check the plain bus first, the
button trick second.

One trap this surfaced, in the other direction: a ``1a86:55d3``
"USB Single Serial" (CH9102) ``ttyACM`` node appeared alongside and
*looked* like a second bridge from the knob. It was the P4 4C's own
USB-UART bridge. **udev's name identifies a bridge chip, never a
board** — several Waveshare boards carry WCH bridges, so ``ls /dev/serial/by-id``
cannot tell them apart. The identifying check stays
``esptool.py --port <port> chip_id`` (``ESP32S3`` for this board's
U4 path — and the same single check that already catches a stale
port guess on the clock boards).
