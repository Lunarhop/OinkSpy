#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class NimBLEAdvertisedDevice;

namespace oink::scan {

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
const char* const* flockMacPrefixes(size_t& count);
const char* const* flockManufacturerPrefixes(size_t& count);
const char* const* soundThinkingPrefixes(size_t& count);
const char* const* deviceNamePatterns(size_t& count);
const uint16_t* bleManufacturerIds(size_t& count);
const char* const* ravenServiceUuids(size_t& count);

} // namespace oink::scan
