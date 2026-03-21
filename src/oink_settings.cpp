#include "oink_settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>

#include "oink_config.h"
#include "oink_log.h"
#include "oink_scan.h"
#include "oink_state.h"

namespace {

constexpr const char* kPrefsNamespace = "oinkspy";
constexpr const char* kConfigKey = "config_json";
constexpr const char* kBootCountKey = "boot_count";
constexpr const char* kClientWifiSsidKey = "sta_ssid";
constexpr const char* kClientWifiPasswordKey = "sta_pass";
constexpr const char* kWigleApiNameKey = "wig_name";
constexpr const char* kWigleApiTokenKey = "wig_token";
constexpr const char* kConfigPath = "config/oinkspy.json";
constexpr uint16_t kWardriveFlushIntervalDefault = 60;
constexpr uint16_t kWardriveFlushIntervalMin = 15;
constexpr uint16_t kWardriveFlushIntervalMax = 600;
constexpr uint16_t kWardriveRotationDefault = 100;
constexpr uint16_t kWardriveRotationMin = 16;
constexpr uint16_t kWardriveRotationMax = 1024;
constexpr uint16_t kWardriveDedupDefault = 300;
constexpr uint16_t kWardriveDedupMin = 0;
constexpr uint16_t kWardriveDedupMax = 3600;
constexpr uint16_t kWardriveMoveThresholdDefault = 50;
constexpr uint16_t kWardriveMoveThresholdMin = 0;
constexpr uint16_t kWardriveMoveThresholdMax = 1000;

uint16_t clampUint16(unsigned long value, uint16_t minValue, uint16_t maxValue, uint16_t defaultValue) {
    if (value < minValue || value > maxValue) {
        return defaultValue;
    }
    return static_cast<uint16_t>(value);
}

void loadWardriveDefaults() {
    oink::gApp.runtimeConfig.wardrive.enabled = false;
    oink::gApp.runtimeConfig.wardrive.flushIntervalSeconds = kWardriveFlushIntervalDefault;
    oink::gApp.runtimeConfig.wardrive.fileRotationMb = kWardriveRotationDefault;
    oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds = kWardriveDedupDefault;
    oink::gApp.runtimeConfig.wardrive.moveThresholdMeters = kWardriveMoveThresholdDefault;
    strlcpy(oink::gApp.runtimeConfig.wardrive.logFormat, "csv", sizeof(oink::gApp.runtimeConfig.wardrive.logFormat));
}

void clampWardriveConfig() {
    oink::gApp.runtimeConfig.wardrive.flushIntervalSeconds = clampUint16(
        oink::gApp.runtimeConfig.wardrive.flushIntervalSeconds,
        kWardriveFlushIntervalMin,
        kWardriveFlushIntervalMax,
        kWardriveFlushIntervalDefault);
    oink::gApp.runtimeConfig.wardrive.fileRotationMb = clampUint16(
        oink::gApp.runtimeConfig.wardrive.fileRotationMb,
        kWardriveRotationMin,
        kWardriveRotationMax,
        kWardriveRotationDefault);
    oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds = clampUint16(
        oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds,
        kWardriveDedupMin,
        kWardriveDedupMax,
        kWardriveDedupDefault);
    oink::gApp.runtimeConfig.wardrive.moveThresholdMeters = clampUint16(
        oink::gApp.runtimeConfig.wardrive.moveThresholdMeters,
        kWardriveMoveThresholdMin,
        kWardriveMoveThresholdMax,
        kWardriveMoveThresholdDefault);
    if (strcasecmp(oink::gApp.runtimeConfig.wardrive.logFormat, "csv") != 0) {
        strlcpy(oink::gApp.runtimeConfig.wardrive.logFormat, "csv", sizeof(oink::gApp.runtimeConfig.wardrive.logFormat));
    }
}

void mergeWardriveConfig(JsonObject wardriveRoot) {
    if (!wardriveRoot) {
        return;
    }

    oink::gApp.runtimeConfig.wardrive.enabled =
        wardriveRoot["enabled"] | oink::gApp.runtimeConfig.wardrive.enabled;
    oink::gApp.runtimeConfig.wardrive.flushIntervalSeconds =
        wardriveRoot["flush_interval_seconds"] | oink::gApp.runtimeConfig.wardrive.flushIntervalSeconds;
    oink::gApp.runtimeConfig.wardrive.fileRotationMb =
        wardriveRoot["file_rotation_mb"] | oink::gApp.runtimeConfig.wardrive.fileRotationMb;
    oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds =
        wardriveRoot["dedup_window_seconds"] | oink::gApp.runtimeConfig.wardrive.dedupWindowSeconds;
    oink::gApp.runtimeConfig.wardrive.moveThresholdMeters =
        wardriveRoot["move_threshold_meters"] | oink::gApp.runtimeConfig.wardrive.moveThresholdMeters;
    strlcpy(oink::gApp.runtimeConfig.wardrive.logFormat,
            wardriveRoot["log_format"] | oink::gApp.runtimeConfig.wardrive.logFormat,
            sizeof(oink::gApp.runtimeConfig.wardrive.logFormat));
    clampWardriveConfig();
}

void loadDefaults() {
    memset(&oink::gApp.runtimeConfig, 0, sizeof(oink::gApp.runtimeConfig));
    oink::gApp.clientWifiConfigured = false;
    oink::gApp.clientWifiConnecting = false;
    oink::gApp.clientWifiConnected = false;
    oink::gApp.clientWifiLastAttemptMs = 0;
    oink::gApp.clientWifiSsid[0] = '\0';
    oink::gApp.clientWifiPassword[0] = '\0';
    strlcpy(oink::gApp.clientWifiStatus, "not configured", sizeof(oink::gApp.clientWifiStatus));
    oink::gApp.clientWifiIp[0] = '\0';
    oink::gApp.wigleConfigured = false;
    oink::gApp.wigleUploadRequested = false;
    oink::gApp.wigleUploadInProgress = false;
    oink::gApp.wigleUploadStatusCode = 0;
    oink::gApp.wigleUploadLastMs = 0;
    oink::gApp.wigleApiName[0] = '\0';
    oink::gApp.wigleApiToken[0] = '\0';
    strlcpy(oink::gApp.wigleUploadStatus, "idle", sizeof(oink::gApp.wigleUploadStatus));
    oink::gApp.wigleUploadCandidatePath[0] = '\0';
    oink::gApp.wigleLastUploadPath[0] = '\0';
    oink::gApp.wigleLastClosedPath[0] = '\0';
    oink::gApp.wigleLastClosedFileBytes = 0;
    strlcpy(oink::gApp.runtimeConfig.apSsid, oink::config::kApSsid, sizeof(oink::gApp.runtimeConfig.apSsid));
    strlcpy(oink::gApp.runtimeConfig.apPassword, oink::config::kApPassword, sizeof(oink::gApp.runtimeConfig.apPassword));
    strlcpy(oink::gApp.runtimeConfig.timezone, "UTC0", sizeof(oink::gApp.runtimeConfig.timezone));
    strlcpy(oink::gApp.runtimeConfig.ntpServer1, "pool.ntp.org", sizeof(oink::gApp.runtimeConfig.ntpServer1));
    strlcpy(oink::gApp.runtimeConfig.ntpServer2, "time.nist.gov", sizeof(oink::gApp.runtimeConfig.ntpServer2));
    oink::gApp.runtimeConfig.buzzerEnabled = true;
    oink::gApp.runtimeConfig.ntpEnabled = true;
    oink::gApp.runtimeConfig.rtcEnabled = true;
    oink::gApp.runtimeConfig.otaEnabled = true;
    oink::gApp.runtimeConfig.bleScanIntervalMs = oink::config::kBleScanIntervalMs;
    oink::gApp.runtimeConfig.standaloneBleScanDurationSec = oink::config::kStandaloneBleScanDurationSec;
    oink::gApp.runtimeConfig.companionBleScanDurationSec = oink::config::kCompanionBleScanDurationSec;
    oink::gApp.runtimeConfig.saveIntervalMs = oink::config::kSaveIntervalMs;
    oink::gApp.runtimeConfig.serialTimeoutMs = oink::config::kSerialTimeoutMs;
    oink::gApp.runtimeConfig.sdLoggingEnabled = true;
    oink::gApp.runtimeConfig.sdJsonEnabled = true;
    oink::gApp.runtimeConfig.sdCsvEnabled = true;
#if OINK_FEATURE_GNSS
    oink::gApp.runtimeConfig.gnssEnabled = true;
#else
    oink::gApp.runtimeConfig.gnssEnabled = false;
#endif
    loadWardriveDefaults();
}

void loadSecretsFromPrefs(Preferences& prefs) {
    String staSsid = prefs.getString(kClientWifiSsidKey, "");
    String staPass = prefs.getString(kClientWifiPasswordKey, "");
    if (!staSsid.isEmpty() && !staPass.isEmpty()) {
        strlcpy(oink::gApp.clientWifiSsid, staSsid.c_str(), sizeof(oink::gApp.clientWifiSsid));
        strlcpy(oink::gApp.clientWifiPassword, staPass.c_str(), sizeof(oink::gApp.clientWifiPassword));
        oink::gApp.clientWifiConfigured = true;
        strlcpy(oink::gApp.clientWifiStatus, "stored in memory", sizeof(oink::gApp.clientWifiStatus));
    }

    String wigleName = prefs.getString(kWigleApiNameKey, "");
    String wigleToken = prefs.getString(kWigleApiTokenKey, "");
    if (!wigleName.isEmpty() && !wigleToken.isEmpty()) {
        strlcpy(oink::gApp.wigleApiName, wigleName.c_str(), sizeof(oink::gApp.wigleApiName));
        strlcpy(oink::gApp.wigleApiToken, wigleToken.c_str(), sizeof(oink::gApp.wigleApiToken));
        oink::gApp.wigleConfigured = true;
    }
}

bool importSecretsFromJson(String& jsonText, Preferences& prefs, bool& scrubbed) {
    scrubbed = false;
    if (jsonText.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonText);
    if (err || !doc.is<JsonObject>()) {
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    JsonObject clientWifi = root["client_wifi"].as<JsonObject>();
    if (clientWifi) {
        String ssid = clientWifi["ssid"] | "";
        String password = clientWifi["password"] | "";
        if (!ssid.isEmpty() && !password.isEmpty()) {
            prefs.putString(kClientWifiSsidKey, ssid);
            prefs.putString(kClientWifiPasswordKey, password);
            strlcpy(oink::gApp.clientWifiSsid, ssid.c_str(), sizeof(oink::gApp.clientWifiSsid));
            strlcpy(oink::gApp.clientWifiPassword, password.c_str(), sizeof(oink::gApp.clientWifiPassword));
            oink::gApp.clientWifiConfigured = true;
            strlcpy(oink::gApp.clientWifiStatus, "imported from SD, scrubbed", sizeof(oink::gApp.clientWifiStatus));
        }
        root.remove("client_wifi");
        scrubbed = true;
    }

    JsonObject wigle = root["wigle"].as<JsonObject>();
    if (wigle) {
        String apiName = wigle["api_name"] | "";
        String apiToken = wigle["api_token"] | "";
        if (!apiName.isEmpty() && !apiToken.isEmpty()) {
            prefs.putString(kWigleApiNameKey, apiName);
            prefs.putString(kWigleApiTokenKey, apiToken);
            strlcpy(oink::gApp.wigleApiName, apiName.c_str(), sizeof(oink::gApp.wigleApiName));
            strlcpy(oink::gApp.wigleApiToken, apiToken.c_str(), sizeof(oink::gApp.wigleApiToken));
            oink::gApp.wigleConfigured = true;
        }
        root.remove("wigle");
        scrubbed = true;
    }

    if (!scrubbed) {
        return false;
    }

    jsonText = "";
    serializeJsonPretty(doc, jsonText);
    jsonText += '\n';
    return true;
}

bool storeSecretPair(const char* keyA,
                     const char* valueA,
                     const char* keyB,
                     const char* valueB,
                     String& error) {
    error = "";
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        error = "Preferences unavailable";
        return false;
    }
    prefs.putString(keyA, valueA ? valueA : "");
    prefs.putString(keyB, valueB ? valueB : "");
    prefs.end();
    return true;
}

void mergeConfigJson(const String& jsonText) {
    if (jsonText.isEmpty()) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonText);
    if (err || !doc.is<JsonObject>()) {
        printf("[OINK-YOU] Config parse failed: %s\n", err ? err.c_str() : "root not object");
        return;
    }

