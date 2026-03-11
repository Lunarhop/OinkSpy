#include "oink_log.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <SdFat.h>
#include <cstring>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "oink_config.h"
#include "oink_settings.h"
#include "oink_state.h"
#include "oink_time.h"

namespace {

SdFs gSd;
SPIClass gSdSpi(FSPI);

constexpr const char* kLogsDir = "logs";
constexpr const char* kSessionsDir = "logs/sessions";
constexpr const char* kDailyDir = "logs/daily";
constexpr const char* kUnsyncedDayToken = "unsynced";
constexpr const char* kFirmwareVersion = "0.2.0-dev";
constexpr size_t kLogQueueLength = 48;
constexpr uint32_t kLogTaskStack = 6144;

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

    gApp.lastSaveCount = gApp.detectionCount;
    printf("[OINK-YOU] Session saved: %d detections\n", gApp.detectionCount);
    xSemaphoreGive(gApp.mutex);
}

void pollAutoSave() {
    if (!gApp.spiffsReady) {
        return;
    }

    unsigned long saveIntervalMs = gApp.runtimeConfig.saveIntervalMs > 0 ? gApp.runtimeConfig.saveIntervalMs : config::kSaveIntervalMs;
    if (millis() - gApp.lastSave >= saveIntervalMs) {
        if (gApp.detectionCount > 0 && gApp.detectionCount != gApp.lastSaveCount) {
            saveSession();
        }
        gApp.lastSave = millis();
        return;
    }

    if (gApp.detectionCount > 0 && gApp.lastSaveCount == 0 && millis() - gApp.lastSave >= config::kInitialSaveDelayMs) {
        saveSession();
        gApp.lastSave = millis();
    }
}

void prepareSdLogs() {
    ensureSdLogFiles();
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
