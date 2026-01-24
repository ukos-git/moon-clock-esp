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
