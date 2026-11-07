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
     - a USB-serial bridge (WCH), giving the board a console/flash
       path that does not use the S3's native USB

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

1. **First plug-in:** the board claimed a ``/dev/ttyUSB*`` node driven
   by the **CH340-family** kernel driver — the U4WDH side. This is
   the working path: the U4WDH bridge enumerates as a plain USB-UART.
2. **Same socket, plug rotated 180°:** the board went **completely
   off the bus** — no ``ttyUSB*`` node, no new ``ttyACM*`` node, no
   kernel message claiming anything. The ESP32-S3's native-USB side
   did not enumerate in the tested orientations.

Interpretation, **unconfirmed** (re-test during bring-up): USB-C
receptacles on boards of this class often wire only one orientation's
CC/ID pins, so one plug rotation is "the port" and the other is
electrically dead — and a charge-only cable reproduces the same
total-silence signature. Both fit the capture; the discriminator is
trying the other orientation with a known data cable, then a second
cable in the other receptacle if the board has one.

One trap this surfaced, in the other direction: a ``1a86:55d3``
"USB Single Serial" (CH9102) ``ttyACM`` node appeared alongside and
*looked* like a second bridge from the knob. It was the P4 4C's own
USB-UART bridge. **udev's name identifies a bridge chip, never a
board** — several Waveshare boards carry WCH bridges, so ``ls /dev/serial/by-id``
cannot tell them apart. The identifying check stays
``esptool.py --port <port> chip_id`` (``ESP32S3`` for this board's
U4 path — and the same single check that already catches a stale
port guess on the clock boards).
