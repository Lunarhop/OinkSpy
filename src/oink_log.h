#pragma once

#include <Arduino.h>

class AsyncResponseStream;

namespace oink {
struct Detection;
}

namespace oink::log {

struct WardriveStatus {
    bool enabled;
    bool active;
    bool canStart;
    bool sdReady;
    bool configValid;
    unsigned long flushIntervalSeconds;
    unsigned long fileRotationMb;
    unsigned long dedupWindowSeconds;
    char logFormat[8];
    char currentPath[96];
    unsigned long currentFileBytes;
    char message[64];
};

void beginStorage();
void promotePrevSession();
void saveSession();
void pollAutoSave();
void prepareSdLogs();
void beginWardriveSession();
void endWardriveSession();
void pollWardrive();
void appendWardriveDetection(const oink::Detection& detection);
WardriveStatus wardriveStatus();
void writeWardriveStatusJson(Print& out);
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
bool writeSdTextFile(const char* path, const String& content);
const char* sessionCsvPath();
const char* sessionJsonlPath();
const char* dailyCsvPath();
const char* dailyJsonlPath();
size_t queuedEventCount();
unsigned long droppedEventCount();
unsigned long writtenEventCount();

} // namespace oink::log

