#include "oink_log.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <SdFat.h>
#include <cstring>
#include <math.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "oink_config.h"
#include "oink_scan.h"
#include "oink_settings.h"
#include "oink_state.h"
#include "oink_time.h"

namespace {

SdFs gSd;
SPIClass gSdSpi(FSPI);

constexpr const char* kLogsDir = "logs";
constexpr const char* kSessionsDir = "logs/sessions";
constexpr const char* kDailyDir = "logs/daily";
constexpr const char* kWardriveDir = "logs/wardrive";
constexpr const char* kWigleDir = "logs/wigle";
constexpr const char* kUnsyncedDayToken = "unsynced";
constexpr const char* kFirmwareVersion = "0.2.0-dev";
constexpr const char* kWiglePreamble = "WigleWifi-1.6,appRelease=OinkSpy,model=XIAO ESP32S3,release=0.2.0-dev,device=oinkspy,display=SSD1306,board=seeed_xiao_esp32s3,brand=Seeed";
constexpr const char* kWigleHeader = "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type";
constexpr size_t kLogQueueLength = 48;
constexpr uint32_t kLogTaskStack = 6144;
constexpr size_t kWardriveDedupEntryCount = 96;

enum class LogEventType : uint8_t {
    Detection,
    Bookmark,
};

struct LogEvent {
    LogEventType type;
    char mac[18];
    char name[48];
    int rssi;
    char method[32];
    bool isRaven;
    char ravenFw[16];
    bool hasGps;
    double gpsLat;
    double gpsLon;
    float gpsAcc;
    int count;
    char label[24];
};

QueueHandle_t gLogQueue = nullptr;
TaskHandle_t gLogTaskHandle = nullptr;
FsFile gWardriveFile;
FsFile gWigleFile;
uint16_t gWardriveFileIndex = 0;

struct WardriveDedupEntry {
    char mac[18];
    char name[48];
    char method[32];
    unsigned long lastLoggedMs;
    bool hasGps;
    double gpsLat;
    double gpsLon;
};

WardriveDedupEntry gWardriveDedup[kWardriveDedupEntryCount] = {};
size_t gWardriveDedupNext = 0;

#if OINK_WDRIVE_DEBUG
constexpr size_t kWardriveDebugRingSize = 20;
constexpr unsigned long kWardriveDebugLogIntervalMs = 5000UL;

struct WardriveDebugEvent {
    unsigned long millisAtEvent;
    char eventType[16];
    char decision[16];
    char reason[24];
    char method[32];
    int rssi;
    int count;
    bool hasGps;
    bool timeSynced;
};

oink::log::WardriveDebugMetrics gWardriveDebugMetrics = {};
WardriveDebugEvent gWardriveDebugRing[kWardriveDebugRingSize] = {};
size_t gWardriveDebugHead = 0;
size_t gWardriveDebugCount = 0;
unsigned long gWardriveDebugCycleResults = 0;
unsigned long gWardriveDebugLastCycleResults = 0;
unsigned long gWardriveDebugLastLogMs = 0;
bool gWardriveDebugLastFlushOk = true;
char gWardriveDebugLastFlushReason[24] = "idle";
char gWardriveDebugWifiMode[12] = "unknown";
char gWardriveDebugWifiReason[24] = "idle";
#endif

void rememberRecentEvent(const LogEvent& event) {
    if (!oink::gApp.mutex || xSemaphoreTake(oink::gApp.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    oink::RecentLogEvent& recent = oink::gApp.recentEvents[oink::gApp.recentEventHead];
    memset(&recent, 0, sizeof(recent));

    strlcpy(recent.recordType,
            event.type == LogEventType::Bookmark ? "bookmark" : "detection",
            sizeof(recent.recordType));
    strlcpy(recent.label, event.label, sizeof(recent.label));
    strlcpy(recent.mac, event.mac, sizeof(recent.mac));
    strlcpy(recent.name, event.name, sizeof(recent.name));
    recent.rssi = event.rssi;
    strlcpy(recent.method, event.method, sizeof(recent.method));
    recent.count = event.count;
    recent.isRaven = event.isRaven;
    strlcpy(recent.ravenFW, event.ravenFw, sizeof(recent.ravenFW));
    recent.hasGPS = event.hasGps;
    recent.gpsLat = event.gpsLat;
    recent.gpsLon = event.gpsLon;
    recent.gpsAcc = event.gpsAcc;
    recent.millisAtEvent = millis();
    recent.epoch = oink::timeutil::currentEpoch();
    recent.bootCount = oink::settings::bootCount();
    strlcpy(recent.timeSource, oink::timeutil::timeSourceLabel(), sizeof(recent.timeSource));
    oink::timeutil::formatIso8601(recent.iso8601, sizeof(recent.iso8601));

    oink::gApp.recentEventHead = (oink::gApp.recentEventHead + 1) % 16;
    if (oink::gApp.recentEventCount < 16) {
        ++oink::gApp.recentEventCount;
    }

    xSemaphoreGive(oink::gApp.mutex);
}

bool pathExists(const char* path) {
    return gSd.exists(path);
}

void ensureDir(const char* path) {
    if (!pathExists(path)) {
        gSd.mkdir(path);
    }
}

void ensureParentDirs(const char* path) {
    if (!path || !path[0]) {
        return;
    }

    char buffer[128];
    strlcpy(buffer, path, sizeof(buffer));
    for (char* cursor = buffer + 1; *cursor; ++cursor) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        ensureDir(buffer);
        *cursor = '/';
    }
}

bool appendLine(const char* path, const String& line) {
    FsFile file = gSd.open(path, O_WRONLY | O_CREAT | O_APPEND);
    if (!file) {
        printf("[OINK-YOU] SD open failed: %s\n", path);
        oink::gApp.sdLoggingHealthy = false;
        return false;
    }

    bool ok = file.println(line);
    file.flush();
    file.close();
    if (!ok) {
        printf("[OINK-YOU] SD write failed: %s\n", path);
        oink::gApp.sdLoggingHealthy = false;
        return false;
    }

    oink::gApp.sdLoggingHealthy = true;
    return true;
}

void resetWardriveDedup() {
    memset(gWardriveDedup, 0, sizeof(gWardriveDedup));
    gWardriveDedupNext = 0;
}

double wardriveDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kEarthRadiusMeters = 6371000.0;
    constexpr double kPi = 3.14159265358979323846;
    double dLat = (lat2 - lat1) * (kPi / 180.0);
    double dLon = (lon2 - lon1) * (kPi / 180.0);
    double aLat1 = lat1 * (kPi / 180.0);
    double aLat2 = lat2 * (kPi / 180.0);
    double sinLat = sin(dLat / 2.0);
    double sinLon = sin(dLon / 2.0);
    double a = sinLat * sinLat + cos(aLat1) * cos(aLat2) * sinLon * sinLon;
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return kEarthRadiusMeters * c;
}

void updateWardriveDedupEntry(WardriveDedupEntry& entry, const oink::Detection& detection) {
    entry.lastLoggedMs = millis();
    strlcpy(entry.name, detection.name, sizeof(entry.name));
    entry.hasGps = detection.hasGPS;
    if (detection.hasGPS) {
        entry.gpsLat = detection.gpsLat;
        entry.gpsLon = detection.gpsLon;
    } else {
        entry.gpsLat = 0.0;
        entry.gpsLon = 0.0;
    }
}

bool sameWardriveIdentity(const WardriveDedupEntry& entry, const oink::Detection& detection) {
    if (strcasecmp(entry.mac, detection.mac) != 0 || strcasecmp(entry.method, detection.method) != 0) {
        return false;
    }
    if (strcasecmp(detection.method, "wifi_ap") == 0) {
        return strcasecmp(entry.name, detection.name) == 0;
    }
    return true;
}

bool wardriveCanRun(String& message) {
    message = "";
    if (!oink::gApp.runtimeConfig.wardrive.enabled) {
        message = "Wardrive Mode disabled";
        return false;
    }
    if (!oink::gApp.runtimeConfig.sdLoggingEnabled) {
        message = "Enable SD logging first";
        return false;
    }
    if (!oink::gApp.sdReady) {
        message = "Insert and mount an SD card";
        return false;
    }
    if (strcasecmp(oink::gApp.runtimeConfig.wardrive.logFormat, "csv") != 0) {
        message = "Unsupported wardrive log format";
        return false;
    }
    return true;
}

size_t wardriveRotationBytes() {
    return static_cast<size_t>(oink::gApp.runtimeConfig.wardrive.fileRotationMb) * 1024UL * 1024UL;
}

String csvField(const char* text);
void flushWardriveFile(const char* reason);
void flushWigleFile();
bool readSdLine(FsFile& file, char* buffer, size_t bufferSize);
bool validateWigleCsvFile(FsFile& file, String& error);
void rememberClosedWigleFile(const char* path, size_t fileBytes);

void formatWigleTimestamp(char* buffer, size_t bufferSize, time_t epoch) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    if (epoch <= 0) {
        strlcpy(buffer, "1970-01-01 00:00:00", bufferSize);
        return;
    }
    struct tm utcTime = {};
    gmtime_r(&epoch, &utcTime);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &utcTime);
}