    JsonObject root = doc.as<JsonObject>();
    strlcpy(oink::gApp.runtimeConfig.apSsid,
            root["ap_ssid"] | oink::gApp.runtimeConfig.apSsid,
            sizeof(oink::gApp.runtimeConfig.apSsid));
    strlcpy(oink::gApp.runtimeConfig.apPassword,
            root["ap_password"] | oink::gApp.runtimeConfig.apPassword,
            sizeof(oink::gApp.runtimeConfig.apPassword));
    strlcpy(oink::gApp.runtimeConfig.timezone,
            root["timezone"] | oink::gApp.runtimeConfig.timezone,
            sizeof(oink::gApp.runtimeConfig.timezone));
    strlcpy(oink::gApp.runtimeConfig.ntpServer1,
            root["ntp_server_1"] | oink::gApp.runtimeConfig.ntpServer1,
            sizeof(oink::gApp.runtimeConfig.ntpServer1));
    strlcpy(oink::gApp.runtimeConfig.ntpServer2,
            root["ntp_server_2"] | oink::gApp.runtimeConfig.ntpServer2,
            sizeof(oink::gApp.runtimeConfig.ntpServer2));
    oink::gApp.runtimeConfig.buzzerEnabled = root["buzzer_enabled"] | oink::gApp.runtimeConfig.buzzerEnabled;
    oink::gApp.runtimeConfig.ntpEnabled = root["ntp_enabled"] | oink::gApp.runtimeConfig.ntpEnabled;
    oink::gApp.runtimeConfig.rtcEnabled = root["rtc_enabled"] | oink::gApp.runtimeConfig.rtcEnabled;
    oink::gApp.runtimeConfig.otaEnabled = root["ota_enabled"] | oink::gApp.runtimeConfig.otaEnabled;
    oink::gApp.runtimeConfig.bleScanIntervalMs = root["ble_scan_interval_ms"] | oink::gApp.runtimeConfig.bleScanIntervalMs;
    oink::gApp.runtimeConfig.standaloneBleScanDurationSec = root["standalone_scan_duration_sec"] | oink::gApp.runtimeConfig.standaloneBleScanDurationSec;
    oink::gApp.runtimeConfig.companionBleScanDurationSec = root["companion_scan_duration_sec"] | oink::gApp.runtimeConfig.companionBleScanDurationSec;
    oink::gApp.runtimeConfig.saveIntervalMs = root["save_interval_ms"] | oink::gApp.runtimeConfig.saveIntervalMs;
    oink::gApp.runtimeConfig.serialTimeoutMs = root["serial_timeout_ms"] | oink::gApp.runtimeConfig.serialTimeoutMs;
    oink::gApp.runtimeConfig.sdLoggingEnabled = root["sd_logging_enabled"] | oink::gApp.runtimeConfig.sdLoggingEnabled;
    oink::gApp.runtimeConfig.sdJsonEnabled = root["sd_json_enabled"] | oink::gApp.runtimeConfig.sdJsonEnabled;
    oink::gApp.runtimeConfig.sdCsvEnabled = root["sd_csv_enabled"] | oink::gApp.runtimeConfig.sdCsvEnabled;
    oink::gApp.runtimeConfig.gnssEnabled = root["gnss_enabled"] | oink::gApp.runtimeConfig.gnssEnabled;
    mergeWardriveConfig(root["wardrive"].as<JsonObject>());
    clampWardriveConfig();
}

