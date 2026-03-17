# OinkSpy
Pig-themed BLE surveillance detector for the Seeed Studio XIAO ESP32-S3.

<img src="flock.png" alt="OinkSpy" width="300px">

OinkSpy is a customized `flock-you` fork adapted for the `Seeed Studio XIAO ESP32-S3` and the `XIAO Expansion Board v1.1`. It focuses on BLE-based surveillance detection with an on-device OLED UI, buzzer alerts, a phone-friendly web dashboard, and wardriving-oriented exports.

## Current Status
- Platform: `PlatformIO + Arduino`
- Target board: `seeed_xiao_esp32s3`
- Current firmware shape: modular firmware split across `src/main.cpp`, board, scan, logging, GNSS, RTC, settings, and time helpers
- Confirmed features: BLE detection, Raven UUID heuristics, OLED status, buzzer alerts, Wi-Fi AP dashboard, phone GPS tagging, fixed-port Grove GNSS, CSV/JSON/KML export, SPIFFS session persistence, SD card logging, browser-based OTA firmware upload, optional PCF8563 RTC support

## Quick Start (Windows)
The recommended path for OinkSpy is `Windows 10/11 + VS Code + PlatformIO IDE`. It keeps build, upload, and serial monitoring in one place and is usually the simplest option when the XIAO board appears in Device Manager as a Windows `COM` port.

