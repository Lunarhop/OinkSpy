#include "oink_gnss.h"

// Repo reconnaissance:
// - Board/core/language: Seeed XIAO ESP32-S3 on PlatformIO with the Arduino-ESP32 core (C++).
// - Existing integrations: OLED UI, serial `printf` logging, and LED_BUILTIN setup already live in the app.
// - GNSS choice: TinyGPS++ over a fixed Grove UART connection at 9600 bps, using HardwareSerial on ESP32.
// - Fixed wiring used here: Grove UART on the XIAO expansion board, default RX=D6 and TX=D7.

#include <TinyGPSPlus.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>

#include "oink_config.h"
#include "oink_scan.h"
#include "oink_state.h"

namespace {

using oink::gnss::Fix;
using oink::gnss::Status;

#if defined(ARDUINO_ARCH_ESP32)
HardwareSerial gGnssSerial(OINK_GNSS_HW_SERIAL_NUM);
#define OINK_GNSS_STREAM gGnssSerial
#else
#define OINK_GNSS_STREAM Serial1
#endif

TinyGPSPlus gGps;
TinyGPSCustom gGpsSatsInViewGn(gGps, "GNGSV", 3);
TinyGPSCustom gGpsSatsInViewGp(gGps, "GPGSV", 3);
TinyGPSCustom gGpsSatsInViewGl(gGps, "GLGSV", 3);
TinyGPSCustom gGpsSatsInViewGa(gGps, "GAGSV", 3);
TinyGPSCustom gGpsSatsInViewGb(gGps, "GBGSV", 3);
TinyGPSCustom gGpsSatsInViewBd(gGps, "BDGSV", 3);
bool gEnabled = false;
bool gSerialBegun = false;
bool gSeen = false;
bool gLoggedSeen = false;
bool gLoggedSatellites = false;
int gSatellites = -1;
unsigned long gLastByteMs = 0;
unsigned long gLastSentenceMs = 0;
unsigned long gLastFixMs = 0;
char gLastSentence[oink::config::kGnssSentenceBufferSize] = "";
char gSentenceBuffer[oink::config::kGnssSentenceBufferSize] = "";
size_t gSentenceLength = 0;

int parseSatelliteValue(TinyGPSCustom& field) {
    if (!field.isValid() || field.age() >= oink::config::kGpsStaleMs) {
        return -1;
    }
    const char* value = field.value();
    if (!value || !value[0]) {
        return -1;
    }
    return atoi(value);
}

const char* pinName(int pin) {
    switch (pin) {
        case D0: return "D0";
        case D1: return "D1";
        case D2: return "D2";
        case D3: return "D3";
        case D4: return "D4";
        case D5: return "D5";
        case D6: return "D6";
        case D7: return "D7";
        case D8: return "D8";
        case D9: return "D9";
        case D10: return "D10";
        default: return "?";
    }
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsEpoch() {
    if (!gGps.date.isValid() || !gGps.time.isValid()) {
        return 0;
    }

    const int64_t days = daysFromCivil(gGps.date.year(), gGps.date.month(), gGps.date.day());
    const int64_t seconds = days * 86400LL +
                            static_cast<int64_t>(gGps.time.hour()) * 3600LL +
                            static_cast<int64_t>(gGps.time.minute()) * 60LL +
                            static_cast<int64_t>(gGps.time.second());
    return seconds > 0 ? static_cast<time_t>(seconds) : 0;
}

void formatGpsTime(char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }
    out[0] = '\0';
    time_t epoch = gpsEpoch();
    if (epoch <= 0) {
        return;
    }
    struct tm tmValue = {};
    if (!gmtime_r(&epoch, &tmValue)) {
        return;
    }
    strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &tmValue);
}

float estimatedAccuracyMeters() {
    if (!gGps.hdop.isValid()) {
        return 0.0f;
    }
    return gGps.hdop.hdop() * oink::config::kGnssHdopToAccuracyMeters;
}

bool fixFresh() {
    return gLastFixMs != 0 && (millis() - gLastFixMs) < oink::config::kGpsStaleMs;
}

void updateIndicators() {
#if OINK_GNSS_LED_PIN >= 0
    if (OINK_GNSS_LED_PIN != oink::config::kStatusLedPin) {
        digitalWrite(OINK_GNSS_LED_PIN, gSeen ? HIGH : LOW);
    }
#endif
}

void rememberSentenceChar(char c) {
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        gSentenceBuffer[gSentenceLength] = '\0';
        if (gSentenceBuffer[0] == '$') {
            strlcpy(gLastSentence, gSentenceBuffer, sizeof(gLastSentence));
        }
        gSentenceLength = 0;
        gSentenceBuffer[0] = '\0';
        return;
    }

    if (gSentenceLength == 0 && c != '$') {
        return;
    }
    if (gSentenceLength + 1 < sizeof(gSentenceBuffer)) {
        gSentenceBuffer[gSentenceLength++] = c;
    } else {
        gSentenceLength = 0;
        gSentenceBuffer[0] = '\0';
    }
}

