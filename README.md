🐖 OinkSpy
Pig-themed BLE Surveillance Detector (Flock-You Fork)
<img src="flock.png" alt="OinkSpy" width="300px">

Standalone BLE surveillance detector with web dashboard, GPS wardriving, OLED alerts, and session persistence.

OinkSpy is a customized firmware fork of Flock-You, adapted for Seeed Studio XIAO hardware and modified with pig-themed UI, sounds, and branding inspired by projects like Porkchop and Piglet.

The goal is to create a playful but powerful BLE surveillance detection platform for wardriving, privacy research, and RF experimentation.

Overview

OinkSpy detects surveillance infrastructure such as:

Flock Safety cameras

SoundThinking / ShotSpotter Raven gunshot detectors

related monitoring hardware broadcasting identifiable BLE signals

The firmware runs a WiFi access point with a live web dashboard, allowing you to monitor detections from your phone while the ESP32 continuously scans BLE advertisements.

Detections can be tagged with GPS coordinates from your phone and exported for mapping or analysis.

Unlike traditional wardriving tools, OinkSpy does not perform WiFi sniffing. BLE scanning runs concurrently while the ESP32 radio maintains the dashboard access point.

Detection Methods

All detection is BLE-based:

Method	Description
MAC prefix matching	Known Flock Safety OUI prefixes
BLE device name patterns	Detects identifiers like FS Ext Battery, Penguin, Flock, Pigvision
BLE manufacturer ID	0x09C8 (XUNTONG) devices
Raven UUID detection	Identifies Raven gunshot detectors by service UUID fingerprint
Firmware estimation	Determines Raven firmware generation based on advertised services
Features
Detection

BLE surveillance device detection

Raven gunshot detector fingerprinting

RSSI signal strength monitoring

multi-heuristic detection engine

Interface

WiFi access point dashboard

mobile-friendly web UI

OLED live status display

audible detection alerts

Wardriving

GPS tagging via browser geolocation

Google Earth KML export

JSON / CSV export

Persistence

session auto-save to SPIFFS

previous session viewer

reboot persistence

Integration

JSON streaming over BLE or serial

Flask dashboard support

companion-mode API

Hardware

Recommended board:

Seeed Studio XIAO ESP32-S3

Example wiring:

Pin	Function
GPIO3	piezo buzzer
I2C	OLED display
GPIO21	optional LED

OLED supported:
SSD1306 128×64 I2C display.

Building & Flashing

Requires PlatformIO.

git clone https://github.com/Lunarhop/OinkSpy
cd OinkSpy

pio run
pio run -t upload
pio device monitor

Dependencies installed automatically:

NimBLE-Arduino

ESP Async WebServer

AsyncTCP

ArduinoJson

SPIFFS

GPS Wardriving

The dashboard can tag detections using your phone’s GPS.

On Android Chrome:

open chrome://flags

enable Insecure origins treated as secure

add:

http://192.168.4.1

relaunch Chrome

connect to the device AP

tap the GPS icon on the dashboard

Note: iOS Safari blocks geolocation over HTTP.

Flask Companion Dashboard

A desktop analysis dashboard is available in the api/ folder.

cd api
pip install -r requirements.txt
python flockyou.py

Then open:

http://localhost:5000

You can import exported detection files or stream live serial data.

Raven Gunshot Detector Detection

Raven devices are detected through BLE service UUID fingerprinting.

These services expose telemetry such as:

device info

GPS

power systems

LTE / network state

upload metrics

diagnostics

The firmware estimates Raven firmware versions based on which services appear in the advertisement.

Relationship to Flock-You

OinkSpy is a custom fork of the Flock-You project.

Upstream project:

https://github.com/colonelpanichacks/flock-you

This fork adds:

pig-themed UI and alerts

custom hardware support

firmware branding changes

experimental features for wardriving

Author

Fork maintained by:

Lunarhop

Original project by:

colonelpanichacks

Disclaimer

This project is intended for:

security research

privacy auditing

educational RF experimentation

Always comply with local laws regarding wireless scanning and radio use.