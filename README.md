# OinkSpy
Pig-themed BLE surveillance detector for the Seeed Studio XIAO ESP32-S3.

<img src="flock.png" alt="OinkSpy" width="300px">

OinkSpy is a customized `flock-you` fork adapted for the `Seeed Studio XIAO ESP32-S3` and the `XIAO Expansion Board v1.1`. It focuses on BLE-based surveillance detection with an on-device OLED UI, buzzer alerts, a phone-friendly web dashboard, and wardriving-oriented exports.

## Current Status
- Platform: `PlatformIO + Arduino`
- Target board: `seeed_xiao_esp32s3`
- Current firmware shape: single-file prototype in `src/main.cpp`
- Confirmed features: BLE detection, Raven UUID heuristics, OLED status, buzzer alerts, Wi-Fi AP dashboard, phone GPS tagging, CSV/JSON/KML export, SPIFFS session persistence
- In-progress productization: WSL-first development workflow, XIAO Expansion Board integration, SD logging, physical controls, power management, OTA

## Detection Coverage
OinkSpy currently detects likely surveillance hardware using BLE heuristics:

| Method | Description |
| --- | --- |
| MAC prefix matching | Known Flock Safety and related OUI prefixes |
| BLE device name patterns | Identifiers such as `FS Ext Battery`, `Penguin`, `Flock`, `Pigvision` |
| BLE manufacturer ID | `0x09C8` (`XUNTONG`) devices |
| Raven UUID detection | Service UUID fingerprinting for Raven gunshot detectors |
| Firmware estimation | Approximate Raven generation from advertised services |

## Hardware Baseline
Recommended hardware:

- `Seeed Studio XIAO ESP32-S3`
- `Seeed Studio XIAO Expansion Board v1.1`

Current and planned board mapping:

| XIAO Pin | GPIO | Function |
| --- | --- | --- |
| `D1` | `GPIO2` | Expansion board user button |
| `D2` | `GPIO3` | MicroSD chip select |
| `D3` | `GPIO4` | Expansion board buzzer |
| `D4` | `GPIO5` | I2C SDA for OLED and optional RTC |
| `D5` | `GPIO6` | I2C SCL for OLED and optional RTC |
| `D6` | `GPIO43` | UART TX for optional Grove GPS |
| `D7` | `GPIO44` | UART RX for optional Grove GPS |
| `D8` | `GPIO7` | SPI SCK for MicroSD |
| `D9` | `GPIO8` | SPI MISO for MicroSD |
| `D10` | `GPIO9` | SPI MOSI for MicroSD |
| `LED_BUILTIN` | `GPIO21` | Single-color status LED fallback |

Notes:

- OLED is expected on I2C address `0x3C`, with `0x3D` as a runtime fallback probe.
- The Expansion Board docs clearly cover OLED, button, buzzer, SD, Grove, battery charging, and RTC support.
- An onboard RGB LED is not currently treated as available; status feedback should assume buzzer + OLED + single-color LED unless extra hardware is added.

## WSL + VS Code + PlatformIO Workflow
OinkSpy is now documented as a `WSL-first` PlatformIO project.

Recommended workflow:

1. Open the repo in `VS Code Remote - WSL`.
2. Install `PlatformIO Core` inside WSL.
3. Run builds from WSL so `.pio/` stays local to your Linux environment.
4. Use whichever upload path is most reliable on your machine:
   - Recommended: build in WSL, flash and monitor from Windows PlatformIO when the USB device is attached to Windows as `COMx`.
   - Optional: upload and monitor directly from WSL if the device is passed through as `/dev/ttyUSB*` or `/dev/ttyACM*`.

Expected serial-port difference:

- Windows: `COMx`
- WSL/Linux: `/dev/ttyUSB*` or `/dev/ttyACM*`

## Building and Flashing
Requires `PlatformIO`.

### Build in WSL
```bash
git clone https://github.com/Lunarhop/OinkSpy
cd OinkSpy
pio run
```

### Upload Path A: Windows PlatformIO
Use this when the board is attached to Windows and exposed as `COMx`.

```bash
pio run -t upload
pio device monitor
```

### Upload Path B: WSL USB passthrough
Use this only if the device is reliably visible inside WSL.

```bash
pio run -t upload
pio device monitor
```

If upload from WSL is unstable, treat `build in WSL + flash in Windows` as the supported fallback rather than a blocker.

## Milestone 0 Environment Check
Before deeper firmware refactors, verify:

- `pio run` succeeds from your WSL environment
- the `seeed_xiao_esp32s3` board package resolves cleanly
- USB CDC logging works in the upload path you actually use
- at least one reproducible upload/monitor path is documented for your machine

Acceptance criteria:

- firmware builds reproducibly from WSL
- upload path is known and repeatable
- serial logs are readable after flash
- OLED and buzzer still respond on boot

## Dependencies
Current embedded dependencies in `platformio.ini`:

- `olikraus/U8g2@^2.35.19`
- `h2zero/NimBLE-Arduino@1.4.2`
- `ESP32Async/AsyncTCP@^3.3.2`
- `ESP32Async/ESPAsyncWebServer@^3.6.0`
- `bblanchon/ArduinoJson@^7.0.4`

Planned additions for the plug-and-play build:

- `SdFat` for MicroSD logging
- `Update.h` / Arduino OTA for firmware updates
- a small RTC wrapper for optional `PCF8563` support

## GPS Wardriving
The current dashboard can tag detections using your phone’s GPS.

On Android Chrome:

1. Open `chrome://flags`
2. Enable `Insecure origins treated as secure`
3. Add `http://192.168.4.1`
4. Relaunch Chrome
5. Connect to the device AP
6. Tap the GPS icon on the dashboard

Note: iOS Safari blocks geolocation over HTTP.

Future hardware support will also target Grove UART GPS on `D6/D7`.

## Flask Companion Dashboard
A desktop analysis dashboard is available in `api/`.

```bash
cd api
pip install -r requirements.txt
python flockyou.py
```

Then open `http://localhost:5000`.

## Relationship to Flock-You
OinkSpy is a custom fork of the upstream project:

- Upstream: https://github.com/colonelpanichacks/flock-you
- This fork adds pig-themed UI and alerts, XIAO-targeted hardware support, firmware branding changes, and experimental wardriving features

## Disclaimer
This project is intended for:

- security research
- privacy auditing
- educational RF experimentation

Always comply with local laws regarding wireless scanning and radio use.
## SD Config
Place a JSON config file on the SD card at `config/oinkspy.json`.
A starter template is included at `config.oinkspy.example.json`.
Current supported keys: `ap_ssid`, `ap_password`, `buzzer_enabled`, `ble_scan_interval_ms`, `standalone_scan_duration_sec`, `companion_scan_duration_sec`, `save_interval_ms`, `serial_timeout_ms`, `sd_logging_enabled`, `sd_json_enabled`, `sd_csv_enabled`.

