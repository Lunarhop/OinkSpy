#pragma once

#include <Arduino.h>

namespace oink {

struct RuntimeConfig {
    char apSsid[33];
    char apPassword[65];
    char timezone[48];
    char ntpServer1[64];
    char ntpServer2[64];
    bool buzzerEnabled;
    bool ntpEnabled;
    unsigned long bleScanIntervalMs;
    int standaloneBleScanDurationSec;
    int companionBleScanDurationSec;
    unsigned long saveIntervalMs;
    unsigned long serialTimeoutMs;
    bool sdLoggingEnabled;
    bool sdJsonEnabled;
    bool sdCsvEnabled;
};

} // namespace oink