String buildWigleCsvLine(const oink::Detection& detection) {
    time_t epoch = oink::timeutil::currentEpoch();
    char firstSeen[24];
    formatWigleTimestamp(firstSeen, sizeof(firstSeen), epoch);

    String line;
    line.reserve(256);
    line += csvField(detection.mac);
    line += ',';
    line += csvField(detection.name);
    line += ',';
    line += csvField(detection.wifiAuthMode[0] ? detection.wifiAuthMode : "[UNKNOWN][ESS]");
    line += ',';
    line += csvField(firstSeen);
    line += ',';
    line += String(detection.wifiChannel);
    line += ',';
    line += String(detection.wifiFrequencyMhz);
    line += ',';
    line += String(detection.rssi);
    line += ',';
    line += String(detection.gpsLat, 8);
    line += ',';
    line += String(detection.gpsLon, 8);
    line += ',';
    line += "0";
    line += ',';
    line += String(detection.gpsAcc, 1);
    line += ',';
    line += ',';
    line += ',';
    line += "WIFI";
    return line;
}

bool readSdLine(FsFile& file, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return false;
    }
    size_t index = 0;
    bool sawData = false;
    while (file.available()) {
        int value = file.read();
        if (value < 0) {
            break;
        }
        sawData = true;
        char ch = static_cast<char>(value);
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        if (index + 1 < bufferSize) {
            buffer[index++] = ch;
        }
    }
    buffer[index] = '\0';
    return sawData || index > 0;
}

bool validateWigleCsvFile(FsFile& file, String& error) {
    error = "";
    if (!file) {
        error = "WiGLE CSV file not found";
        return false;
    }

    char line[256];
    if (!readSdLine(file, line, sizeof(line))) {
        error = "WiGLE CSV file is empty";
        return false;
    }
    if (strcmp(line, kWiglePreamble) != 0) {
        error = "WiGLE CSV header is invalid";
        return false;
    }
    if (!readSdLine(file, line, sizeof(line))) {
        error = "WiGLE CSV header row is missing";
        return false;
    }
    if (strcmp(line, kWigleHeader) != 0) {
        error = "WiGLE CSV columns do not match WiGLE 1.6";
        return false;
    }
    if (!readSdLine(file, line, sizeof(line))) {
        error = "WiGLE CSV has no data rows yet";
        return false;
    }
    if (!line[0]) {
        error = "WiGLE CSV has no data rows yet";
        return false;
    }
    return true;
}

void rememberClosedWigleFile(const char* path, size_t fileBytes) {
    if (!path || !path[0]) {
        return;
    }
    strlcpy(oink::gApp.wigleLastClosedPath, path, sizeof(oink::gApp.wigleLastClosedPath));
    oink::gApp.wigleLastClosedFileBytes = fileBytes;
}

String buildWardriveCsvLine(const oink::Detection& detection) {
    char isoBuffer[40];
    oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));
    time_t epoch = oink::timeutil::currentEpoch();

    String line;
    line.reserve(320);
    line += String(millis());
    line += ',';
    line += String(static_cast<unsigned long>(epoch));
    line += ',';
    line += csvField(isoBuffer);
    line += ',';
    line += csvField(oink::timeutil::timeSourceLabel());
    line += ',';
    line += oink::settings::deviceId();
    line += ',';
    line += String(oink::settings::bootCount());
    line += ',';
    line += csvField(detection.mac);
    line += ',';
    line += csvField(detection.name);
    line += ',';
    line += csvField(detection.method);
    line += ',';
    line += String(detection.wifiChannel);
    line += ',';
    line += String(detection.wifiFrequencyMhz);
    line += ',';
    line += csvField(detection.wifiAuthMode);
    line += ',';
    line += String(detection.rssi);
    line += ',';
    line += String(detection.count);
    line += ',';
    line += (detection.isRaven ? "true" : "false");
    line += ',';
    line += csvField(detection.ravenFW);
    if (detection.hasGPS) {
        line += ',';
        line += String(detection.gpsLat, 8);
        line += ',';
        line += String(detection.gpsLon, 8);
        line += ',';
        line += String(detection.gpsAcc, 1);
    } else {
        line += ",,,";
    }
    return line;
}

bool openWardriveFile() {
    String error;
    if (!wardriveCanRun(error)) {
        return false;
    }

    ensureDir(kLogsDir);
    ensureDir(kWardriveDir);
    ensureDir(kWigleDir);

    char dayToken[16];
    oink::timeutil::currentDayToken(dayToken, sizeof(dayToken));
    if (!dayToken[0]) {
        strlcpy(dayToken, kUnsyncedDayToken, sizeof(dayToken));
    }

    snprintf(oink::gApp.wardriveCurrentPath,
             sizeof(oink::gApp.wardriveCurrentPath),
             "%s/wardrive-%s-boot%06lu-%03u.csv",
             kWardriveDir,
             dayToken,
             oink::settings::bootCount(),
             static_cast<unsigned>(gWardriveFileIndex));

    gWardriveFile = gSd.open(oink::gApp.wardriveCurrentPath, O_WRONLY | O_CREAT | O_APPEND);
    if (!gWardriveFile) {
        oink::gApp.wardriveActive = false;
        return false;
    }

    oink::gApp.wardriveCurrentFileBytes = static_cast<size_t>(gWardriveFile.fileSize());
    if (oink::gApp.wardriveCurrentFileBytes == 0) {
        gWardriveFile.println("# oinkspy_wardrive=1");
        gWardriveFile.println("# device_id=" + String(oink::settings::deviceId()));
        gWardriveFile.println("# boot_count=" + String(oink::settings::bootCount()));
        gWardriveFile.println("millis,epoch,iso8601,time_source,device_id,boot_count,mac,name,method,wifi_channel,wifi_frequency_mhz,wifi_auth_mode,rssi,count,is_raven,raven_fw,gps_lat,gps_lon,gps_acc");
        flushWardriveFile("header");
        oink::gApp.wardriveCurrentFileBytes = static_cast<size_t>(gWardriveFile.fileSize());
    }

    snprintf(oink::gApp.wigleCurrentPath,
             sizeof(oink::gApp.wigleCurrentPath),
             "%s/wigle-%s-boot%06lu-%03u.csv",
             kWigleDir,
             dayToken,
             oink::settings::bootCount(),
             static_cast<unsigned>(gWardriveFileIndex));

    gWigleFile = gSd.open(oink::gApp.wigleCurrentPath, O_WRONLY | O_CREAT | O_APPEND);
    if (!gWigleFile) {
        gWardriveFile.close();
        oink::gApp.wardriveActive = false;
        oink::gApp.wardriveCurrentFileBytes = 0;
        oink::gApp.wardriveCurrentPath[0] = '\0';
        return false;
    }

    oink::gApp.wigleCurrentFileBytes = static_cast<size_t>(gWigleFile.fileSize());
    if (oink::gApp.wigleCurrentFileBytes == 0) {
        gWigleFile.println(kWiglePreamble);
        gWigleFile.println(kWigleHeader);
        flushWigleFile();
        oink::gApp.wigleCurrentFileBytes = static_cast<size_t>(gWigleFile.fileSize());
    }

    oink::gApp.wardriveActive = true;
    oink::gApp.wardriveLastFlushMs = millis();
    oink::gApp.wardriveLastRotateMs = millis();
    printf("[OINK-YOU] Wardrive log active: %s\n", oink::gApp.wardriveCurrentPath);
    return true;
}

