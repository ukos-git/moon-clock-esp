Chapter 2 — The S3 board
========================

The primary target is the **Waveshare ESP32-S3-Touch-AMOLED-1.75**: a
1.75-inch round capacitive-touch AMOLED development board. It is the
whole product — the panel, the touch, the motion sensor, and the real-time
clock are all on the same small board, and the clock body (a 3D-printed
stand, see the photograph in Chapter 1) is mostly the board plus a
battery.

The whole product is one small board: the panel, the touch, the motion
sensor, and the real-time clock all share the ESP32-S3's QSPI, I²C, and
I²S buses. The schematic below traces those buses from the SoC to each
component and calls out the one wiring detail that constrains the sleep
machine — the RTC's alarm line never reaches a wake input.

.. graphviz:: ../_graphs/s3-wiring.dot
   :alt: S3 component wiring schematic
   :align: center
   :caption: The S3 board's component wiring — QSPI (CO5300 AMOLED), I²C (touch, IMU, TCA9554 expander, PCF85063A RTC), I²S (ES8311 codec).

The orange path is the wake-up caveat: the RTC's ALM output lands on the
TCA9554 expander's pin 3, which a running CPU polls at 1.3 s — it is
**not** wired to an ESP wake input, so a true ``esp_deep_sleep`` could
never be woken by a timer or alarm on this board. (The product's "deep
sleep" is app-level — the backlight off, the CPU still running — so the
alarm still fires via that poll. Chapter 15.)

SoC and memory
--------------

- **ESP32-S3**: dual-core Xtensa LX7, up to 240 MHz, with an FPU the
  bake uses (Chapter 11).
- **16 MB flash** with an octal PSRAM. The flash is split by
  ``partitions.csv`` into a 2 MB ``factory`` app partition, a 64 KB
  SPIFFS ``storage`` (credentials + alarm rules), and a 2.5 MB
  ``moon_assets`` data partition that holds the equirect albedo and
  the int8 relief DEM (see Chapter 19 for why the DEM's 2 MB size
  forced the 16 MB chip).
- **PSRAM caveat**: the S3's octal PSRAM is **not SPI-DMA-capable**.
  Every DMA transfer that the display stack performs must land in
  internal SRAM. This single fact shapes two things that look
  arbitrary until you know it: the LVGL flush strips are staged
  through internal RAM at 25 rows high (a 32 KiB S3 SPI DMA cap,
  Chapter 13), and the 2 MB relief DEM lives in PSRAM and is read by
  the CPU, never DMA'd.

The panel: CO5300 over QSPI
----------------------------

The **CO5300** controller drives a 466×466 RGB565 (5-6-5, no alpha)
AMOLED panel over **QSPI**. Two properties matter to the rest of this
book:

- **No alpha channel.** The panel is either on with an RGB565 color or
  off; there is no per-pixel transparency. "Fading out" a screen means
  ramping the panel's *hardware brightness register* (register 0x51),
  not writing alpha. Chapter 13 and Chapter 15 both lean on this.
- **A brightness register, not a backlight PWM.** The CO5300 exposes a
  per-panel brightness value. The screen-switch "fade to black" and
  the light-sleep dim (Chapter 15) are both just slow writes to that
  register while the pixel content is held constant — which is also
  exactly why they are cheap and why a lost QSPI write shows up as a
  frozen frame, not a dimmed one (the GPSPI-stall incident, Chapter
  18).

Touch, motion, time
-------------------

- **CST9217** capacitive touch, I²C. The UI's indev layer polls it;
  the settings screens (Chapter 21) and the clock-roll gesture
  (Chapter 15) both key off it.
- **QMI8658C** IMU (accelerometer + gyro), I²C. This drives two
  behaviors that are easy to miss in a photo: the **tilt wake**
  (departure from a settled pose wakes the device, Chapter 15) and
  the **clock roll** (the clock face counter-rotates to stay upright
  as you tilt the board, Chapter 15).
- **PCF85063A** RTC, behind a **TCA9554** IO expander, I²C. The RTC
  keeps the clock alive through light sleep, and because the product's
  "deep sleep" is app-level (the backlight off, the CPU still running)
  the alarm still fires from the dark screen. It fires by *polling*,
  not by a wake line: the PCF's ALM output is wired to the TCA9554
  expander's pin 3, and a running CPU reads that pin at 1.3 s intervals
  (the ``lv_timer`` interrupt handler). That line is **not** an ESP
  wake input, which is why the board could never be woken from a true
  ``esp_deep_sleep`` on a timer or alarm — the same fact the schematic
  above calls out. The alarm is armed through the RTC's hardware alarm
  (``clock_clear_alarm`` / re-arm — Chapter 15 documents the "stale
  armed ALM fires a phantom alarm" bug that required disarming on a
  zero-rule boot).

Power
-----

The board is powered from USB in this build (the bench stand has a
coiled cable in the photos). The sleep machine (Chapter 15) exists
largely because an always-on AMOLED on a desk is a burn-in risk and a
power draw; light sleep keeps the panel on but dims it, deep sleep
turns the panel's backlight off entirely.

The custom shim
---------------

The board is driven through a vendored Waveshare BSP component
(``components/esp32_s3_touch_amoled_1_75``), but all the project's own
board logic sits in a thin shim, ``components/board``. The shim
(``board_display.c``) is deliberately small — it exists to give the
rest of the code one ``board_panel()`` and one
``board_io_expander_init()`` that behave identically on the S3 and the
P4 (the P4 has no IO expander and a different panel handle, so the
shim returns NULL / the P4 handle respectively):

.. code-block:: c
   :caption: components/board/board_display.c (the whole shim)

   #include "board_display.h"

   esp_lcd_panel_handle_t board_panel(void) {
   #if defined(CONFIG_BOARD_P4)
       return bsp_display_get_panel_handle();
   #else
       return bsp_display_get_panel();
   #endif
   }

   esp_io_expander_handle_t board_io_expander_init(void) {
       /* The P4 board has no IO expander (no PCF85063A interrupt line). */
   #if defined(CONFIG_BOARD_P4)
       return NULL;
   #else
       return bsp_io_expander_init();
   #endif
   }

The panel's DMA staging, the 25-row flush-strip limit, and the
``s_panel_mtx`` that serializes brightness writes are described where
they matter: the display pipeline (Chapter 13) and the sleep/dim
machinery (Chapter 15).
