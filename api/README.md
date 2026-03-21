# OinkSpy Companion Dashboard

Flask + Socket.IO dashboard for viewing live OinkSpy detections, replaying exported data, and managing serial-connected GPS / sniffer sessions from a desktop browser.

## What It Does

- Streams live detections from the OinkSpy sniffer over serial
- Supports USB GPS dongles or the sniffer's embedded Grove GNSS status feed
- Imports JSON, CSV, and KML exports from the firmware UI
- Exports session or cumulative detections as CSV and KML
- Edits persisted dashboard filters and detection pattern groups
- Looks up manufacturers from the bundled IEEE OUI database

## Quick Start

```bash
cd api
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python flockyou.py
```

On Windows PowerShell:

```powershell
cd api
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python flockyou.py
```

Then open [http://localhost:5000](http://localhost:5000).

## Runtime Files

- `api/data/settings.json`
  Committed sample/default dashboard settings.
- `api/data/settings.local.json`
  Local runtime overrides written by the dashboard by default.
- `api/data/cumulative_detections.json`
  Local cumulative detection store.
- `api/data/cumulative_detections.pkl`
  Legacy format. If present without the JSON file, the app migrates it once on startup.
- `api/exports/`
  Generated CSV and KML downloads.

## Environment Variables

- `SECRET_KEY`
  Recommended for stable sessions. If unset, the app generates an ephemeral development key and warns that sessions reset on restart.
- `OINKSPY_HOST`
  Bind host. Defaults to `0.0.0.0`.
- `OINKSPY_PORT`
  Bind port. Defaults to `5000`.
- `OINKSPY_DEBUG`
  Debug toggle. Defaults to `false`.
- `OINKSPY_SETTINGS_FILE`
  Optional override path for the writable local settings file.
- `OINKSPY_SERIAL_PORTS`
  Comma-separated fallback serial ports when pySerial enumeration is unavailable.

## Main HTTP API

- `GET /`
  Dashboard UI.
- `GET /api/help`
  Quickstart, file locations, and troubleshooting hints.
- `GET /api/status`
  Connection summary, port counts, and recommended next action.
- `GET /api/detections`
  Session detections by default. Supports `filter` and `type=session|cumulative`.
- `POST /api/detections`
  Adds a detection payload.
- `POST /api/clear`
  Clears session detections.
- `GET /api/gps/ports`
  Lists GPS sources, including embedded GNSS when available.
- `POST /api/gps/connect`
- `POST /api/gps/disconnect`
- `GET /api/flock/ports`
- `POST /api/flock/connect`
- `POST /api/flock/disconnect`
- `GET /api/settings`
- `POST /api/settings`
- `GET /api/patterns`
- `POST /api/patterns`
- `GET /api/stats`
- `GET /api/export/csv`
- `GET /api/export/kml`
- `POST /api/import/json`
- `POST /api/import/csv`
- `POST /api/import/kml`
- `POST /api/oui/search`
- `GET /api/oui/all`
- `POST /api/oui/refresh`

## Socket.IO Events

- `new_detection`
- `detection_updated`
- `detections_cleared`
- `gps_update`
- `gps_disconnected`
- `flock_disconnected`
- `heartbeat`
- `heartbeat_ack`
- `serial_data`

## Testing

Install dev dependencies and run:

```bash
pip install -r requirements-dev.txt
python -m pytest tests/test_ux_api.py
```

## Notes

- This companion app is intended for local or LAN-adjacent desktop use, not hardened public deployment.
- The OUI refresh endpoint downloads the latest IEEE list and rewrites `api/oui.txt`.
- Live serial workflows assume the sniffer is already flashed and reachable over USB serial.