void closeWardriveFile() {
    if (gWardriveFile) {
        flushWardriveFile("close");
        gWardriveFile.close();
    }
    if (gWigleFile) {
        flushWigleFile();
        oink::gApp.wigleCurrentFileBytes = static_cast<size_t>(gWigleFile.fileSize());
        rememberClosedWigleFile(oink::gApp.wigleCurrentPath, oink::gApp.wigleCurrentFileBytes);
        gWigleFile.close();
    }
    oink::gApp.wardriveActive = false;
}

void clearWardriveRuntimeState() {
    oink::gApp.wardriveActive = false;
    oink::gApp.wardriveLastFlushMs = 0;
    oink::gApp.wardriveLastRotateMs = 0;
    oink::gApp.wardriveCurrentFileBytes = 0;
    oink::gApp.wardriveCurrentPath[0] = '\0';
    oink::gApp.wigleCurrentFileBytes = 0;
    oink::gApp.wigleCurrentPath[0] = '\0';
}

#if OINK_WDRIVE_DEBUG
void refreshWardriveDebugMetrics() {
    gWardriveDebugMetrics.ringbufSize = gWardriveDebugCount;
    gWardriveDebugMetrics.lastFlushTs = oink::gApp.wardriveLastFlushMs;
    gWardriveDebugMetrics.wardriveEnabled = oink::gApp.runtimeConfig.wardrive.enabled;
}

void updateWardriveWifiDebugState(const char* wifiMode, bool staEnabled, bool pending) {
    strlcpy(gWardriveDebugWifiMode, wifiMode ? wifiMode : "unknown", sizeof(gWardriveDebugWifiMode));
    gWardriveDebugMetrics.staEnabled = staEnabled;
    gWardriveDebugMetrics.wifiScanPending = pending;
}

void pushWardriveDebugEvent(const char* eventType,
                            const char* decision,
                            const char* reason,
                            const char* method,
                            int rssi,
                            int count,
                            bool hasGps,
                            bool timeSynced) {
    WardriveDebugEvent& event = gWardriveDebugRing[gWardriveDebugHead];
    memset(&event, 0, sizeof(event));
    event.millisAtEvent = millis();
    strlcpy(event.eventType, eventType ? eventType : "event", sizeof(event.eventType));
    strlcpy(event.decision, decision ? decision : "info", sizeof(event.decision));
    strlcpy(event.reason, reason ? reason : "", sizeof(event.reason));
    strlcpy(event.method, method ? method : "", sizeof(event.method));
    event.rssi = rssi;
    event.count = count;
    event.hasGps = hasGps;
    event.timeSynced = timeSynced;

    gWardriveDebugHead = (gWardriveDebugHead + 1) % kWardriveDebugRingSize;
    if (gWardriveDebugCount < kWardriveDebugRingSize) {
        ++gWardriveDebugCount;
    }
    refreshWardriveDebugMetrics();
}

void maybeLogWardriveDebugSummary() {
    if (millis() - gWardriveDebugLastLogMs < kWardriveDebugLogIntervalMs) {
        return;
    }
    gWardriveDebugLastLogMs = millis();
    refreshWardriveDebugMetrics();
    printf("[OINK-YOU] WDRIVE dbg scans=%lu seen=%lu logged=%lu dedup=%lu gps_drop=%lu time_drop=%lu flushes=%lu flush_err=%lu wifi_try=%lu wifi_fail=%lu wifi_last=%d wifi_mode=%s sta=%s pending=%s cycle=%lu enabled=%s active=%s\n",
           gWardriveDebugMetrics.scansRun,
           gWardriveDebugMetrics.apsSeen,
           gWardriveDebugMetrics.apsLogged,
           gWardriveDebugMetrics.apsDroppedDedup,
           gWardriveDebugMetrics.apsDroppedGps,
           gWardriveDebugMetrics.apsDroppedTime,
           gWardriveDebugMetrics.flushes,
           gWardriveDebugMetrics.flushErrors,
           gWardriveDebugMetrics.wifiScanAttempts,
           gWardriveDebugMetrics.wifiScanFailures,
           gWardriveDebugMetrics.wifiScanResultsLast,
           gWardriveDebugWifiMode,
           gWardriveDebugMetrics.staEnabled ? "yes" : "no",
           gWardriveDebugMetrics.wifiScanPending ? "yes" : "no",
           gWardriveDebugLastCycleResults,
           oink::gApp.runtimeConfig.wardrive.enabled ? "yes" : "no",
           oink::gApp.wardriveActive ? "yes" : "no");
}

