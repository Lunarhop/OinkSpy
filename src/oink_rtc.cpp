#include "oink_rtc.h"

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#include "oink_config.h"
#include "oink_state.h"

namespace {

constexpr uint8_t kRtcAddress = 0x51;
constexpr unsigned long kRtcSyncIntervalMs = 300000;

bool gEnabled = false;
bool gPresent = false;
bool gTimeValid = false;
bool gRunning = false;
time_t gLastEpoch = 0;
unsigned long gLastSyncMs = 0;

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

uint8_t fromBcd(uint8_t value) {
    return static_cast<uint8_t>(((value >> 4U) & 0x0FU) * 10U + (value & 0x0FU));
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t epochFromUtcCivil(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    const int64_t days = daysFromCivil(year, month, day);
    const int64_t seconds = days * 86400LL +
                            static_cast<int64_t>(hour) * 3600LL +
                            static_cast<int64_t>(minute) * 60LL +
                            static_cast<int64_t>(second);
    return seconds > 0 ? static_cast<time_t>(seconds) : 0;
}

void formatUtcIso8601(time_t epoch, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (epoch <= 0) {
        return;
    }

    struct tm utcTime = {};
    if (!gmtime_r(&epoch, &utcTime)) {
        return;
    }
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &utcTime);
}

bool probe() {
    Wire.beginTransmission(kRtcAddress);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t startRegister, uint8_t* out, size_t length) {
    if (!out || length == 0) {
        return false;
    }

    Wire.beginTransmission(kRtcAddress);
    Wire.write(startRegister);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    size_t read = Wire.requestFrom(static_cast<int>(kRtcAddress), static_cast<int>(length));
    if (read != length) {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        out[i] = Wire.read();
    }
    return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kRtcAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool writeRegisters(uint8_t startRegister, const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }

    Wire.beginTransmission(kRtcAddress);
    Wire.write(startRegister);
    for (size_t i = 0; i < length; ++i) {
        Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
}

bool decodeTime(const uint8_t* regs, time_t& epoch) {
    if (!regs) {
        return false;
    }

    const bool lowVoltage = (regs[0] & 0x80U) != 0;
    const uint8_t second = fromBcd(regs[0] & 0x7FU);
    const uint8_t minute = fromBcd(regs[1] & 0x7FU);
    const uint8_t hour = fromBcd(regs[2] & 0x3FU);
    const uint8_t day = fromBcd(regs[3] & 0x3FU);
    const uint8_t month = fromBcd(regs[5] & 0x1FU);
    const bool century1900 = (regs[5] & 0x80U) != 0;
    const uint16_t year = static_cast<uint16_t>((century1900 ? 1900 : 2000) + fromBcd(regs[6]));

    if (lowVoltage ||
        second > 59U || minute > 59U || hour > 23U ||
        day == 0U || day > 31U || month == 0U || month > 12U) {
        return false;
    }

    epoch = epochFromUtcCivil(year, month, day, hour, minute, second);
    return epoch > 1700000000;
}

bool readRtcEpoch(time_t& epoch) {
    uint8_t regs[7] = {};
    if (!readRegisters(0x02, regs, sizeof(regs))) {
        return false;
    }
    if (!decodeTime(regs, epoch)) {
        gTimeValid = false;
        return false;
    }

    gTimeValid = true;
    gLastEpoch = epoch;
    return true;
}

bool writeRtcEpoch(time_t epoch) {
    if (epoch <= 1700000000) {
        return false;
    }

    struct tm utcTime = {};
    if (!gmtime_r(&epoch, &utcTime)) {
        return false;
    }

    uint8_t regs[7] = {};
    regs[0] = toBcd(static_cast<uint8_t>(utcTime.tm_sec));
    regs[1] = toBcd(static_cast<uint8_t>(utcTime.tm_min));
    regs[2] = toBcd(static_cast<uint8_t>(utcTime.tm_hour));
    regs[3] = toBcd(static_cast<uint8_t>(utcTime.tm_mday));
    regs[4] = toBcd(static_cast<uint8_t>(utcTime.tm_wday));
    regs[5] = toBcd(static_cast<uint8_t>(utcTime.tm_mon + 1));
    regs[6] = toBcd(static_cast<uint8_t>((utcTime.tm_year + 1900) % 100));

    if (!writeRegisters(0x02, regs, sizeof(regs))) {
        return false;
    }

    gTimeValid = true;
    gLastEpoch = epoch;
    gLastSyncMs = millis();
    return true;
}

} // namespace

namespace oink::rtc {

void begin() {
    gEnabled = oink::gApp.runtimeConfig.rtcEnabled;
    gPresent = false;
    gTimeValid = false;
    gRunning = false;
    gLastEpoch = 0;
    gLastSyncMs = 0;

    if (!gEnabled) {
        return;
    }

    Wire.begin(oink::config::kI2cSdaPin, oink::config::kI2cSclPin);
    gPresent = probe();
    if (!gPresent) {
        printf("[OINK-YOU] RTC probe failed on 0x%02X\n", kRtcAddress);
        return;
    }

    gRunning = writeRegister(0x00, 0x00) && writeRegister(0x01, 0x00);
    if (!gRunning) {
        printf("[OINK-YOU] RTC detected but could not clear control flags\n");
        return;
    }

    time_t epoch = 0;
    if (readRtcEpoch(epoch)) {
        char iso8601[32];
        formatUtcIso8601(epoch, iso8601, sizeof(iso8601));
        printf("[OINK-YOU] RTC ready on 0x%02X (UTC %s)\n", kRtcAddress, iso8601);
    } else {
        printf("[OINK-YOU] RTC ready on 0x%02X but time is not valid yet\n", kRtcAddress);
    }
}

bool enabled() {
    return gEnabled;
}

bool present() {
    return gPresent;
}

bool readEpoch(time_t& epoch) {
    if (!gEnabled || !gPresent || !gRunning) {
        return false;
    }
    return readRtcEpoch(epoch);
}

bool syncFromSystem(bool force) {
    if (!gEnabled || !gPresent || !gRunning) {
        return false;
    }

    if (!force && gLastSyncMs != 0 && millis() - gLastSyncMs < kRtcSyncIntervalMs) {
        return false;
    }

    time_t epoch = time(nullptr);
    if (epoch <= 1700000000) {
        return false;
    }

    if (writeRtcEpoch(epoch)) {
        char iso8601[32];
        formatUtcIso8601(epoch, iso8601, sizeof(iso8601));
        printf("[OINK-YOU] RTC synced from system clock: %s\n", iso8601);
        return true;
    }

    printf("[OINK-YOU] RTC sync failed\n");
    return false;
}

Status getStatus() {
    Status status{};
    status.enabled = gEnabled;
    status.present = gPresent;
    status.timeValid = gTimeValid;
    status.running = gRunning;
    status.epoch = gLastEpoch;
    formatUtcIso8601(gLastEpoch, status.iso8601, sizeof(status.iso8601));
    status.lastSyncAgeMs = gLastSyncMs ? (millis() - gLastSyncMs) : 0UL;
    return status;
}

void writeStatusJson(Print& out) {
    Status status = getStatus();
    out.printf("{\"enabled\":%s,\"present\":%s,\"running\":%s,\"time_valid\":%s,\"epoch\":%lu,\"iso8601\":\"%s\",\"last_sync_age_ms\":%lu}",
               status.enabled ? "true" : "false",
               status.present ? "true" : "false",
               status.running ? "true" : "false",
               status.timeValid ? "true" : "false",
               static_cast<unsigned long>(status.epoch),
               status.iso8601,
               status.lastSyncAgeMs);
}

} // namespace oink::rtc