void populateDeviceId() {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(oink::gApp.deviceId,
             sizeof(oink::gApp.deviceId),
             "%04X%08X",
             static_cast<unsigned int>((chipId >> 32) & 0xFFFF),
             static_cast<unsigned int>(chipId & 0xFFFFFFFF));
}

} // namespace

namespace oink::settings {

void load() {
    loadDefaults();
    populateDeviceId();

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        printf("[OINK-YOU] Preferences init failed; using defaults\n");
        gApp.bootCount = 1;
        gApp.buzzerOn = gApp.runtimeConfig.buzzerEnabled;
        gApp.bleScanDurationSec = gApp.runtimeConfig.standaloneBleScanDurationSec;
        return;
    }

    gApp.bootCount = prefs.getULong(kBootCountKey, 0) + 1;
    prefs.putULong(kBootCountKey, gApp.bootCount);
    loadSecretsFromPrefs(prefs);

    String nvsConfig = prefs.getString(kConfigKey, "");
    if (!nvsConfig.isEmpty()) {
        mergeConfigJson(nvsConfig);
        printf("[OINK-YOU] Loaded config fallback from NVS\n");
    }

    String sdConfig;
    if (oink::log::readSdTextFile(kConfigPath, sdConfig)) {
        bool scrubbed = false;
        importSecretsFromJson(sdConfig, prefs, scrubbed);
        mergeConfigJson(sdConfig);
        prefs.putString(kConfigKey, sdConfig);
        printf("[OINK-YOU] Loaded config from SD: %s\n", kConfigPath);
        if (scrubbed) {
            if (oink::log::writeSdTextFile(kConfigPath, sdConfig)) {
                printf("[OINK-YOU] Scrubbed imported client WiFi / WiGLE secrets from SD config\n");
            } else {
                printf("[OINK-YOU] Warning: imported secrets but failed to scrub SD config\n");
            }
        }
    }