void onValidSentence() {
    gLastSentenceMs = millis();
    if (!gSeen &&
        (gGps.time.isValid() || gGps.location.isValid() || gGps.hdop.isValid() || gGps.satellites.isValid())) {
        gSeen = true;
        updateIndicators();
        if (!gLoggedSeen) {
            printf("[OINK-YOU] GPS: seen on U%d RX=%s TX=%s @ %lu\n",
                   OINK_GNSS_HW_SERIAL_NUM,
                   pinName(OINK_GNSS_UART_RX),
                   pinName(OINK_GNSS_UART_TX),
                   static_cast<unsigned long>(OINK_GNSS_BAUD));
            gLoggedSeen = true;
        }
    }

    if (gGps.satellites.isValid()) {
        gSatellites = static_cast<int>(gGps.satellites.value());
        if (!gLoggedSatellites) {
            printf("[OINK-YOU] Sats: %d\n", gSatellites);
            gLoggedSatellites = true;
        }
    }

    if (gGps.location.isValid() && gGps.location.isUpdated()) {
        gLastFixMs = millis();
        oink::scan::updateGps(gGps.location.lat(), gGps.location.lng(), estimatedAccuracyMeters());
    }
}

} // namespace

namespace oink::gnss {

bool begin() {
#if !OINK_FEATURE_GNSS
    gEnabled = false;
    return false;
#else
    gEnabled = oink::gApp.runtimeConfig.gnssEnabled;
    if (!gEnabled) {
        return false;
    }

#if OINK_GNSS_LED_PIN >= 0
    if (OINK_GNSS_LED_PIN != oink::config::kStatusLedPin) {
        pinMode(OINK_GNSS_LED_PIN, OUTPUT);
        digitalWrite(OINK_GNSS_LED_PIN, LOW);
    }
#endif

#if defined(ARDUINO_ARCH_RP2040)
    OINK_GNSS_STREAM.setRX(OINK_GNSS_UART_RX);
    OINK_GNSS_STREAM.setTX(OINK_GNSS_UART_TX);
    OINK_GNSS_STREAM.begin(OINK_GNSS_BAUD);
#elif defined(ARDUINO_ARCH_ESP32)
    OINK_GNSS_STREAM.begin(OINK_GNSS_BAUD, SERIAL_8N1, OINK_GNSS_UART_RX, OINK_GNSS_UART_TX);
#else
    OINK_GNSS_STREAM.begin(OINK_GNSS_BAUD);
#endif

    gSerialBegun = true;
    gSeen = false;
    gLoggedSeen = false;
    gLoggedSatellites = false;
    gSatellites = -1;
    gLastByteMs = 0;
    gLastSentenceMs = 0;
    gLastFixMs = 0;
    gLastSentence[0] = '\0';
    gSentenceBuffer[0] = '\0';
    gSentenceLength = 0;
    gGps = TinyGPSPlus();

    printf("[OINK-YOU] GNSS fixed UART ready: U%d RX=%s TX=%s baud=%lu\n",
           OINK_GNSS_HW_SERIAL_NUM,
           pinName(OINK_GNSS_UART_RX),
           pinName(OINK_GNSS_UART_TX),
           static_cast<unsigned long>(OINK_GNSS_BAUD));
    return true;
#endif
}

void update() {
#if !OINK_FEATURE_GNSS
    return;
#else
    if (!gEnabled || !gSerialBegun) {
        return;
    }

    int budget = 96;
    while (budget-- > 0 && OINK_GNSS_STREAM.available() > 0) {
        int value = OINK_GNSS_STREAM.read();
        if (value < 0) {
            break;
        }

        char c = static_cast<char>(value);
        gLastByteMs = millis();
        rememberSentenceChar(c);
        if (gGps.encode(c)) {
            onValidSentence();
        }
    }

    updateIndicators();
#endif
}

bool gpsSeen() {
    return gSeen;
}

int satellitesSeen() {
    int combined = parseSatelliteValue(gGpsSatsInViewGn);
    if (combined >= 0) {
        return combined;
    }

    int total = 0;
    bool any = false;
    TinyGPSCustom* const groups[] = {
        &gGpsSatsInViewGp,
        &gGpsSatsInViewGl,
        &gGpsSatsInViewGa,
        &gGpsSatsInViewGb,
        &gGpsSatsInViewBd,
    };
    for (TinyGPSCustom* field : groups) {
        int count = parseSatelliteValue(*field);
        if (count >= 0) {
            total += count;
            any = true;
        }
    }
    if (any) {
        return total;
    }
    return gSeen ? gSatellites : -1;
}

int satellitesUsed() {
    return gGps.satellites.isValid() ? gSatellites : -1;
}

bool hasFix() {
    return fixFresh() && gGps.location.isValid() && satellitesUsed() > 0;
}

Fix getFix() {
    Fix fix{};
    fix.fresh = hasFix();
    fix.hasLocation = gGps.location.isValid();
    fix.hasAltitude = gGps.altitude.isValid();
    fix.hasSpeed = gGps.speed.isValid();
    fix.hasCourse = gGps.course.isValid();
    fix.hasSatellites = gGps.satellites.isValid();
    fix.hasHdop = gGps.hdop.isValid();
    fix.hasTime = gGps.date.isValid() && gGps.time.isValid();
    fix.latitude = gGps.location.isValid() ? gGps.location.lat() : 0.0;
    fix.longitude = gGps.location.isValid() ? gGps.location.lng() : 0.0;
    fix.altitudeMeters = gGps.altitude.isValid() ? gGps.altitude.meters() : 0.0;
    fix.speedMps = gGps.speed.isValid() ? gGps.speed.mps() : 0.0;
    fix.courseDeg = gGps.course.isValid() ? gGps.course.deg() : 0.0;
    fix.satellites = gGps.satellites.isValid() ? gGps.satellites.value() : 0;
    fix.hdop = gGps.hdop.isValid() ? gGps.hdop.hdop() : 0.0f;
    fix.epoch = gpsEpoch();
    formatGpsTime(fix.iso8601, sizeof(fix.iso8601));
    return fix;
}

Status getStatus() {
    Status status{};
    status.enabled = gEnabled;
    status.seen = gSeen;
    status.hasFix = hasFix();
    status.uartIndex = OINK_GNSS_HW_SERIAL_NUM;
    status.rxPin = OINK_GNSS_UART_RX;
    status.txPin = OINK_GNSS_UART_TX;
    status.baud = OINK_GNSS_BAUD;
    status.satellitesSeen = satellitesSeen();
    status.satellitesUsed = satellitesUsed();
    unsigned long now = millis();
    status.lastByteAgeMs = gLastByteMs ? (now - gLastByteMs) : 0;
    status.lastSentenceAgeMs = gLastSentenceMs ? (now - gLastSentenceMs) : 0;
    status.lastFixAgeMs = gLastFixMs ? (now - gLastFixMs) : 0;
    return status;
}

const char* lastSentence() {
    return gLastSentence;
}

void printStatus(Stream& out) {
    Status status = getStatus();
    Fix fix = getFix();
    char satsSeenBuf[8];
    char satsUsedBuf[8];
    char hdopBuf[12];
    if (status.satellitesSeen >= 0) {
        snprintf(satsSeenBuf, sizeof(satsSeenBuf), "%d", status.satellitesSeen);
    } else {
        strlcpy(satsSeenBuf, "-", sizeof(satsSeenBuf));
    }
    if (status.satellitesUsed >= 0) {
        snprintf(satsUsedBuf, sizeof(satsUsedBuf), "%d", status.satellitesUsed);
    } else {
        strlcpy(satsUsedBuf, "-", sizeof(satsUsedBuf));
    }
    if (fix.hasHdop) {
        snprintf(hdopBuf, sizeof(hdopBuf), "%.2f", fix.hdop);
    } else {
        strlcpy(hdopBuf, "-", sizeof(hdopBuf));
    }
    out.printf("GNSS: port=U%d rx=%s tx=%s baud=%lu GPS: %s Sats: %s",
               status.uartIndex,
               pinName(status.rxPin),
               pinName(status.txPin),
               status.baud,
               status.seen ? "seen" : "none",
               satsSeenBuf);
    out.printf("/%s", satsUsedBuf);
    out.printf(" Fix: %s HDOP: %s last_ms=%lu\n",
               status.hasFix ? "yes" : "no",
               hdopBuf,
               status.lastSentenceAgeMs);
}

void writeStatusJson(Print& out) {
    Status status = getStatus();
    Fix fix = getFix();
    out.printf("{\"enabled\":%s,\"gps_seen\":%s,\"satellites\":%d,\"satellites_seen\":%d,\"satellites_used\":%d,\"has_fix\":%s,\"uart\":%d,\"rx_pin\":\"%s\",\"tx_pin\":\"%s\",\"baud\":%lu,\"last_byte_age_ms\":%lu,\"last_sentence_age_ms\":%lu,\"last_fix_age_ms\":%lu,\"hdop\":%.2f}",
               status.enabled ? "true" : "false",
               status.seen ? "true" : "false",
               status.satellitesUsed >= 0 ? status.satellitesUsed : 0,
               status.satellitesSeen,
               status.satellitesUsed,
               status.hasFix ? "true" : "false",
               status.uartIndex,
               pinName(status.rxPin),
               pinName(status.txPin),
               status.baud,
               status.lastByteAgeMs,
               status.lastSentenceAgeMs,
               status.lastFixAgeMs,
               fix.hasHdop ? fix.hdop : 0.0f);
}

bool handleCommand(const char* line, Stream& out) {
    if (!line) {
        return false;
    }

    char buffer[64];
    strlcpy(buffer, line, sizeof(buffer));
    char* token = strtok(buffer, " ");
    if (!token || strcasecmp(token, "gnss") != 0) {
        return false;
    }

    char* command = strtok(nullptr, " ");
    if (!command || strcasecmp(command, "status") == 0) {
        printStatus(out);
        return true;
    }

    out.println("GNSS commands: gnss status");
    return true;
}

} // namespace oink::gnss
