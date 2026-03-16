#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include "oink_config.h"
#include "oink_runtime.h"

namespace oink {

struct Detection {
    char mac[18];
    char name[48];
    int rssi;
    char method[32];
    unsigned long firstSeen;
    unsigned long lastSeen;
    int count;
    bool isRaven;
    char ravenFW[16];
    double gpsLat;
    double gpsLon;
    float gpsAcc;
    bool hasGPS;
};

struct RecentNotification {
    char shortDesc[32];
    unsigned long timestamp;
};

struct RecentLogEvent {
    char recordType[12];
    char label[24];
    char mac[18];
    char name[48];
    int rssi;
    char method[32];
    int count;
    bool isRaven;
    char ravenFW[16];
    bool hasGPS;
    double gpsLat;
    double gpsLon;
    float gpsAcc;
    unsigned long millisAtEvent;
    time_t epoch;
    char iso8601[40];
    char timeSource[24];
    unsigned long bootCount;
};

struct AppState {
    Detection detections[config::kMaxDetections];
    int detectionCount;
    SemaphoreHandle_t mutex;

    RuntimeConfig runtimeConfig;
    char deviceId[13];
    unsigned long bootCount;

    bool buzzerOn;
    int bleScanDurationSec;
    unsigned long lastBleScan;
    bool triggered;
    bool deviceInRange;
    unsigned long lastDetectionTime;
    unsigned long lastHeartbeat;

    NimBLEScan* bleScan;
    NimBLEServer* bleServer;
    NimBLECharacteristic* txChar;
    volatile bool bleClientConnected;
    volatile uint16_t negotiatedMtu;

    volatile bool serialHostConnected;
    unsigned long lastSerialHeartbeat;
    volatile bool companionChangePending;

    double gpsLat;
    double gpsLon;
    float gpsAcc;
    bool gpsValid;
    unsigned long gpsLastUpdate;

    bool timeSynced;
    bool manualTimeSet;
    char timeSource[24];
    time_t lastEpoch;
    unsigned long lastTimeSyncAttempt;
    unsigned long lastTimeSyncMillis;
    char dayToken[16];

    unsigned long lastSave;
    int lastSaveCount;
    bool spiffsReady;
    bool sdReady;
    bool sdLoggingHealthy;
    bool logWorkerReady;
    unsigned long logEventsQueued;
    unsigned long logEventsWritten;
    unsigned long logEventsDropped;
    char sessionCsvPath[64];
    char sessionJsonlPath[64];
    char dailyCsvPath[64];
    char dailyJsonlPath[64];
    RecentLogEvent recentEvents[16];
    uint8_t recentEventHead;
    uint8_t recentEventCount;

    RecentNotification notifications[4];
    uint8_t notificationHead;
    bool oledReady;
    unsigned long lastOledRefresh;
    unsigned long lastAlertFlash;
    bool alertFlashOn;
};

extern AppState gApp;

} // namespace oink




