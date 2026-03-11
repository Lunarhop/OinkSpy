#include "oink_settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>

#include "oink_config.h"
#include "oink_log.h"
#include "oink_state.h"

namespace {

constexpr const char* kPrefsNamespace = "oinkspy";
constexpr const char* kConfigKey = "config_json";
constexpr const char* kBootCountKey = "boot_count";
constexpr const char* kConfigPath = "config/oinkspy.json";

void loadDefaults() {
    memset(&oink::gApp.runtimeConfig, 0, sizeof(oink::gApp.runtimeConfig));
    strlcpy(oink::gApp.runtimeConfig.apSsid, oink::config::kApSsid, sizeof(oink::gApp.runtimeConfig.apSsid));
    strlcpy(oink::gApp.runtimeConfig.apPassword, oink::config::kApPassword, sizeof(oink::gApp.runtimeConfig.apPassword));
    strlcpy(oink::gApp.runtimeConfig.timezone, "UTC0", sizeof(oink::gApp.runtimeConfig.timezone));
    strlcpy(oink::gApp.runtimeConfig.ntpServer1, "pool.ntp.org", sizeof(oink::gApp.runtimeConfig.ntpServer1));
    strlcpy(oink::gApp.runtimeConfig.ntpServer2, "time.nist.gov", sizeof(oink::gApp.runtimeConfig.ntpServer2));
    oink::gApp.runtimeConfig.buzzerEnabled = true;
    oink::gApp.runtimeConfig.ntpEnabled = true;
    oink::gApp.runtimeConfig.bleScanIntervalMs = oink::config::kBleScanIntervalMs;
    oink::gApp.runtimeConfig.standaloneBleScanDurationSec = oink::config::kStandaloneBleScanDurationSec;
    oink::gApp.runtimeConfig.companionBleScanDurationSec = oink::config::kCompanionBleScanDurationSec;
    oink::gApp.runtimeConfig.saveIntervalMs = oink::config::kSaveIntervalMs;
    oink::gApp.runtimeConfig.serialTimeoutMs = oink::config::kSerialTimeoutMs;
    oink::gApp.runtimeConfig.sdLoggingEnabled = true;
    oink::gApp.runtimeConfig.sdJsonEnabled = true;
    oink::gApp.runtimeConfig.sdCsvEnabled = true;
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
    oink::gApp.runtimeConfig.bleScanIntervalMs = root["ble_scan_interval_ms"] | oink::gApp.runtimeConfig.bleScanIntervalMs;
    oink::gApp.runtimeConfig.standaloneBleScanDurationSec = root["standalone_scan_duration_sec"] | oink::gApp.runtimeConfig.standaloneBleScanDurationSec;
    oink::gApp.runtimeConfig.companionBleScanDurationSec = root["companion_scan_duration_sec"] | oink::gApp.runtimeConfig.companionBleScanDurationSec;
    oink::gApp.runtimeConfig.saveIntervalMs = root["save_interval_ms"] | oink::gApp.runtimeConfig.saveIntervalMs;
    oink::gApp.runtimeConfig.serialTimeoutMs = root["serial_timeout_ms"] | oink::gApp.runtimeConfig.serialTimeoutMs;
    oink::gApp.runtimeConfig.sdLoggingEnabled = root["sd_logging_enabled"] | oink::gApp.runtimeConfig.sdLoggingEnabled;
    oink::gApp.runtimeConfig.sdJsonEnabled = root["sd_json_enabled"] | oink::gApp.runtimeConfig.sdJsonEnabled;
    oink::gApp.runtimeConfig.sdCsvEnabled = root["sd_csv_enabled"] | oink::gApp.runtimeConfig.sdCsvEnabled;
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

    String nvsConfig = prefs.getString(kConfigKey, "");
    if (!nvsConfig.isEmpty()) {
        mergeConfigJson(nvsConfig);
        printf("[OINK-YOU] Loaded config fallback from NVS\n");
    }

    String sdConfig;
    if (oink::log::readSdTextFile(kConfigPath, sdConfig)) {
        mergeConfigJson(sdConfig);
        prefs.putString(kConfigKey, sdConfig);
        printf("[OINK-YOU] Loaded config from SD: %s\n", kConfigPath);
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

} // namespace oink::settings