void noteWardriveFlushResult(bool ok, const char* reason) {
    ++gWardriveDebugMetrics.flushes;
    if (!ok) {
        ++gWardriveDebugMetrics.flushErrors;
    }
    gWardriveDebugLastFlushOk = ok;
    strlcpy(gWardriveDebugLastFlushReason, reason ? reason : "", sizeof(gWardriveDebugLastFlushReason));
    if (ok) {
        oink::gApp.wardriveLastFlushMs = millis();
        gWardriveDebugMetrics.lastFlushTs = millis();
    }
    pushWardriveDebugEvent("flush", ok ? "ok" : "error", reason, "", 0, 0, oink::scan::gpsIsFresh(), oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
}

void noteWardriveWifiScanStateImpl(const char* decision,
                                   const char* reason,
                                   int resultCount,
                                   const char* wifiMode,
                                   bool staEnabled,
                                   bool pending) {
    updateWardriveWifiDebugState(wifiMode, staEnabled, pending);
    if (decision && strcmp(decision, "start") == 0) {
        ++gWardriveDebugMetrics.wifiScanAttempts;
    } else if (decision && strcmp(decision, "error") == 0) {
        ++gWardriveDebugMetrics.wifiScanFailures;
    } else if (decision && strcmp(decision, "complete") == 0) {
        gWardriveDebugMetrics.wifiScanResultsLast = resultCount;
    }
    strlcpy(gWardriveDebugWifiReason, reason ? reason : "", sizeof(gWardriveDebugWifiReason));
    pushWardriveDebugEvent("wifi_scan",
                           decision ? decision : "info",
                           reason ? reason : "",
                           wifiMode ? wifiMode : "",
                           0,
                           resultCount,
                           oink::scan::gpsIsFresh(),
                           oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
}
#endif

void flushWardriveFile(const char* reason) {
    if (!gWardriveFile) {
        return;
    }

    gWardriveFile.flush();
    bool ok = !gWardriveFile.getWriteError();
    if (!ok) {
        oink::gApp.sdLoggingHealthy = false;
    }
#if OINK_WDRIVE_DEBUG
    noteWardriveFlushResult(ok, reason);
#else
    if (ok) {
        oink::gApp.wardriveLastFlushMs = millis();
    }
#endif
}

void flushWigleFile() {
    if (!gWigleFile) {
        return;
    }
    gWigleFile.flush();
    if (gWigleFile.getWriteError()) {
        oink::gApp.sdLoggingHealthy = false;
        return;
    }
    oink::gApp.wigleCurrentFileBytes = static_cast<size_t>(gWigleFile.fileSize());
}

bool shouldLogWardrive(const oink::Detection& detection) {
    if (!oink::gApp.wardriveActive) {
        return false;
    }

    unsigned long windowMs =
        static_cast<unsigned long>(oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds) * 1000UL;
    if (windowMs == 0) {
        return true;
    }

    for (size_t i = 0; i < kWardriveDedupEntryCount; ++i) {
        WardriveDedupEntry& entry = gWardriveDedup[i];
        if (entry.mac[0] == '\0') {
            continue;
        }
        if (sameWardriveIdentity(entry, detection)) {
            if (millis() - entry.lastLoggedMs < windowMs) {
                bool movedEnough = false;
                if (detection.hasGPS && entry.hasGps &&
                    oink::gApp.runtimeConfig.wardrive.moveThresholdMeters > 0) {
                    movedEnough =
                        wardriveDistanceMeters(entry.gpsLat,
                                               entry.gpsLon,
                                               detection.gpsLat,
                                               detection.gpsLon) >=
                        static_cast<double>(oink::gApp.runtimeConfig.wardrive.moveThresholdMeters);
                }
                if (movedEnough) {
                    updateWardriveDedupEntry(entry, detection);
                    return true;
                }
#if OINK_WDRIVE_DEBUG
                ++gWardriveDebugMetrics.apsDroppedDedup;
                pushWardriveDebugEvent("candidate",
                                       "dropped",
                                       "dedup",
                                       detection.method,
                                       detection.rssi,
                                       detection.count,
                                       detection.hasGPS,
                                       oink::timeutil::isSynced());
                maybeLogWardriveDebugSummary();
#endif
                return false;
            }
            updateWardriveDedupEntry(entry, detection);
            return true;
        }
    }

    WardriveDedupEntry& slot = gWardriveDedup[gWardriveDedupNext];
    strlcpy(slot.mac, detection.mac, sizeof(slot.mac));
    strlcpy(slot.name, detection.name, sizeof(slot.name));
    strlcpy(slot.method, detection.method, sizeof(slot.method));
    updateWardriveDedupEntry(slot, detection);
    gWardriveDedupNext = (gWardriveDedupNext + 1) % kWardriveDedupEntryCount;
    return true;
}

void writeSessionCsvHeaderIfMissing(const char* path) {
    if (pathExists(path)) {
        return;
    }

    appendLine(path, String("# oinkspy_firmware=") + kFirmwareVersion);
    appendLine(path, String("# device_id=") + oink::settings::deviceId());
    appendLine(path, String("# boot_count=") + oink::settings::bootCount());
    appendLine(path, String("# time_source=") + oink::timeutil::timeSourceLabel());
    appendLine(path, "millis,epoch,iso8601,time_source,device_id,boot_count,record_type,label,mac,name,rssi,method,count,is_raven,raven_fw,gps_lat,gps_lon,gps_acc");
}

void writeDailyCsvHeaderIfMissing(const char* path) {
    if (pathExists(path)) {
        return;
    }

    appendLine(path, String("# oinkspy_firmware=") + kFirmwareVersion);
    appendLine(path, String("# device_id=") + oink::settings::deviceId());
    appendLine(path, "millis,epoch,iso8601,time_source,device_id,boot_count,record_type,label,mac,name,rssi,method,count,is_raven,raven_fw,gps_lat,gps_lon,gps_acc");
}

void writeSessionJsonlHeaderIfMissing(const char* path) {
    if (pathExists(path)) {
        return;
    }

    char isoBuffer[40];
    oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));

    JsonDocument doc;
    doc["record_type"] = "header";
    doc["firmware_version"] = kFirmwareVersion;
    doc["device_id"] = oink::settings::deviceId();
    doc["boot_count"] = oink::settings::bootCount();
    doc["time_source"] = oink::timeutil::timeSourceLabel();
    if (isoBuffer[0]) {
        doc["iso8601"] = isoBuffer;
    }
    String line;
    serializeJson(doc, line);
    appendLine(path, line);
}

void writeDailyJsonlHeaderIfMissing(const char* path) {
    if (pathExists(path)) {
        return;
    }

    char isoBuffer[40];
    oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));

    JsonDocument doc;
    doc["record_type"] = "header";
    doc["firmware_version"] = kFirmwareVersion;
    doc["device_id"] = oink::settings::deviceId();
    if (isoBuffer[0]) {
        doc["iso8601"] = isoBuffer;
    }
    String line;
    serializeJson(doc, line);
    appendLine(path, line);
}

void ensureSdLogFiles() {
    if (!oink::gApp.sdReady || !oink::gApp.runtimeConfig.sdLoggingEnabled) {
        return;
    }

    ensureDir(kLogsDir);
    ensureDir(kSessionsDir);
    ensureDir(kDailyDir);

    char dayToken[16];
    oink::timeutil::currentDayToken(dayToken, sizeof(dayToken));
    if (!dayToken[0]) {
        strlcpy(dayToken, kUnsyncedDayToken, sizeof(dayToken));
    }

    snprintf(oink::gApp.sessionCsvPath,
             sizeof(oink::gApp.sessionCsvPath),
             "%s/session-%s-boot%06lu.csv",
             kSessionsDir,
             oink::settings::deviceId(),
             oink::settings::bootCount());
    snprintf(oink::gApp.sessionJsonlPath,
             sizeof(oink::gApp.sessionJsonlPath),
             "%s/session-%s-boot%06lu.jsonl",
             kSessionsDir,
             oink::settings::deviceId(),
             oink::settings::bootCount());
    snprintf(oink::gApp.dailyCsvPath,
             sizeof(oink::gApp.dailyCsvPath),
             "%s/daily-%s.csv",
             kDailyDir,
             dayToken);
    snprintf(oink::gApp.dailyJsonlPath,
             sizeof(oink::gApp.dailyJsonlPath),
             "%s/daily-%s.jsonl",
             kDailyDir,
             dayToken);

    if (oink::gApp.runtimeConfig.sdCsvEnabled) {
        writeSessionCsvHeaderIfMissing(oink::gApp.sessionCsvPath);
        writeDailyCsvHeaderIfMissing(oink::gApp.dailyCsvPath);
    }
    if (oink::gApp.runtimeConfig.sdJsonEnabled) {
        writeSessionJsonlHeaderIfMissing(oink::gApp.sessionJsonlPath);
        writeDailyJsonlHeaderIfMissing(oink::gApp.dailyJsonlPath);
    }
}

String csvField(const char* text) {
    String field = text ? text : "";
    field.replace("\"", "'");
    return String('"') + field + String('"');
}

String buildCsvLine(const char* recordType,
                    const char* label,
                    const char* mac,
                    const char* name,
                    int rssi,
                    const char* method,
                    bool isRaven,
                    const char* ravenFw,
                    bool hasGps,
                    double gpsLat,
                    double gpsLon,
                    float gpsAcc,
                    int count) {
    char isoBuffer[40];
    oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));
    time_t epoch = oink::timeutil::currentEpoch();

    String line;
    line.reserve(320);
    line += String(millis());
    line += ',';
    line += String(static_cast<unsigned long>(epoch));
    line += ',';
    line += csvField(isoBuffer);
    line += ',';
    line += csvField(oink::timeutil::timeSourceLabel());
    line += ',';
    line += oink::settings::deviceId();
    line += ',';
    line += String(oink::settings::bootCount());
    line += ',';
    line += csvField(recordType);
    line += ',';
    line += csvField(label);
    line += ',';
    line += csvField(mac);
    line += ',';
    line += csvField(name);
    line += ',';
    line += String(rssi);
    line += ',';
    line += csvField(method);
    line += ',';
    line += String(count);
    line += ',';
    line += (isRaven ? "true" : "false");
    line += ',';
    line += csvField(ravenFw);
    if (hasGps) {
        line += ',';
        line += String(gpsLat, 8);
        line += ',';
        line += String(gpsLon, 8);
        line += ',';
        line += String(gpsAcc, 1);
    } else {
        line += ",,,";
    }
    return line;
}

String buildJsonLine(const char* mac,
                     const char* name,
                     int rssi,
                     const char* method,
                     bool isRaven,
                     const char* ravenFw,
                     bool hasGps,
                     double gpsLat,
                     double gpsLon,
                     float gpsAcc,
                     int count) {
    char isoBuffer[40];
    oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));
    time_t epoch = oink::timeutil::currentEpoch();

    JsonDocument doc;
    doc["record_type"] = "detection";
    doc["millis"] = millis();
    doc["epoch"] = static_cast<unsigned long>(epoch);
    doc["device_id"] = oink::settings::deviceId();
    doc["boot_count"] = oink::settings::bootCount();
    doc["time_source"] = oink::timeutil::timeSourceLabel();
    if (isoBuffer[0]) {
        doc["iso8601"] = isoBuffer;
    }
    doc["mac"] = mac;
    doc["name"] = name;
    doc["rssi"] = rssi;
    doc["method"] = method;
    doc["count"] = count;
    doc["is_raven"] = isRaven;
    doc["raven_fw"] = ravenFw;
    if (hasGps) {
        JsonObject gps = doc["gps"].to<JsonObject>();
        gps["lat"] = gpsLat;
        gps["lon"] = gpsLon;
        gps["acc"] = gpsAcc;
    }
    String line;
    serializeJson(doc, line);
    return line;
}