    prefs.end();

    gApp.buzzerOn = gApp.runtimeConfig.buzzerEnabled;
    gApp.bleScanDurationSec = gApp.runtimeConfig.standaloneBleScanDurationSec;
    printf("[OINK-YOU] Device ID: %s, boot #%lu\n", gApp.deviceId, gApp.bootCount);
}

const char* deviceId() {
    return gApp.deviceId;
}

unsigned long bootCount() {
    return gApp.bootCount;
}

String serializeRuntimeConfigJson() {
    JsonDocument doc;
    doc["ap_ssid"] = gApp.runtimeConfig.apSsid;
    doc["ap_password"] = gApp.runtimeConfig.apPassword;
    doc["timezone"] = gApp.runtimeConfig.timezone;
    doc["ntp_enabled"] = gApp.runtimeConfig.ntpEnabled;
    doc["rtc_enabled"] = gApp.runtimeConfig.rtcEnabled;
    doc["ntp_server_1"] = gApp.runtimeConfig.ntpServer1;
    doc["ntp_server_2"] = gApp.runtimeConfig.ntpServer2;
    doc["ota_enabled"] = gApp.runtimeConfig.otaEnabled;
    doc["buzzer_enabled"] = gApp.runtimeConfig.buzzerEnabled;
    doc["ble_scan_interval_ms"] = gApp.runtimeConfig.bleScanIntervalMs;
    doc["standalone_scan_duration_sec"] = gApp.runtimeConfig.standaloneBleScanDurationSec;
    doc["companion_scan_duration_sec"] = gApp.runtimeConfig.companionBleScanDurationSec;
    doc["save_interval_ms"] = gApp.runtimeConfig.saveIntervalMs;
    doc["serial_timeout_ms"] = gApp.runtimeConfig.serialTimeoutMs;
    doc["sd_logging_enabled"] = gApp.runtimeConfig.sdLoggingEnabled;
    doc["sd_json_enabled"] = gApp.runtimeConfig.sdJsonEnabled;
    doc["sd_csv_enabled"] = gApp.runtimeConfig.sdCsvEnabled;
    doc["gnss_enabled"] = gApp.runtimeConfig.gnssEnabled;

    JsonObject wardrive = doc["wardrive"].to<JsonObject>();
    wardrive["enabled"] = gApp.runtimeConfig.wardrive.enabled;
    wardrive["flush_interval_seconds"] = gApp.runtimeConfig.wardrive.flushIntervalSeconds;
    wardrive["file_rotation_mb"] = gApp.runtimeConfig.wardrive.fileRotationMb;
    wardrive["dedup_window_seconds"] = gApp.runtimeConfig.wardrive.dedupWindowSeconds;
    wardrive["move_threshold_meters"] = gApp.runtimeConfig.wardrive.moveThresholdMeters;
    wardrive["log_format"] = gApp.runtimeConfig.wardrive.logFormat;

    String json;
    serializeJsonPretty(doc, json);
    json += '\n';
    return json;
}

