#include "oink_time.h"

#include <WiFi.h>
#include <sys/time.h>

#include "oink_state.h"

namespace {

constexpr unsigned long kUnsyncedRetryMs = 30000;
constexpr unsigned long kSyncedRefreshMs = 3600000;

void updateCachedState(bool synced) {
    oink::gApp.timeSynced = synced;
    if (!synced) {
        strlcpy(oink::gApp.dayToken, "unsynced", sizeof(oink::gApp.dayToken));
        return;
    }

    time_t now = time(nullptr);
    if (now <= 1700000000) {
        oink::gApp.timeSynced = false;
        strlcpy(oink::gApp.dayToken, "unsynced", sizeof(oink::gApp.dayToken));
        return;
    }

    oink::gApp.lastEpoch = now;
    oink::gApp.lastTimeSyncMillis = millis();

    struct tm localTime;
    if (localtime_r(&now, &localTime)) {
        snprintf(oink::gApp.dayToken,
                 sizeof(oink::gApp.dayToken),
                 "%04d%02d%02d",
                 localTime.tm_year + 1900,
                 localTime.tm_mon + 1,
                 localTime.tm_mday);
    } else {
        strlcpy(oink::gApp.dayToken, "unsynced", sizeof(oink::gApp.dayToken));
        oink::gApp.timeSynced = false;
    }
}

bool refreshFromSystemClock() {
    time_t now = time(nullptr);
    if (now <= 1700000000) {
        updateCachedState(false);
        return false;
    }
    updateCachedState(true);
    return true;
}

} // namespace

namespace oink::timeutil {

void begin() {
    strlcpy(oink::gApp.dayToken, "unsynced", sizeof(oink::gApp.dayToken));
    oink::gApp.timeSynced = false;
    if (!oink::gApp.runtimeConfig.ntpEnabled) {
        return;
    }

    configTzTime(oink::gApp.runtimeConfig.timezone,
                 oink::gApp.runtimeConfig.ntpServer1,
                 oink::gApp.runtimeConfig.ntpServer2);
    oink::gApp.lastTimeSyncAttempt = millis();
    refreshFromSystemClock();
}

void poll() {
    unsigned long nowMs = millis();
    unsigned long interval = oink::gApp.timeSynced ? kSyncedRefreshMs : kUnsyncedRetryMs;
    if (nowMs - oink::gApp.lastTimeSyncAttempt < interval) {
        return;
    }

    oink::gApp.lastTimeSyncAttempt = nowMs;
    if (!oink::gApp.runtimeConfig.ntpEnabled) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        refreshFromSystemClock();
    } else if (oink::gApp.timeSynced) {
        updateCachedState(true);
    }
}

bool isSynced() {
    return oink::gApp.timeSynced;
}

bool setFromEpoch(time_t epoch, const char* source) {
    if (epoch <= 1700000000) {
        return false;
    }

    timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        return false;
    }

    oink::gApp.manualTimeSet = true;
    oink::gApp.lastTimeSyncAttempt = millis();
    bool ok = refreshFromSystemClock();
    if (ok) {
        printf("[OINK-YOU] Time set from %s: %lu\n", source ? source : "manual", static_cast<unsigned long>(epoch));
    }
    return ok;
}

time_t currentEpoch() {
    if (!oink::gApp.timeSynced) {
        return 0;
    }
    return time(nullptr);
}

const char* timeSourceLabel() {
    return oink::gApp.timeSynced ? (oink::gApp.manualTimeSet ? "manual_wall_clock" : "ntp_wall_clock") : "millis_unsynced";
}

void formatIso8601(char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }
    buffer[0] = '\0';

    time_t now = currentEpoch();
    if (now <= 0) {
        return;
    }

    struct tm localTime;
    if (!localtime_r(&now, &localTime)) {
        return;
    }
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &localTime);
}

void currentDayToken(char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (oink::gApp.timeSynced) {
        updateCachedState(true);
    } else if (!oink::gApp.dayToken[0]) {
        updateCachedState(false);
    }
    strlcpy(buffer, oink::gApp.dayToken, size);
}
} // namespace oink::timeutil



