# ESP32 Moon Phase Clock

## Features

- **WiFi connectivity** with SPIFFS credential storage
- **Automatic NTP time synchronization** with timezone support
- **RTC integration** with PCF85063A
- **LVGL UI** for moon phase display
- **Configurable timezone** (defaults to Central European Time with DST)

## Setup

### Configure Project

```bash
export IDF_TOOLCHAIN=clang
idf.py fullclean
idf.py set-target esp32s3
idf.py reconfigure  # Apply sdkconfig.defaults
```

### WiFi Credentials Setup

#### Create WiFi credentials file

```bash
echo "YourWiFiSSID" > spiffs/wifi_credentials.txt
echo "YourWiFiPassword" >> spiffs/wifi_credentials.txt
```

#### Build and flash

```bash
idf.py build flash
```

The SPIFFS partition will be automatically formatted on first boot if it
doesn't exist.

#### Configure Timezone

Use `idf.py menuconfig` to configure:

Component config → Clock Component Configuration → Default timezone

Default: "CET-1CEST,M3.5.0/2,M10.5.0/3" (Central European Time)

Other Time Zone Options:

- "EST5EDT,M3.2.0,M11.1.0" (Eastern US Time)
- "PST8PDT,M3.2.0,M11.1.0" (Pacific US Time)
- "UTC0" (UTC)
- "JST-9" (Japan Standard Time)

NTP Server: Default is "pool.ntp.org" Sync Interval: Default is 12 hours

## Development

Before pushing, please manually run

```
idf.py save-defconfig
idf.py clang-check --exit-code --run-clang-tidy-options="-header-filter=''" main/main.cpp
```

## Hardware Differences Affecting RTC-Driven Clocks

This clock component was tested on multiple Waveshare Touch-AMOLED boards.
However, **RTC interrupt and wake capabilities differ by board revision** due
to hardware wiring.

### ESP32-S3 Touch AMOLED 1.75

The external RTC (**PCF85063A**) is **not connected to any ESP32 GPIO**. RTC
interrupt (`RTC_INT`) is routed to the **AXP PMIC (EXIO3)** only, and AXP
interrupt (`AXP_IRQ`) is **not wired to the ESP32**.

**Consequences:** ❌ RTC alarms and timers **cannot generate interrupts**, ❌
RTC cannot wake the ESP32 from sleep, ❌ Minute-tick updates cannot be
interrupt-driven.

**Recommended usage:** Use the RTC **only as a time source**. Perform
**minute-aligned software updates** (e.g. via `esp_timer`). Do not rely on RTC
wake or alarm interrupts.

This board is designed for **continuous operation with display power
management**, not RTC-driven wake.

---

GPIO Pins from the
[Data Sheet](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf):

| ESP32-S3 | LCD       | USB   | RTC     | SD   | ES8311     | QMI8658C  | SYS     | GPS        | OUT    |
| -------- | --------- | ----- | ------- | ---- | ---------- | --------- | ------- | ---------- | ------ |
| GPIO01   |           |       |         | MOSI |            |           |         |            |        |
| GPIO02   |           |       |         | SCK  |            |           |         |            |        |
| GPIO03   |           |       |         | MISO |            |           |         |            |        |
| GPIO04   | QSPI SIO0 |       |         |      |            |           |         |            |        |
| GPIO05   | QSPI SI1  |       |         |      |            |           |         |            |        |
| GPIO06   | QSPI SI2  |       |         |      |            |           |         |            |        |
| GPIO07   | QSPI SI3  |       |         |      |            |           |         |            |        |
| GPIO08   |           |       |         |      | I2S DSDIN  |           |         |            |        |
| GPIO09   |           |       |         |      | I2S SCLK   |           |         |            |        |
| GPIO10   |           |       |         |      | I2S ASDOUT |           |         |            |        |
| GPIO11   | TP INT    |       |         |      |            |           |         |            |        |
| GPIO12   | LCD CS    |       |         |      |            |           |         |            |        |
| GPIO13   | LCD TE    |       |         |      |            |           |         |            |        |
| GPIO14   | TP SCL    |       | RTC SCL |      |            | ESP32 SCL |         | ESP32 SCL  |        |
| GPIO15   | TP SDA    |       | RTC SDA |      |            | ESP32 SDA |         | ESP32 SDA  |        |
| GPIO16   |           |       |         |      |            |           |         |            | GPIO16 |
| GPIO17   |           |       |         |      |            |           |         | NC/GPS RXD | GPIO17 |
| GPIO18   |           |       |         |      |            |           |         | NC/GPS TXD | GPIO18 |
| GPIO19   |           | USB N |         |      |            |           |         |            |        |
| GPIO20   |           | USB P |         |      |            |           |         |            |        |
| GPIO21   |           |       |         |      |            | QMI INT2  |         |            |        |
| GPIO38   | QSPI SCL  |       |         |      |            |           |         |            |        |
| GPIO39   | LCD RESET |       |         |      |            |           |         |            |        |
| GPIO40   | TP RESET  |       |         |      |            |           |         |            |        |
| GPIO41   |           |       |         | SDCS |            |           |         |            |        |
| GPIO42   |           |       |         |      | I2S MCLK   |           |         |            |        |
| GPIO43   |           |       |         |      |            |           |         |            | U0TXD  |
| GPIO44   |           |       |         |      |            |           |         |            | U0RXD  |
| GPIO45   |           |       |         |      | I2S LRCK   |           |         |            |        |
| GPIO46   |           |       |         |      | PA CTRL    |           |         |            |        |
| EXIO0    |           |       |         |      |            |           |         |            |        |
| EXIO1    |           |       |         |      |            |           |         |            |        |
| EXIO2    |           |       |         |      |            |           |         |            |        |
| EXIO3    |           |       | RTC INT |      |            |           |         |            |        |
| EXIO4    |           |       |         |      |            |           | SYS OUT |            |        |
| EXIO5    |           |       |         |      |            |           | AXP IRQ |            |        |
| EXIO6    |           |       |         |      |            | QMI INT1  |         |            |        |
| EXIO7    |           |       |         |      |            |           | GPS RST |            |        |