void processDetectionEvent(const LogEvent& event) {
    if (!oink::gApp.sdReady || !oink::gApp.runtimeConfig.sdLoggingEnabled) {
        return;
    }

    ensureSdLogFiles();

    if (oink::gApp.runtimeConfig.sdCsvEnabled) {
        String csv = buildCsvLine("detection",
                                  "",
                                  event.mac,
                                  event.name,
                                  event.rssi,
                                  event.method,
                                  event.isRaven,
                                  event.ravenFw,
                                  event.hasGps,
                                  event.gpsLat,
                                  event.gpsLon,
                                  event.gpsAcc,
                                  event.count);
        appendLine(oink::gApp.sessionCsvPath, csv);
        appendLine(oink::gApp.dailyCsvPath, csv);
    }

    if (oink::gApp.runtimeConfig.sdJsonEnabled) {
        String json = buildJsonLine(event.mac,
                                    event.name,
                                    event.rssi,
                                    event.method,
                                    event.isRaven,
                                    event.ravenFw,
                                    event.hasGps,
                                    event.gpsLat,
                                    event.gpsLon,
                                    event.gpsAcc,
                                    event.count);
        appendLine(oink::gApp.sessionJsonlPath, json);
        appendLine(oink::gApp.dailyJsonlPath, json);
    }
}

void processBookmarkEvent(const LogEvent& event) {
    if (!oink::gApp.sdReady || !oink::gApp.runtimeConfig.sdLoggingEnabled) {
        return;
    }

    ensureSdLogFiles();

    const char* bookmarkLabel = event.label[0] ? event.label : "manual";
    if (oink::gApp.runtimeConfig.sdCsvEnabled) {
        String csv = buildCsvLine("bookmark", bookmarkLabel, "", "", 0, "bookmark", false, "", false, 0.0, 0.0, 0.0f, 0);
        appendLine(oink::gApp.sessionCsvPath, csv);
        appendLine(oink::gApp.dailyCsvPath, csv);
    }

    if (oink::gApp.runtimeConfig.sdJsonEnabled) {
        char isoBuffer[40];
        oink::timeutil::formatIso8601(isoBuffer, sizeof(isoBuffer));
        JsonDocument doc;
        doc["record_type"] = "bookmark";
        doc["millis"] = millis();
        doc["epoch"] = static_cast<unsigned long>(oink::timeutil::currentEpoch());
        doc["device_id"] = oink::settings::deviceId();
        doc["boot_count"] = oink::settings::bootCount();
        doc["time_source"] = oink::timeutil::timeSourceLabel();
        if (isoBuffer[0]) {
            doc["iso8601"] = isoBuffer;
        }
        doc["label"] = bookmarkLabel;
        String line;
        serializeJson(doc, line);
        appendLine(oink::gApp.sessionJsonlPath, line);
        appendLine(oink::gApp.dailyJsonlPath, line);
    }
}

void processLogEvent(const LogEvent& event) {
    rememberRecentEvent(event);
    switch (event.type) {
        case LogEventType::Detection:
            processDetectionEvent(event);
            break;
        case LogEventType::Bookmark:
            processBookmarkEvent(event);
            break;
        default:
            break;
    }
    ++oink::gApp.logEventsWritten;
}

bool enqueueEvent(const LogEvent& event) {
    if (!gLogQueue) {
        ++oink::gApp.logEventsDropped;
        return false;
    }

    BaseType_t ok = xQueueSend(gLogQueue, &event, 0);
    if (ok != pdPASS) {
        ++oink::gApp.logEventsDropped;
        return false;
    }

    ++oink::gApp.logEventsQueued;
    return true;
}

void logWriterTask(void*) {
    LogEvent event = {};
    for (;;) {
        if (xQueueReceive(gLogQueue, &event, pdMS_TO_TICKS(250)) == pdTRUE) {
            processLogEvent(event);
        }
    }
}

void startLogWorker() {
    if (gLogQueue) {
        return;
    }

    gLogQueue = xQueueCreate(kLogQueueLength, sizeof(LogEvent));
    if (!gLogQueue) {
        printf("[OINK-YOU] Log queue create failed\n");
        oink::gApp.logWorkerReady = false;
        return;
    }

    BaseType_t taskOk = xTaskCreate(logWriterTask,
                                    "OinkLog",
                                    kLogTaskStack,
                                    nullptr,
                                    1,
                                    &gLogTaskHandle);
    if (taskOk != pdPASS) {
        printf("[OINK-YOU] Log task create failed\n");
        vQueueDelete(gLogQueue);
        gLogQueue = nullptr;
        oink::gApp.logWorkerReady = false;
        return;
    }

    oink::gApp.logWorkerReady = true;
    printf("[OINK-YOU] Log worker ready (queue=%u)\n", static_cast<unsigned>(kLogQueueLength));
}

} // namespace

namespace oink::log {

void promotePrevSession() {
    if (!gApp.spiffsReady) {
        return;
    }
    if (!SPIFFS.exists(config::kSessionFile)) {
        printf("[OINK-YOU] No prior session file to promote\n");
        return;
    }

    File src = SPIFFS.open(config::kSessionFile, "r");
    if (!src) {
        printf("[OINK-YOU] Failed to open session file for promotion\n");
        return;
    }

    String data = src.readString();
    src.close();
    if (data.length() == 0) {
        printf("[OINK-YOU] Session file empty, skipping promotion\n");
        SPIFFS.remove(config::kSessionFile);
        return;
    }

    File dst = SPIFFS.open(config::kPrevSessionFile, "w");
    if (!dst) {
        printf("[OINK-YOU] Failed to create prev_session file\n");
        return;
    }
    dst.print(data);
    dst.close();

    SPIFFS.remove(config::kSessionFile);
    printf("[OINK-YOU] Prior session promoted: %d bytes\n", data.length());
}

void beginStorage() {
    startLogWorker();

    if (SPIFFS.begin(true)) {
        gApp.spiffsReady = true;
        printf("[OINK-YOU] SPIFFS ready\n");
        promotePrevSession();
    } else {
        printf("[OINK-YOU] SPIFFS init failed - no persistence\n");
    }

    gSdSpi.begin(config::kSpiSckPin, config::kSpiMisoPin, config::kSpiMosiPin, config::kSdChipSelectPin);
    pinMode(config::kSdChipSelectPin, OUTPUT);
    digitalWrite(config::kSdChipSelectPin, HIGH);
    if (gSd.begin(SdSpiConfig(config::kSdChipSelectPin, SHARED_SPI, SD_SCK_MHZ(12), &gSdSpi))) {
        gApp.sdReady = true;
        oink::gApp.sdLoggingHealthy = true;
        printf("[OINK-YOU] SD ready on CS=%u\n", config::kSdChipSelectPin);
    } else {
        gApp.sdReady = false;
        oink::gApp.sdLoggingHealthy = false;
        printf("[OINK-YOU] SD mount failed - continuing without SD logging\n");
    }
}

bool readSdTextFile(const char* path, String& out) {
    out = "";
    if (!gApp.sdReady) {
        return false;
    }

    FsFile file = gSd.open(path, O_RDONLY);
    if (!file) {
        return false;
    }

    while (file.available()) {
        out += static_cast<char>(file.read());
    }
    file.close();
    return out.length() > 0;
}

bool writeSdTextFile(const char* path, const String& content) {
    if (!gApp.sdReady || !path || !path[0]) {
        return false;
    }

    ensureParentDirs(path);
    FsFile file = gSd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) {
        printf("[OINK-YOU] SD open failed: %s\n", path);
        gApp.sdLoggingHealthy = false;
        return false;
    }

