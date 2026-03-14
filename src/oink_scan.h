#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class NimBLEAdvertisedDevice;
class Print;

namespace oink::scan {

void loadProfile();
void writeProfileJson(Print& out);
bool updateProfileFromJson(const String& json, String& error);
void setupBle();
void onCompanionChange();
void pollScan();
void setScanningEnabled(bool enabled);
bool isScanningEnabled();
void updateGps(double lat, double lon, float acc);
bool gpsIsFresh();
int countRavenDetections();
int countGpsTaggedDetections();
void resetDetections();

} // namespace oink::scan
