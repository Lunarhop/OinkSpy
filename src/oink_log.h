#pragma once

#include <Arduino.h>

class AsyncResponseStream;

namespace oink::log {

void beginStorage();
void promotePrevSession();
void saveSession();
void pollAutoSave();
void prepareSdLogs();
void appendDetectionEvent(const char* mac,
                          const char* name,
                          int rssi,
                          const char* method,
                          bool isRaven,
                          const char* ravenFw,
                          bool hasGps,
                          double gpsLat,
                          double gpsLon,
                          float gpsAcc,
                          int count);
void appendBookmarkEvent(const char* label);
void writeDetectionsJson(AsyncResponseStream* resp);
void writeDetectionsKml(AsyncResponseStream* resp);
void writeRecentEventsJson(AsyncResponseStream* resp);
bool storageReady();
bool sdReady();
bool readSdTextFile(const char* path, String& out);
const char* sessionCsvPath();
const char* sessionJsonlPath();
const char* dailyCsvPath();
const char* dailyJsonlPath();
size_t queuedEventCount();
unsigned long droppedEventCount();
unsigned long writtenEventCount();

} // namespace oink::log