    size_t written = file.print(content);
    file.flush();
    file.close();
    if (written != content.length()) {
        printf("[OINK-YOU] SD write failed: %s\n", path);
        gApp.sdLoggingHealthy = false;
        return false;
    }

    gApp.sdLoggingHealthy = true;
    return true;
}

bool prepareWigleCsv(char* pathOut, size_t pathOutSize, size_t& fileBytes, String& error) {
    error = "";
    fileBytes = 0;
    oink::gApp.wigleUploadCandidatePath[0] = '\0';
    if (!gApp.sdReady) {
        error = "SD card not ready";
        return false;
    }
    if (gApp.wardriveActive) {
        error = "Stop Wardrive before WiGLE export or upload";
        return false;
    }
    const char* candidatePath = gApp.wigleLastClosedPath[0] ? gApp.wigleLastClosedPath : gApp.wigleCurrentPath;
    if (!candidatePath[0]) {
        error = "No WiGLE CSV captured yet";
        return false;
    }

    FsFile file = gSd.open(candidatePath, O_RDONLY);
    if (!file) {
        error = "WiGLE CSV file not found";
        return false;
    }
    fileBytes = static_cast<size_t>(file.fileSize());
    if (fileBytes == 0) {
        file.close();
        error = "WiGLE CSV file is empty";
        return false;
    }
    if (!validateWigleCsvFile(file, error)) {
        file.close();
        return false;
    }
    file.close();
    if (pathOut && pathOutSize > 0) {
        strlcpy(pathOut, candidatePath, pathOutSize);
    }
    strlcpy(gApp.wigleUploadCandidatePath, candidatePath, sizeof(gApp.wigleUploadCandidatePath));
    if (strcmp(candidatePath, gApp.wigleCurrentPath) == 0) {
        gApp.wigleCurrentFileBytes = fileBytes;
    }
    if (strcmp(candidatePath, gApp.wigleLastClosedPath) == 0) {
        gApp.wigleLastClosedFileBytes = fileBytes;
    }
    return true;
}

bool writeSdFileToStream(const char* path, Print& out) {
    if (!gApp.sdReady || !path || !path[0]) {
        return false;
    }

    FsFile file = gSd.open(path, O_RDONLY);
    if (!file) {
        return false;
    }

    uint8_t buffer[512];
    while (file.available()) {
        int readCount = file.read(buffer, sizeof(buffer));
        if (readCount <= 0) {
            file.close();
            return false;
        }
        size_t written = out.write(buffer, static_cast<size_t>(readCount));
        if (written != static_cast<size_t>(readCount)) {
            file.close();
            return false;
        }
    }
    file.close();
    return true;
}

void saveSession() {
    if (!gApp.spiffsReady || !gApp.mutex) {
        return;
    }
    if (xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(300)) != pdTRUE) {
        return;
    }

    File file = SPIFFS.open(config::kSessionFile, "w");
    if (!file) {
        xSemaphoreGive(gApp.mutex);
        return;
    }

    file.print("[");
    for (int i = 0; i < gApp.detectionCount; ++i) {
        if (i > 0) {
            file.print(",");
        }
        Detection& detection = gApp.detections[i];
        file.printf(
            "{\"mac\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"method\":\"%s\",\"first\":%lu,\"last\":%lu,\"count\":%d,\"raven\":%s,\"fw\":\"%s\"",
            detection.mac,
            detection.name,
            detection.rssi,
            detection.method,
            detection.firstSeen,
            detection.lastSeen,
            detection.count,
            detection.isRaven ? "true" : "false",
            detection.ravenFW);
        if (detection.hasGPS) {
            file.printf(",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"acc\":%.1f}",
                        detection.gpsLat,
                        detection.gpsLon,
                        detection.gpsAcc);
        }
        file.print("}");
    }
    file.print("]");
    file.close();

    gApp.lastSaveRevision = gApp.detectionRevision;
    printf("[OINK-YOU] Session saved: %d detections\n", gApp.detectionCount);
    xSemaphoreGive(gApp.mutex);
}

void pollAutoSave() {
    if (!gApp.spiffsReady) {
        return;
    }

    unsigned long saveIntervalMs = gApp.runtimeConfig.saveIntervalMs > 0 ? gApp.runtimeConfig.saveIntervalMs : config::kSaveIntervalMs;
    if (millis() - gApp.lastSave >= saveIntervalMs) {
        if (gApp.detectionCount > 0 && gApp.detectionRevision != gApp.lastSaveRevision) {
            saveSession();
        }
        gApp.lastSave = millis();
        return;
    }

    if (gApp.detectionCount > 0 && gApp.lastSaveRevision == 0 && millis() - gApp.lastSave >= config::kInitialSaveDelayMs) {
        saveSession();
        gApp.lastSave = millis();
    }
}

void prepareSdLogs() {
    ensureSdLogFiles();
    if (gApp.runtimeConfig.wardrive.enabled) {
        beginWardriveSession();
    }
}

