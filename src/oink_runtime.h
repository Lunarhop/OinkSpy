#pragma once

#include <Arduino.h>

namespace oink {

struct WardriveConfig {
    bool enabled;
    uint16_t flushIntervalSeconds;
    uint16_t fileRotationMb;
    uint16_t dedupWindowSeconds;
    uint16_t moveThresholdMeters;
    char logFormat[8];
};

struct RuntimeConfig {
    char apSsid[33];
    char apPassword[65];
    char timezone[48];
    char ntpServer1[64];
    char ntpServer2[64];
    bool buzzerEnabled;
    bool ntpEnabled;
    bool rtcEnabled;
    bool otaEnabled;
    unsigned long bleScanIntervalMs;
    int standaloneBleScanDurationSec;
    int companionBleScanDurationSec;
    unsigned long saveIntervalMs;
    unsigned long serialTimeoutMs;
    bool sdLoggingEnabled;
    bool sdJsonEnabled;
    bool sdCsvEnabled;
    bool gnssEnabled;
    WardriveConfig wardrive;
};

} // namespace oink