### Prerequisites
- Windows 10/11 (x64)
- [Git for Windows](https://git-scm.com/download/win)
- [Visual Studio Code](https://code.visualstudio.com/download)
- [PlatformIO IDE for VS Code](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- If Windows does not detect the board, start with Seeed's official [XIAO ESP32-S3 getting started guide](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- A USB data cable; charge-only cables will not work for upload or serial monitoring

### Install
1. Install Git for Windows and VS Code.
2. Open VS Code and install `PlatformIO IDE` from the Extensions view.
3. Restart VS Code if the extension prompts you to do so.

### Clone and open
Clone the repo in PowerShell:

```powershell
git clone git@github.com:Lunarhop/OinkSpy.git
cd OinkSpy
```

Then open the folder in VS Code:

1. Select `File -> Open Folder...`
2. Choose the `OinkSpy` folder you just cloned

### First run
The first time you open the project, PlatformIO automatically installs the ESP32 platform, toolchains, and libraries into:

```text
%USERPROFILE%\.platformio
```

Give that first setup a few minutes to finish. To verify the install, either open PlatformIO Home in VS Code or run this in the VS Code terminal with a PowerShell shell:

```powershell
pio --version
```

### Build, upload, and monitor
In the VS Code UI, use the PlatformIO toolbar to `Build`, `Upload`, and `Monitor`. If PlatformIO chooses the wrong device, pick the correct Windows `COM` port before uploading or opening the serial monitor.

The same actions are available from the VS Code terminal in PowerShell:

```powershell
pio run
pio run -t upload
pio device monitor
```

Useful checks:

- `pio run` should complete without board-package errors.
- `pio run -t upload` should flash the board and reboot it.
- `pio device monitor` should show boot logs at `115200`.
- After boot, the device captive portal should be available at `http://192.168.4.1`.

The optional Flask desktop dashboard remains in `api/` and can consume live serial data or imported JSON, CSV, and KML exports. The on-device Wi-Fi AP is for phone control, GPS assist, and status viewing; it is not a separate scan mode.

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
| `D6` | `GPIO43` | GNSS UART RX |
| `D7` | `GPIO44` | GNSS UART TX |
| `D8` | `GPIO7` | SPI SCK for MicroSD |
| `D9` | `GPIO8` | SPI MISO for MicroSD |
| `D10` | `GPIO9` | SPI MOSI for MicroSD |
| `LED_BUILTIN` | `GPIO21` | Single-color status LED fallback |

Notes:

- OLED is expected on I2C address `0x3C`, with `0x3D` as a runtime fallback probe.
- The Expansion Board docs clearly cover OLED, button, buzzer, SD, Grove, battery charging, and RTC support.
- An onboard RGB LED is not currently treated as available; status feedback should assume buzzer + OLED + single-color LED unless extra hardware is added.

## Optional: Windows + WSL Workflow
Use the WSL path when you already develop in Linux, want Linux paths and shells, or the repo tooling around OinkSpy fits better in WSL. Prefer the native Windows workflow above when you want the most reliable USB and serial-device experience.

### Requirements
- [VS Code Remote - WSL](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl)
- WSL 2 with a Linux distribution installed

### Steps
Open the repo inside WSL, then launch VS Code from that WSL shell:

```bash
git clone git@github.com:Lunarhop/OinkSpy.git
cd OinkSpy
code .
```

You can also open a WSL folder directly from VS Code by browsing to a UNC path such as:

```text
\\wsl$\<Distro>\home\<user>\OinkSpy
```

When VS Code prompts for extensions in the WSL environment, install `PlatformIO IDE` there as well. After that, use the same commands from the WSL terminal:

```bash
pio run
pio run -t upload
pio device monitor
```

### USB and serial note
- Prefer the native Windows workflow when your board drivers are Windows-only or the device is most stable as a Windows `COM` port.
- If you want direct serial access from WSL, follow Microsoft's official [Connect USB devices](https://learn.microsoft.com/en-us/windows/wsl/connect-usb) guidance for USB/IP.
- A practical fallback is to keep the source tree in WSL, then build there and upload or monitor from Windows PlatformIO against the Windows `COM` port.

## Optional: Windows CLI-Only (PlatformIO Core)
If you do not want the full IDE workflow, you can work entirely from PowerShell with PlatformIO Core.

The easiest install path is still `VS Code + PlatformIO IDE`, which bundles Core. If you want a standalone install instead, use the official [PlatformIO installer](https://platformio.org/install) or install it with Python and `pipx`:

```powershell
winget install --id Python.Python.3.11 -e
py -m pip install --user pipx
py -m pipx ensurepath
pipx install platformio
pio --version
```

Run the project commands from the repo root:

```powershell
pio run
pio run -t upload
pio device monitor
```

## Other Platforms
For Linux and macOS, use the same basic flow with the official installers for [PlatformIO](https://platformio.org/install) and [VS Code](https://code.visualstudio.com/download): install the tools, clone the repo, open the folder, then run `pio run`, `pio run -t upload`, and `pio device monitor`.

## Milestone 0 Environment Check
Before deeper firmware refactors, verify:

- `pio run` succeeds from your primary development environment
- the `seeed_xiao_esp32s3` board package resolves cleanly
- USB CDC logging works in the upload path you actually use
- at least one reproducible upload/monitor path is documented for your machine

Acceptance criteria:

- firmware builds reproducibly from Windows or WSL
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

- hardware-in-loop validation of OTA uploads on a real XIAO ESP32-S3
- hardware-in-loop validation of `PCF8563` RTC detection and battery-backed time restore

## GPS Wardriving
OinkSpy now supports both browser GPS tagging and the Seeed Studio Grove - GPS (L76K) GNSS module over UART at `9600 8N1`.

On Android Chrome:

1. Open `chrome://flags`
2. Enable `Insecure origins treated as secure`
3. Add `http://192.168.4.1`
4. Relaunch Chrome
5. Connect to the device AP
6. Tap the GPS icon on the dashboard

Note: iOS Safari blocks geolocation over HTTP.

### Grove GNSS hardware
Supported today:

- Board/core: `Seeed Studio XIAO ESP32-S3` with `PlatformIO + Arduino`
- GNSS parser: `TinyGPS++`
- UART transport: `HardwareSerial(1)` with ESP32 pin remap

Connect the Grove - GPS (L76K) module to the fixed Grove UART wiring used by this firmware:

- `D6` = RX
- `D7` = TX

There is no runtime sweep or auto-detect logic. The firmware opens one known UART mapping and continuously parses NMEA from that fixed port.

### GNSS override options
Runtime config key in `config/oinkspy.json`:

- `gnss_enabled`

Compile-time overrides in `platformio.ini`:

```ini
build_flags =
    -DOINK_FEATURE_GNSS=1
    -DOINK_GNSS_BAUD=9600
    -DOINK_GNSS_UART_RX=D6
    -DOINK_GNSS_UART_TX=D7
    -DOINK_GNSS_HW_SERIAL_NUM=1
```

Use the build flags above if your board/core needs a different fixed UART mapping.

### Indicators

- `GPS: seen` appears on the OLED and in serial status once valid NMEA has been parsed.
- `Sats: N` shows the current TinyGPS++ satellite count. Until that field is valid, the OLED shows `Sats: -`.
- `LED_BUILTIN` follows GNSS visibility by default: off before NMEA is seen, on after valid GNSS traffic is parsed.

### Serial and API controls
Serial commands:

- `gnss status`

HTTP status endpoint:

- `GET /api/gnss`

Example serial output:

```text
[OINK-YOU] GNSS fixed UART ready: U1 RX=D6 TX=D7 baud=9600
[OINK-YOU] GPS: seen on U1 RX=D6 TX=D7 @ 9600
[OINK-YOU] Sats: 7
GNSS: port=U1 rx=D6 tx=D7 baud=9600 GPS: seen Sats: 7 Fix: yes HDOP: 0.90 last_ms=412
```

## Flask Desktop Dashboard
A desktop analysis dashboard is available in `api/`.

```powershell
cd api
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python flockyou.py
```

Then open `http://localhost:5000`.

On Linux, macOS, or WSL, activate the virtual environment with `source .venv/bin/activate` instead.

Common first-run flow:

1. Connect the XIAO board over USB and open the dashboard.
2. Choose the OinkSpy serial port and click `Connect`.
3. Optional: connect a USB GPS receiver for location-tagged detections.
4. If hardware is offline, use `Import` to load JSON, CSV, or KML exported by the device.

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

## Configuration

### Precedence

| Surface | Highest precedence | Then | Defaults |
| --- | --- | --- | --- |
| Firmware runtime | SD card `config/oinkspy.json` | compiled defaults in `src/oink_settings.cpp` | built-in defaults |
| Firmware GNSS wiring | `platformio.ini` `build_flags` | source defaults | built-in defaults |
| Companion serial discovery | `OINKSPY_SERIAL_PORTS` | pySerial / fallback glob scan | empty list |
| Companion UI settings | saved `api/data/settings.json` | in-memory defaults | `gps_port=""`, `flock_port=""`, `filter="all"` |
| Flask secret key | `SECRET_KEY` env var | dev fallback in `api/flockyou.py` | `flockyou_dev_key_2024` |

### Firmware SD Config

Place a JSON config file on the SD card at `config/oinkspy.json`.
A starter template is included at `config.oinkspy.example.json`.

| Key | Default | Notes |
| --- | --- | --- |
| `ap_ssid` | `oinkyou` | Soft AP name shown during phone setup |
| `ap_password` | `oinkyou123` | Captive portal password |
| `timezone` | `UTC0` | POSIX TZ string for timestamps |
| `ntp_enabled` | `true` | Enables network time sync |
| `ntp_server_1` | `pool.ntp.org` | Primary NTP server |
| `ntp_server_2` | `time.nist.gov` | Secondary NTP server |
| `buzzer_enabled` | `true` | Boot and alert audio feedback |
| `ble_scan_interval_ms` | `3000` | Time between scans |
| `standalone_scan_duration_sec` | `2` | Scan duration while the local phone-control AP is the only active Wi-Fi role |
| `companion_scan_duration_sec` | `3` | Scan duration when a remote host is attached and OinkSpy can favor flock-drive throughput |
| `save_interval_ms` | `15000` | Session persistence cadence |
| `serial_timeout_ms` | `5000` | Serial command timeout |
| `sd_logging_enabled` | `true` | Master switch for SD logging |
| `sd_json_enabled` | `true` | Write JSONL log files |
| `sd_csv_enabled` | `true` | Write CSV log files |
| `gnss_enabled` | `true` when compiled in | Enables Grove GNSS reader |
| `rtc_enabled` | `true` | Probes and uses an optional `PCF8563` on I2C |
| `ota_enabled` | `true` | Enables firmware upload at `/api/ota` from the captive portal |

## OTA And RTC
The device portal now includes a firmware upload tool in the `TOOLS` tab. Upload the PlatformIO-generated `firmware.bin` for the `seeed_xiao_esp32s3` target and the device will reboot automatically after a successful flash.

Optional `PCF8563` RTC support now shares the same I2C bus as the OLED on `D4` and `D5` at address `0x51`. When present, OinkSpy can:

- restore wall-clock time from RTC on boot when NTP is unavailable
- keep the RTC updated from browser-set or NTP-synced system time
- report RTC presence and validity through the portal status API

Hardware validation still needs a real device:

- confirm an OTA upload from the captive portal completes and reboots cleanly
- confirm RTC time survives power loss with a backup cell installed

## Troubleshooting

### Windows
- Missing board drivers: install the USB or UART driver for your board model, then reconnect the device. Common examples are `CP210x` and `CH34x`; for the XIAO ESP32-S3, start with Seeed's official [getting started guide](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
- `Access is denied` during upload: close other apps that may be using the same `COM` port, including another serial monitor, Arduino IDE, or a stale PlatformIO monitor tab.
- Antivirus or Microsoft Defender blocks tool downloads: allowlist the project folder and the PlatformIO package directory.

```text
%USERPROFILE%\.platformio
```

- Long-path problems: enable Win32 long paths in Windows or move the repo closer to the drive root.
- Permission errors in managed or corporate environments: run VS Code as a standard user, not as Administrator, and make sure your account can write to `%USERPROFILE%\.platformio`.

### Diagnostics
Run these from the repo root in a PowerShell terminal:

```powershell
pio --version
pio system info
pio run -v
```

### Links
- [PlatformIO Docs](https://docs.platformio.org/)
- [PlatformIO Troubleshooting](https://docs.platformio.org/page/faq/index.html)
- [Microsoft: Connect USB devices in WSL](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

### Project-specific runtime issues

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| `pio run` fails inside WSL | PlatformIO not installed in WSL or package cache issue | Reinstall `PlatformIO Core` in WSL and rerun `pio run` from the repo root |
| Board uploads fail from WSL | USB passthrough instability | Build in WSL, then upload from Windows PlatformIO against the `COM` port |
| Dashboard shows no serial ports | Data-only USB cable missing, board busy, or pySerial enumeration failed | Refresh ports, reconnect USB, close other serial monitors, or set `OINKSPY_SERIAL_PORTS=COM3` on Windows or `OINKSPY_SERIAL_PORTS=/dev/ttyUSB0,/dev/ttyACM0` on WSL/Linux |
| Phone GPS badge stays on `HTTP` | Browser blocks geolocation on insecure origins | On Android Chrome, add `http://192.168.4.1` to `chrome://flags` insecure origins; iOS Safari will not allow GPS over HTTP |
| Device portal shows `Storage: SD missing` | SD card absent or not mounted | Reseat the SD card, confirm the chip-select wiring on `D2`, and reboot |
| KML export has no points | Detections were not GPS tagged | Connect browser GPS or Grove GNSS before scanning, then export again |