void noteWardriveScanStart(const char* reason) {
#if OINK_WDRIVE_DEBUG
    ++gWardriveDebugMetrics.scansRun;
    gWardriveDebugMetrics.lastScanTs = millis();
    gWardriveDebugLastCycleResults = gWardriveDebugCycleResults;
    gWardriveDebugCycleResults = 0;
    pushWardriveDebugEvent("scan",
                           "start",
                           reason ? reason : (oink::scan::isScanningEnabled() ? "scan_cycle" : "scan_paused"),
                           "",
                           0,
                           static_cast<int>(gWardriveDebugLastCycleResults),
                           oink::scan::gpsIsFresh(),
                           oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
#endif
}

void noteWardriveScanCandidate(const char* method, int rssi, int count, bool hasGps, bool timeSynced) {
#if OINK_WDRIVE_DEBUG
    ++gWardriveDebugMetrics.apsSeen;
    ++gWardriveDebugCycleResults;
    pushWardriveDebugEvent("scan", "seen", "candidate", method, rssi, count, hasGps, timeSynced);
    maybeLogWardriveDebugSummary();
#else
    (void)method;
    (void)rssi;
    (void)count;
    (void)hasGps;
    (void)timeSynced;
#endif
}

void noteWardriveWifiScanState(const char* decision,
                               const char* reason,
                               int resultCount,
                               const char* wifiMode,
                               bool staEnabled,
                               bool pending) {
#if OINK_WDRIVE_DEBUG
    noteWardriveWifiScanStateImpl(decision, reason, resultCount, wifiMode, staEnabled, pending);
#else
    (void)decision;
    (void)reason;
    (void)resultCount;
    (void)wifiMode;
    (void)staEnabled;
    (void)pending;
#endif
}

void beginWardriveSession() {
    closeWardriveFile();
    clearWardriveRuntimeState();
    resetWardriveDedup();
    gWardriveFileIndex = 0;

    String message;
    if (!wardriveCanRun(message)) {
        if (message.length()) {
            printf("[OINK-YOU] Wardrive inactive: %s\n", message.c_str());
        }
#if OINK_WDRIVE_DEBUG
        pushWardriveDebugEvent("state",
                               "inactive",
                               message.c_str(),
                               "",
                               0,
                               0,
                               oink::scan::gpsIsFresh(),
                               oink::timeutil::isSynced());
        maybeLogWardriveDebugSummary();
#endif
        return;
    }

    if (!openWardriveFile()) {
        printf("[OINK-YOU] Wardrive file open failed\n");
#if OINK_WDRIVE_DEBUG
        pushWardriveDebugEvent("state",
                               "error",
                               "open_failed",
                               "",
                               0,
                               0,
                               oink::scan::gpsIsFresh(),
                               oink::timeutil::isSynced());
        maybeLogWardriveDebugSummary();
#endif
        return;
    }
#if OINK_WDRIVE_DEBUG
    pushWardriveDebugEvent("state", "enabled", "session_started", "", 0, 0, oink::scan::gpsIsFresh(), oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
#endif
}

void endWardriveSession() {
    closeWardriveFile();
    clearWardriveRuntimeState();
#if OINK_WDRIVE_DEBUG
    pushWardriveDebugEvent("state", "disabled", "session_stopped", "", 0, 0, oink::scan::gpsIsFresh(), oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
#endif
}

void pollWardrive() {
    if (!gApp.runtimeConfig.wardrive.enabled || !gApp.wardriveActive || !gWardriveFile) {
        return;
    }

    unsigned long flushIntervalMs =
        static_cast<unsigned long>(gApp.runtimeConfig.wardrive.flushIntervalSeconds) * 1000UL;
    if (flushIntervalMs == 0) {
        flushIntervalMs = 1000UL;
    }
    if (millis() - gApp.wardriveLastFlushMs < flushIntervalMs) {
        return;
    }

    flushWardriveFile("interval");
    flushWigleFile();
}

void appendWardriveDetection(const Detection& detection) {
    if (!gApp.runtimeConfig.wardrive.enabled) {
        return;
    }

    if (!gApp.wardriveActive || !gWardriveFile) {
        beginWardriveSession();
        if (!gApp.wardriveActive || !gWardriveFile) {
            return;
        }
    }

    if (!shouldLogWardrive(detection)) {
        return;
    }

    if (gApp.wardriveCurrentFileBytes >= wardriveRotationBytes()) {
        closeWardriveFile();
        ++gWardriveFileIndex;
        if (!openWardriveFile()) {
            printf("[OINK-YOU] Wardrive rotation failed\n");
            return;
        }
    }

    String line = buildWardriveCsvLine(detection);
    if (!gWardriveFile.println(line)) {
        printf("[OINK-YOU] Wardrive append failed: %s\n", gApp.wardriveCurrentPath);
        gApp.sdLoggingHealthy = false;
#if OINK_WDRIVE_DEBUG
        ++gWardriveDebugMetrics.flushErrors;
        pushWardriveDebugEvent("candidate",
                               "error",
                               "sd_write",
                               detection.method,
                               detection.rssi,
                               detection.count,
                               detection.hasGPS,
                               oink::timeutil::isSynced());
        maybeLogWardriveDebugSummary();
#endif
        closeWardriveFile();
        return;
    }

    gApp.sdLoggingHealthy = true;
    gApp.wardriveCurrentFileBytes = static_cast<size_t>(gWardriveFile.fileSize());
    if (strcasecmp(detection.method, "wifi_ap") == 0 && detection.hasGPS && gWigleFile) {
        String wigleLine = buildWigleCsvLine(detection);
        if (!gWigleFile.println(wigleLine)) {
            printf("[OINK-YOU] WiGLE sidecar append failed: %s\n", gApp.wigleCurrentPath);
            gApp.sdLoggingHealthy = false;
            closeWardriveFile();
            return;
        }
        gApp.wigleCurrentFileBytes = static_cast<size_t>(gWigleFile.fileSize());
    }
#if OINK_WDRIVE_DEBUG
    ++gWardriveDebugMetrics.apsLogged;
    pushWardriveDebugEvent("candidate",
                           "kept",
                           detection.hasGPS ? (oink::timeutil::isSynced() ? "logged" : "time_optional")
                                            : (oink::timeutil::isSynced() ? "gps_optional" : "gps_time_optional"),
                           detection.method,
                           detection.rssi,
                           detection.count,
                           detection.hasGPS,
                           oink::timeutil::isSynced());
    maybeLogWardriveDebugSummary();
#endif
    unsigned long flushIntervalMs =
        static_cast<unsigned long>(gApp.runtimeConfig.wardrive.flushIntervalSeconds) * 1000UL;
    if (flushIntervalMs == 0 || millis() - gApp.wardriveLastFlushMs >= flushIntervalMs) {
        flushWardriveFile("append");
        flushWigleFile();
    }
}

WardriveStatus wardriveStatus() {
    WardriveStatus status = {};
    status.enabled = gApp.runtimeConfig.wardrive.enabled;
    status.active = gApp.wardriveActive;
    status.sdReady = gApp.sdReady;
    status.flushIntervalSeconds = gApp.runtimeConfig.wardrive.flushIntervalSeconds;
    status.fileRotationMb = gApp.runtimeConfig.wardrive.fileRotationMb;
    status.dedupWindowSeconds = gApp.runtimeConfig.wardrive.dedupWindowSeconds;
    strlcpy(status.logFormat, gApp.runtimeConfig.wardrive.logFormat, sizeof(status.logFormat));
    strlcpy(status.currentPath, gApp.wardriveCurrentPath, sizeof(status.currentPath));
    status.currentFileBytes = static_cast<unsigned long>(gApp.wardriveCurrentFileBytes);

    String message;
    status.canStart = wardriveCanRun(message);
    status.configValid = strcasecmp(gApp.runtimeConfig.wardrive.logFormat, "csv") == 0;
    if (status.active && status.currentPath[0]) {
        strlcpy(status.message, "Logging detections to SD", sizeof(status.message));
    } else if (message.length()) {
        strlcpy(status.message, message.c_str(), sizeof(status.message));
    } else {
        strlcpy(status.message, "Ready", sizeof(status.message));
    }
    return status;
}

void writeWardriveStatusJson(Print& out) {
    WardriveStatus status = wardriveStatus();
    out.printf("{\"enabled\":%s,\"active\":%s,\"can_start\":%s,\"sd_ready\":%s,\"config_valid\":%s,\"flush_interval_seconds\":%lu,\"file_rotation_mb\":%lu,\"dedup_window_seconds\":%lu,\"move_threshold_meters\":%u,\"log_format\":\"%s\",\"current_path\":\"%s\",\"current_file_bytes\":%lu,\"message\":\"%s\"}",
               status.enabled ? "true" : "false",
               status.active ? "true" : "false",
               status.canStart ? "true" : "false",
               status.sdReady ? "true" : "false",
               status.configValid ? "true" : "false",
               status.flushIntervalSeconds,
               status.fileRotationMb,
               status.dedupWindowSeconds,
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.moveThresholdMeters),
               status.logFormat,
               status.currentPath,
               status.currentFileBytes,
               status.message);
}

void writeWardriveDebugJson(Print& out) {
#if OINK_WDRIVE_DEBUG
    refreshWardriveDebugMetrics();
    out.printf("{\"debug_compiled\":true,\"protocol\":\"ble+wifi_ap\",\"diagnosis\":\"Wardrive ingests matched BLE detections plus passive Wi-Fi AP scan results; GPS and time are optional.\",\"metrics\":{\"scans_run\":%lu,\"aps_seen\":%lu,\"aps_logged\":%lu,\"aps_dropped_dedup\":%lu,\"aps_dropped_gps\":%lu,\"aps_dropped_time\":%lu,\"flushes\":%lu,\"flush_errors\":%lu,\"wifi_scan_attempts\":%lu,\"wifi_scan_failures\":%lu,\"wifi_scan_results_last\":%d,\"ringbuf_size\":%u,\"last_scan_ts\":%lu,\"last_flush_ts\":%lu,\"wardrive_enabled\":%s,\"last_cycle_results\":%lu,\"last_flush_ok\":%s,\"last_flush_reason\":\"%s\",\"wifi_mode\":\"%s\",\"wifi_reason\":\"%s\",\"sta_enabled\":%s,\"wifi_scan_pending\":%s,\"scan_paused\":%s,\"gps_fresh\":%s,\"time_synced\":%s,\"move_threshold_meters\":%u},\"events\":[",
               gWardriveDebugMetrics.scansRun,
               gWardriveDebugMetrics.apsSeen,
               gWardriveDebugMetrics.apsLogged,
               gWardriveDebugMetrics.apsDroppedDedup,
               gWardriveDebugMetrics.apsDroppedGps,
               gWardriveDebugMetrics.apsDroppedTime,
               gWardriveDebugMetrics.flushes,
               gWardriveDebugMetrics.flushErrors,
               gWardriveDebugMetrics.wifiScanAttempts,
               gWardriveDebugMetrics.wifiScanFailures,
               gWardriveDebugMetrics.wifiScanResultsLast,
               static_cast<unsigned>(gWardriveDebugMetrics.ringbufSize),
               gWardriveDebugMetrics.lastScanTs,
               gWardriveDebugMetrics.lastFlushTs,
               gWardriveDebugMetrics.wardriveEnabled ? "true" : "false",
               gWardriveDebugLastCycleResults,
               gWardriveDebugLastFlushOk ? "true" : "false",
               gWardriveDebugLastFlushReason,
               gWardriveDebugWifiMode,
               gWardriveDebugWifiReason,
               gWardriveDebugMetrics.staEnabled ? "true" : "false",
               gWardriveDebugMetrics.wifiScanPending ? "true" : "false",
               oink::scan::isScanningEnabled() ? "false" : "true",
               oink::scan::gpsIsFresh() ? "true" : "false",
               oink::timeutil::isSynced() ? "true" : "false",
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.moveThresholdMeters));

    for (size_t i = 0; i < gWardriveDebugCount; ++i) {
        size_t index = (gWardriveDebugHead + kWardriveDebugRingSize - gWardriveDebugCount + i) % kWardriveDebugRingSize;
        const WardriveDebugEvent& event = gWardriveDebugRing[index];
        if (i > 0) {
            out.print(',');
        }
        out.printf("{\"millis\":%lu,\"event\":\"%s\",\"decision\":\"%s\",\"reason\":\"%s\",\"method\":\"%s\",\"rssi\":%d,\"count\":%d,\"has_gps\":%s,\"time_synced\":%s}",
                   event.millisAtEvent,
                   event.eventType,
                   event.decision,
                   event.reason,
                   event.method,
                   event.rssi,
                   event.count,
                   event.hasGps ? "true" : "false",
                   event.timeSynced ? "true" : "false");
    }
    out.print("]}");
#else
    out.print("{\"debug_compiled\":false,\"protocol\":\"ble+wifi_ap\",\"diagnosis\":\"Wardrive debug metrics are compiled out. Rebuild with -DOINK_WDRIVE_DEBUG=1.\"}");
#endif
}

void printWardriveDebug(Stream& out) {
    writeWardriveDebugJson(out);
    out.println();
}

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
                          int count) {
    LogEvent event = {};
    event.type = LogEventType::Detection;
    strlcpy(event.mac, mac ? mac : "", sizeof(event.mac));
    strlcpy(event.name, name ? name : "", sizeof(event.name));
    event.rssi = rssi;
    strlcpy(event.method, method ? method : "", sizeof(event.method));
    event.isRaven = isRaven;
    strlcpy(event.ravenFw, ravenFw ? ravenFw : "", sizeof(event.ravenFw));
    event.hasGps = hasGps;
    event.gpsLat = gpsLat;
    event.gpsLon = gpsLon;
    event.gpsAcc = gpsAcc;
    event.count = count;
    enqueueEvent(event);
}

