#pragma once

#include <Arduino.h>
#include <time.h>

class Print;

namespace oink::rtc {

struct Status {
    bool enabled;
    bool present;
    bool timeValid;
    bool running;
    time_t epoch;
    char iso8601[32];
    unsigned long lastSyncAgeMs;
};

void begin();
bool enabled();
bool present();
bool readEpoch(time_t& epoch);
bool syncFromSystem(bool force = false);
Status getStatus();
void writeStatusJson(Print& out);

} // namespace oink::rtc