bool persistRuntimeConfig(bool preferSd, String& error) {
    error = "";

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        error = "Preferences unavailable";
        return false;
    }

    String json = serializeRuntimeConfigJson();
    prefs.putString(kConfigKey, json);
    prefs.end();

    if (preferSd) {
        if (oink::log::sdReady()) {
            if (!oink::log::writeSdTextFile(kConfigPath, json)) {
                error = "Saved in NVS but failed to update SD config";
                return false;
            }
        } else {
            error = "Saved in NVS; SD config unavailable";
        }
    }

    return true;
}

bool setWardriveEnabled(bool enabled, bool persist, String& error) {
    error = "";
    if (enabled && !gApp.runtimeConfig.sdLoggingEnabled) {
        error = "Wardrive Mode requires sd_logging_enabled";
        return false;
    }
    if (enabled && !oink::log::sdReady()) {
        error = "Wardrive Mode requires a mounted SD card";
        return false;
    }

    bool previous = gApp.runtimeConfig.wardrive.enabled;
    gApp.runtimeConfig.wardrive.enabled = enabled;

    String persistMessage;
    if (persist && !persistRuntimeConfig(true, persistMessage)) {
        gApp.runtimeConfig.wardrive.enabled = previous;
        error = persistMessage.length() ? persistMessage : "Failed to persist Wardrive Mode";
        return false;
    }

    oink::scan::onCompanionChange();

    if (gApp.runtimeConfig.wardrive.enabled) {
        oink::log::beginWardriveSession();
    } else {
        oink::log::endWardriveSession();
    }

    if (!persistMessage.isEmpty()) {
        error = persistMessage;
    }
    return true;
}