void appendBookmarkEvent(const char* label) {
    LogEvent event = {};
    event.type = LogEventType::Bookmark;
    strlcpy(event.label, (label && label[0]) ? label : "manual", sizeof(event.label));
    enqueueEvent(event);
}

void writeDetectionsJson(AsyncResponseStream* resp) {
    resp->print("[");
    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < gApp.detectionCount; ++i) {
            if (i > 0) {
                resp->print(",");
            }
            Detection& detection = gApp.detections[i];
            resp->printf(
                "{\"mac\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"method\":\"%s\",\"first\":%lu,\"last\":%lu,\"count\":%d,\"raven\":%s,\"fw\":\"%s\"",
                detection.mac,
                detection.name,
                detection.rssi,
                detection.method,
                detection.firstSeen,
                detection.lastSeen,
                detection.count,
                detection.isRaven ? "true" : "false",
                detection.ravenFW);
            if (detection.hasGPS) {
                resp->printf(",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"acc\":%.1f}",
                             detection.gpsLat,
                             detection.gpsLon,
                             detection.gpsAcc);
            }
            resp->print("}");
        }
        xSemaphoreGive(gApp.mutex);
    }
    resp->print("]");
}

void writeDetectionsKml(AsyncResponseStream* resp) {
    resp->print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n"
                "<name>Oink-You Detections</name>\n"
                "<description>Surveillance device detections with GPS</description>\n"
                "<Style id=\"det\"><IconStyle><color>ff4489ec</color><scale>1.0</scale></IconStyle></Style>\n"
                "<Style id=\"raven\"><IconStyle><color>ff4444ef</color><scale>1.2</scale></IconStyle></Style>\n");

    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        for (int i = 0; i < gApp.detectionCount; ++i) {
            Detection& detection = gApp.detections[i];
            if (!detection.hasGPS) {
                continue;
            }
            resp->print("<Placemark>\n");
            resp->printf("<name>%s</name>\n", detection.mac);
            resp->printf("<styleUrl>#%s</styleUrl>\n", detection.isRaven ? "raven" : "det");
            resp->print("<description><![CDATA[");
            if (detection.name[0]) {
                resp->printf("<b>Name:</b> %s<br/>", detection.name);
            }
            resp->printf("<b>Method:</b> %s<br/><b>RSSI:</b> %d dBm<br/><b>Count:</b> %d<br/>",
                         detection.method,
                         detection.rssi,
                         detection.count);
            if (detection.isRaven) {
                resp->printf("<b>Raven FW:</b> %s<br/>", detection.ravenFW);
            }
            resp->printf("<b>Accuracy:</b> %.1f m", detection.gpsAcc);
            resp->print("]]></description>\n");
            resp->printf("<Point><coordinates>%.8f,%.8f,0</coordinates></Point>\n",
                         detection.gpsLon,
                         detection.gpsLat);
            resp->print("</Placemark>\n");
        }
        xSemaphoreGive(gApp.mutex);
    }

    resp->print("</Document>\n</kml>");
}

void writeRecentEventsJson(AsyncResponseStream* resp) {
    resp->print("[");
    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        bool first = true;
        for (uint8_t offset = 0; offset < gApp.recentEventCount; ++offset) {
            int index = static_cast<int>(gApp.recentEventHead) - 1 - static_cast<int>(offset);
            if (index < 0) {
                index += 16;
            }

            const RecentLogEvent& event = gApp.recentEvents[index];
            if (!first) {
                resp->print(",");
            }
            first = false;

            resp->printf(
                "{\"record_type\":\"%s\",\"label\":\"%s\",\"mac\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"method\":\"%s\",\"count\":%d,\"is_raven\":%s,\"raven_fw\":\"%s\",\"millis\":%lu,\"epoch\":%lu,\"iso8601\":\"%s\",\"time_source\":\"%s\",\"boot_count\":%lu",
                event.recordType,
                event.label,
                event.mac,
                event.name,
                event.rssi,
                event.method,
                event.count,
                event.isRaven ? "true" : "false",
                event.ravenFW,
                event.millisAtEvent,
                static_cast<unsigned long>(event.epoch),
                event.iso8601,
                event.timeSource,
                event.bootCount);
            if (event.hasGPS) {
                resp->printf(",\"gps\":{\"lat\":%.8f,\"lon\":%.8f,\"acc\":%.1f}}",
                             event.gpsLat,
                             event.gpsLon,
                             event.gpsAcc);
            } else {
                resp->print("}");
            }
        }
        xSemaphoreGive(gApp.mutex);
    }
    resp->print("]");
}

bool storageReady() {
    return gApp.spiffsReady;
}

bool sdReady() {
    return gApp.sdReady;
}

const char* sessionCsvPath() {
    return gApp.sessionCsvPath;
}

const char* sessionJsonlPath() {
    return gApp.sessionJsonlPath;
}

const char* dailyCsvPath() {
    return gApp.dailyCsvPath;
}

const char* dailyJsonlPath() {
    return gApp.dailyJsonlPath;
}

const char* wigleCsvPath() {
    return gApp.wigleLastClosedPath[0] ? gApp.wigleLastClosedPath : gApp.wigleCurrentPath;
}

size_t queuedEventCount() {
    return gLogQueue ? static_cast<size_t>(uxQueueMessagesWaiting(gLogQueue)) : 0U;
}

unsigned long droppedEventCount() {
    return gApp.logEventsDropped;
}

unsigned long writtenEventCount() {
    return gApp.logEventsWritten;
}

} // namespace oink::log