### RP2350 Touch AMOLED 2.41

The RTC interrupt is **directly wired to the MCU**. `RTC_INT` is connected to
**GPIO8**.

**Consequences:** ✅ RTC alarms and timers **can generate interrupts**, ✅
Minute-tick updates can be interrupt-driven, ✅ MCU wake from RTC events is
possible.

**Recommended usage:** Use RTC interrupts for **minute-aligned UI updates**.
Optional low-power sleep strategies using RTC wake.

This board supports a **traditional alarm-clock style architecture**.

---

GPIO Pins from the
[Data Sheet](https://files.waveshare.com/wiki/RP2350-Touch-AMOLED-2.41/RP2350-Touch-AMOLED-2.41.pdf):

| GPIO   | LCD        | UART | RTC     | SD_Card | IMU      | BAT         | Other  |
| ------ | ---------- | ---- | ------- | ------- | -------- | ----------- | ------ |
| GPIO0  |            | TXD  |         |         |          |             | TXD    |
| GPIO1  |            | RXD  |         |         |          |             | RXD    |
| GPIO2  |            |      |         |         |          |             | GPIO2  |
| GPIO3  |            |      |         |         | IMU INT2 |             |        |
| GPIO4  | TP INT     |      |         |         |          |             |        |
| GPIO5  | TP RESET   |      |         |         |          |             |        |
| GPIO6  | TP SDA     |      | RTC SDA |         | IMU SDA  |             | SDA    |
| GPIO7  | TP SCL     |      | RTC SCL |         | IMU SCL  |             | SCL    |
| GPIO8  |            |      | RTC INT |         | IMU INT1 |             |        |
| GPIO9  | OLED CS    |      |         |         |          |             |        |
| GPIO10 | OLED CLK   |      |         |         |          |             |        |
| GPIO11 | OLED SIO0  |      |         |         |          |             |        |
| GPIO12 | OLED SI1   |      |         |         |          |             |        |
| GPIO13 | OLED SI2   |      |         |         |          |             |        |
| GPIO14 | OLED SI3   |      |         |         |          |             |        |
| GPIO15 | OLED RESET |      |         |         |          |             |        |
| GPIO16 | OLED TE    |      |         |         |          |             |        |
| GPIO17 | PWR EN     |      |         |         |          |             |        |
| GPIO18 |            |      |         | SD SCLK |          |             |        |
| GPIO19 |            |      |         | SD MOSI |          |             |        |
| GPIO20 |            |      |         | SD MISO |          |             |        |
| GPIO21 |            |      |         | SDIO D1 |          |             |        |
| GPIO22 |            |      |         | SDIO D2 |          |             |        |
| GPIO23 |            |      |         | SD CS   |          |             |        |
| GPIO24 |            |      |         |         |          | KEY BAT     |        |
| GPIO25 |            |      |         |         |          | BAT Control |        |
| GPIO26 |            |      |         |         |          |             | GPIO26 |
| GPIO27 |            |      |         |         |          |             | GPIO27 |
| GPIO28 |            |      |         |         |          |             | GPIO28 |
| GPIO29 |            |      |         |         |          |             | GPIO29 |
| GPIO30 |            |      |         |         |          |             | GPIO30 |
| GPIO31 |            |      |         |         |          |             | GPIO31 |
| GPIO32 |            |      |         |         |          |             | GPIO32 |
| GPIO33 |            |      |         |         |          |             | GPIO33 |
| GPIO34 |            |      |         |         |          |             | GPIO34 |
| GPIO35 |            |      |         |         |          |             | GPIO35 |
| GPIO36 |            |      |         |         |          |             | GPIO36 |
| GPIO37 |            |      |         |         |          |             | GPIO37 |
| GPIO38 |            |      |         |         |          |             | GPIO38 |
| GPIO39 |            |      |         |         |          |             | GPIO39 |
| GPIO40 |            |      |         |         |          | BAT ADC     |        |
| GPIO41 |            |      |         |         |          |             | GPIO41 |
| GPIO42 |            |      |         |         |          |             | GPIO42 |
| GPIO43 |            |      |         |         |          |             | GPIO43 |
| GPIO44 |            |      |         |         |          |             | GPIO44 |
| GPIO45 |            |      |         |         |          |             | GPIO45 |
| GPIO46 |            |      |         |         |          |             | GPIO46 |
| GPIO47 |            |      |         |         |          |             | GPIO47 |

### Component Behavior

This clock component adapts its behavior based on the target board:

**On boards without RTC interrupts:** Time display updates are scheduled in
software.

**On boards with RTC interrupts:** Time display updates may be
interrupt-driven.

The API remains the same; only the internal scheduling strategy changes.

### Summary

| Board         | RTC → GPIO | RTC Interrupt | RTC Wake | Strategy                 |
| ------------- | ---------- | ------------- | -------- | ------------------------ |
| ESP32-S3 1.75 | ❌         | ❌            | ❌       | Software-timed updates   |
| RP2350 2.41   | ✅ (GPIO8) | ✅            | ✅       | Interrupt-driven updates |