void writeWardriveConfigJson(Print& out) {
    out.printf("{\"enabled\":%s,\"flush_interval_seconds\":%u,\"file_rotation_mb\":%u,\"dedup_window_seconds\":%u,\"move_threshold_meters\":%u,\"log_format\":\"%s\"}",
               gApp.runtimeConfig.wardrive.enabled ? "true" : "false",
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.flushIntervalSeconds),
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.fileRotationMb),
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.dedupWindowSeconds),
               static_cast<unsigned>(gApp.runtimeConfig.wardrive.moveThresholdMeters),
               gApp.runtimeConfig.wardrive.logFormat);
}

bool hasClientWifiCredentials() {
    return gApp.clientWifiConfigured && gApp.clientWifiSsid[0] && gApp.clientWifiPassword[0];
}

const char* clientWifiSsid() {
    return gApp.clientWifiSsid;
}

const char* clientWifiPassword() {
    return gApp.clientWifiPassword;
}

bool setClientWifiCredentials(const char* ssid, const char* password, String& error) {
    error = "";
    if (!ssid || !ssid[0] || !password || !password[0]) {
        error = "SSID and password are required";
        return false;
    }
    if (strlen(ssid) >= sizeof(gApp.clientWifiSsid) || strlen(password) >= sizeof(gApp.clientWifiPassword)) {
        error = "Client WiFi credentials are too long";
        return false;
    }
    if (!storeSecretPair(kClientWifiSsidKey, ssid, kClientWifiPasswordKey, password, error)) {
        return false;
    }
    strlcpy(gApp.clientWifiSsid, ssid, sizeof(gApp.clientWifiSsid));
    strlcpy(gApp.clientWifiPassword, password, sizeof(gApp.clientWifiPassword));
    gApp.clientWifiConfigured = true;
    gApp.clientWifiConnecting = false;
    gApp.clientWifiConnected = false;
    gApp.clientWifiLastAttemptMs = 0;
    gApp.clientWifiIp[0] = '\0';
    strlcpy(gApp.clientWifiStatus, "stored in memory", sizeof(gApp.clientWifiStatus));
    oink::scan::onCompanionChange();
    return true;
}

bool clearClientWifiCredentials(String& error) {
    error = "";
    if (!storeSecretPair(kClientWifiSsidKey, "", kClientWifiPasswordKey, "", error)) {
        return false;
    }
    gApp.clientWifiConfigured = false;
    gApp.clientWifiConnecting = false;
    gApp.clientWifiConnected = false;
    gApp.clientWifiLastAttemptMs = 0;
    gApp.clientWifiSsid[0] = '\0';
    gApp.clientWifiPassword[0] = '\0';
    gApp.clientWifiIp[0] = '\0';
    strlcpy(gApp.clientWifiStatus, "not configured", sizeof(gApp.clientWifiStatus));
    oink::scan::onCompanionChange();
    return true;
}

