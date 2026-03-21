#pragma once

#include <Arduino.h>
#include <time.h>

namespace oink::gnss {

struct Fix {
    bool fresh;
    bool hasLocation;
    bool hasAltitude;
    bool hasSpeed;
    bool hasCourse;
    bool hasSatellites;
    bool hasHdop;
    bool hasTime;
    double latitude;
    double longitude;
    double altitudeMeters;
    double speedMps;
    double courseDeg;
    uint32_t satellites;
    float hdop;
    time_t epoch;
    char iso8601[32];
};

struct Status {
    bool enabled;
    bool seen;
    bool hasFix;
    int uartIndex;
    int rxPin;
    int txPin;
    unsigned long baud;
    int satellitesSeen;
    int satellitesUsed;
    unsigned long lastByteAgeMs;
    unsigned long lastSentenceAgeMs;
    unsigned long lastFixAgeMs;
};

bool begin();
void update();
bool gpsSeen();
int satellitesSeen();
int satellitesUsed();
bool hasFix();
Fix getFix();
Status getStatus();
const char* lastSentence();
void printStatus(Stream& out);
void writeStatusJson(Print& out);
bool handleCommand(const char* line, Stream& out);

} // namespace oink::gnss
