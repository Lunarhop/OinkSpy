# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- Added a 60-second quickstart, configuration precedence table, and troubleshooting guide to the repo README.
- Added a setup checklist and inline notices to the companion Flask dashboard.
- Added a field checklist and live storage/time/GNSS status summary to the device captive portal.
- Added `/api/help` and richer `/api/status` responses for the companion dashboard.
- Added pytest coverage for the new UX-focused API validation paths.

### Changed
- Improved companion API validation errors for malformed JSON bodies and missing serial ports.
- Normalized invalid saved filter values back to `all` instead of leaving the dashboard in a confusing state.
- Fixed `api/README.md` to point at the real Flask entry point: `python flockyou.py`.
