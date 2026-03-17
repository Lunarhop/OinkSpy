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

struct WardriveDebugMetrics {
    // Number of wardrive scan cycles started since boot.
    unsigned long scansRun;
    // Number of BLE matches plus passive Wi-Fi AP results seen by the wardrive path.
    unsigned long apsSeen;
    // Number of wardrive CSV rows appended to SD.
    unsigned long apsLogged;
    // Number of candidates suppressed by the dedup window.
    unsigned long apsDroppedDedup;
    // Number of candidates dropped because GPS was mandatory.
    unsigned long apsDroppedGps;
    // Number of candidates dropped because valid time was mandatory.
    unsigned long apsDroppedTime;
    // Number of wardrive file flushes attempted.
    unsigned long flushes;
    // Number of wardrive flush or append failures.
    unsigned long flushErrors;
    // Number of passive Wi-Fi scan starts attempted.
    unsigned long wifiScanAttempts;
    // Number of passive Wi-Fi scans that failed to start or complete.
    unsigned long wifiScanFailures;
    // Number of AP results returned by the most recent completed Wi-Fi scan.
    int wifiScanResultsLast;
    // Number of entries currently held in the debug ring buffer.
    size_t ringbufSize;
    // millis() when the most recent scan window started.
    unsigned long lastScanTs;
    // millis() when the most recent wardrive flush completed.
    unsigned long lastFlushTs;
    // Current runtime/config enable state for Wardrive Mode.
    bool wardriveEnabled;
    // Whether the current Wi-Fi mode includes STA support for passive scans.
    bool staEnabled;
    // Whether an async Wi-Fi scan is currently outstanding.
    bool wifiScanPending;
};

void beginStorage();
void promotePrevSession();
void saveSession();
void pollAutoSave();
void prepareSdLogs();
void beginWardriveSession();
void endWardriveSession();
void pollWardrive();
void noteWardriveScanStart(const char* reason);
void noteWardriveScanCandidate(const char* method, int rssi, int count, bool hasGps, bool timeSynced);
void noteWardriveWifiScanState(const char* decision,
                               const char* reason,
                               int resultCount,
                               const char* wifiMode,
                               bool staEnabled,
                               bool pending);
void appendWardriveDetection(const oink::Detection& detection);
WardriveStatus wardriveStatus();
void writeWardriveStatusJson(Print& out);
void writeWardriveDebugJson(Print& out);
void printWardriveDebug(Stream& out);
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