bool hasWigleCredentials() {
    return gApp.wigleConfigured && gApp.wigleApiName[0] && gApp.wigleApiToken[0];
}

const char* wigleApiName() {
    return gApp.wigleApiName;
}

const char* wigleApiToken() {
    return gApp.wigleApiToken;
}

bool setWigleCredentials(const char* apiName, const char* apiToken, String& error) {
    error = "";
    if (!apiName || !apiName[0] || !apiToken || !apiToken[0]) {
        error = "WiGLE API name and token are required";
        return false;
    }
    if (strlen(apiName) >= sizeof(gApp.wigleApiName) || strlen(apiToken) >= sizeof(gApp.wigleApiToken)) {
        error = "WiGLE credentials are too long";
        return false;
    }
    if (!storeSecretPair(kWigleApiNameKey, apiName, kWigleApiTokenKey, apiToken, error)) {
        return false;
    }
    strlcpy(gApp.wigleApiName, apiName, sizeof(gApp.wigleApiName));
    strlcpy(gApp.wigleApiToken, apiToken, sizeof(gApp.wigleApiToken));
    gApp.wigleConfigured = true;
    strlcpy(gApp.wigleUploadStatus, "credentials stored", sizeof(gApp.wigleUploadStatus));
    return true;
}

bool clearWigleCredentials(String& error) {
    error = "";
    if (!storeSecretPair(kWigleApiNameKey, "", kWigleApiTokenKey, "", error)) {
        return false;
    }
    gApp.wigleConfigured = false;
    gApp.wigleApiName[0] = '\0';
    gApp.wigleApiToken[0] = '\0';
    strlcpy(gApp.wigleUploadStatus, "credentials cleared", sizeof(gApp.wigleUploadStatus));
    return true;
}

bool handleCommand(const char* line, Stream& out) {
    if (!line) {
        return false;
    }

    char buffer[64];
    strlcpy(buffer, line, sizeof(buffer));
    char* token = strtok(buffer, " ");
    if (!token || strcasecmp(token, "wardrive") != 0) {
        return false;
    }

    char* command = strtok(nullptr, " ");
    if (!command || strcasecmp(command, "status") == 0) {
        oink::log::WardriveStatus status = oink::log::wardriveStatus();
        out.printf("Wardrive: enabled=%s active=%s flush=%us rotate=%uMB dedup=%us move=%um format=%s path=%s message=%s\n",
                   status.enabled ? "yes" : "no",
                   status.active ? "yes" : "no",
                   static_cast<unsigned>(status.flushIntervalSeconds),
                   static_cast<unsigned>(status.fileRotationMb),
                   static_cast<unsigned>(status.dedupWindowSeconds),
                   static_cast<unsigned>(gApp.runtimeConfig.wardrive.moveThresholdMeters),
                   status.logFormat,
                   status.currentPath[0] ? status.currentPath : "-",
                   status.message);
        return true;
    }
    if (strcasecmp(command, "debug") == 0) {
        oink::log::printWardriveDebug(out);
        return true;
    }

    bool enable = false;
    if (strcasecmp(command, "on") == 0) {
        enable = true;
    } else if (strcasecmp(command, "off") == 0) {
        enable = false;
    } else if (strcasecmp(command, "toggle") == 0) {
        enable = !gApp.runtimeConfig.wardrive.enabled;
    } else {
        out.println("Wardrive commands: wardrive status|debug|on|off|toggle");
        return true;
    }

    String error;
    if (!setWardriveEnabled(enable, true, error)) {
        out.printf("Wardrive update failed: %s\n", error.c_str());
        return true;
    }

    out.printf("Wardrive Mode %s\n", gApp.runtimeConfig.wardrive.enabled ? "enabled" : "disabled");
    if (!error.isEmpty()) {
        out.printf("Wardrive note: %s\n", error.c_str());
    }
    return true;
}

} // namespace oink::settings

