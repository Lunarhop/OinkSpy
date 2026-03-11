# WSL PlatformIO Workflow

This project is designed to be comfortable to develop from `WSL + VS Code`, even when the `Seeed Studio XIAO ESP32-S3` is physically attached to Windows.

## Recommended Split
- Edit code in `VS Code Remote - WSL`
- Build in WSL with `pio run`
- Keep `.pio/` artifacts inside the WSL filesystem
- Flash and monitor from Windows PlatformIO if USB passthrough is inconsistent

This split is the default supported workflow for OinkSpy.

## Prerequisites
- VS Code with `Remote - WSL`
- `PlatformIO Core` installed inside WSL
- PlatformIO extension available in Windows VS Code if you plan to upload from Windows
- A working USB cable for the XIAO ESP32-S3

## Build in WSL
From the repo root:

```bash
pio run
```

This should resolve:

- `platform = espressif32`
- `board = seeed_xiao_esp32s3`
- `framework = arduino`

## Upload Options
### Option A: Upload from Windows
Use this when the device shows up as `COMx` in Windows and you do not want to fight USB passthrough.

Typical flow:

1. Build in WSL.
2. Open the same repo from Windows VS Code.
3. Use PlatformIO upload and serial monitor against the Windows `COM` port.

Why this is recommended:

- fewer WSL USB edge cases
- simpler serial monitoring
- works well with a Windows-attached ESP32 board

### Option B: Upload directly from WSL
Use this only if the board is reliably attached to WSL with a Linux serial device such as:

- `/dev/ttyUSB0`
- `/dev/ttyACM0`

If you use `usbipd-win` or a similar bridge, confirm the device remains stable across resets during flashing.

## Serial Port Expectations
- Windows serial ports look like `COM3`, `COM4`, or similar
- WSL serial devices usually look like `/dev/ttyUSB0` or `/dev/ttyACM0`

If `pio device monitor` works in one environment but not the other, prefer the working path and document it locally.

## Milestone 0 Validation
Before deeper firmware changes, confirm:

1. `pio run` succeeds from WSL
2. at least one `upload` workflow is reliable
3. serial monitor output is readable after flashing
4. boot output still shows OLED and buzzer initialization on the target board

If WSL upload is flaky, that is not considered a project blocker. The supported fallback is:

- build in WSL
- upload in Windows
- monitor in whichever environment has stable serial access
