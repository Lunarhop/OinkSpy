#include "oink_scan.h"

#include <ArduinoJson.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ctype.h>
#include <cstring>
#include <strings.h>
#include <vector>

#include "oink_board.h"
#include "oink_config.h"
#include "oink_log.h"
#include "oink_state.h"

namespace {

bool gScanningEnabled = true;
constexpr const char* kPrefsNamespace = "oinkspy";
constexpr const char* kPatternPrefsKey = "detect_db";
constexpr size_t kMaxStringEntries = 64;
constexpr size_t kMaxManufacturerEntries = 32;
constexpr size_t kMaxStringLength = 64;

struct StringPatternGroup {
    const char* jsonKey;
    const char* label;
    bool enabled = true;
    std::vector<String> values;

    StringPatternGroup(const char* key, const char* name) : jsonKey(key), label(name) {}
};

StringPatternGroup gFlockMacGroup{"macs", "Oink MAC Prefixes"};
StringPatternGroup gFlockManufacturerGroup{"macs_mfr", "Contract Mfr MACs"};
StringPatternGroup gSoundThinkingGroup{"macs_soundthinking", "SoundThinking MACs"};
StringPatternGroup gDeviceNameGroup{"names", "BLE Device Names"};
StringPatternGroup gRavenUuidGroup{"raven", "Raven UUIDs"};
bool gBleManufacturerEnabled = true;
std::vector<uint16_t> gBleManufacturerIds;

String normalizeToken(const String& rawValue) {
    String value = rawValue;
    value.trim();
    return value;
}

String normalizePrefix(const String& rawValue) {
    String compact;
    compact.reserve(rawValue.length());
    for (size_t i = 0; i < rawValue.length(); ++i) {
        char c = rawValue[i];
        if (isxdigit(static_cast<unsigned char>(c))) {
            compact += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
    }
    if (compact.length() < 6) {
        return String();
    }
    compact = compact.substring(0, 6);
    char formatted[9];
    snprintf(formatted,
             sizeof(formatted),
             "%c%c:%c%c:%c%c",
             compact[0],
             compact[1],
             compact[2],
             compact[3],
             compact[4],
             compact[5]);
    return String(formatted);
}

String normalizeUuid(const String& rawValue) {
    String value = normalizeToken(rawValue);
    value.toLowerCase();
    return value;
}

bool parseManufacturerId(const String& rawValue, uint16_t& outValue) {
    String value = normalizeToken(rawValue);
    value.replace("0x", "");
    value.replace("0X", "");
    value.trim();
    if (!value.length() || value.length() > 4) {
        return false;
    }

    char* end = nullptr;
    unsigned long parsed = strtoul(value.c_str(), &end, 16);
    if (!end || *end != '\0' || parsed > 0xFFFFUL) {
        return false;
    }
    outValue = static_cast<uint16_t>(parsed);
    return true;
}

void loadDefaultStringGroup(StringPatternGroup& group, const char* const* defaults, size_t count, String (*normalize)(const String&)) {
    group.enabled = true;
    group.values.clear();
    group.values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        String normalized = normalize(defaults[i]);
        if (normalized.length()) {
            group.values.push_back(normalized);
        }
    }
}

void loadProfileDefaults() {
    loadDefaultStringGroup(gFlockMacGroup, oink::config::kFlockMacPrefixes, oink::config::kFlockMacPrefixCount, normalizePrefix);
    loadDefaultStringGroup(gFlockManufacturerGroup, oink::config::kFlockManufacturerPrefixes, oink::config::kFlockManufacturerPrefixCount, normalizePrefix);
    loadDefaultStringGroup(gSoundThinkingGroup, oink::config::kSoundThinkingPrefixes, oink::config::kSoundThinkingPrefixCount, normalizePrefix);
    loadDefaultStringGroup(gDeviceNameGroup, oink::config::kDeviceNamePatterns, oink::config::kDeviceNamePatternCount, normalizeToken);
    loadDefaultStringGroup(gRavenUuidGroup, oink::config::kRavenServiceUuids, oink::config::kRavenServiceUuidCount, normalizeUuid);

    gBleManufacturerEnabled = true;
    gBleManufacturerIds.clear();
    gBleManufacturerIds.reserve(oink::config::kBleManufacturerIdCount);
    for (size_t i = 0; i < oink::config::kBleManufacturerIdCount; ++i) {
        gBleManufacturerIds.push_back(oink::config::kBleManufacturerIds[i]);
    }
}

bool checkPrefix(const char* value, const StringPatternGroup& group) {
    if (!group.enabled || !value || !value[0]) {
        return false;
    }
    for (const String& pattern : group.values) {
        if (strncasecmp(value, pattern.c_str(), 8) == 0) {
            return true;
        }
    }
    return false;
}

bool checkDeviceName(const char* name) {
    if (!gDeviceNameGroup.enabled || !name || !name[0]) {
        return false;
    }
    for (const String& pattern : gDeviceNameGroup.values) {
        if (strcasestr(name, pattern.c_str())) {
            return true;
        }
    }
    return false;
}

bool checkManufacturerId(uint16_t id) {
    if (!gBleManufacturerEnabled) {
        return false;
    }
    for (uint16_t pattern : gBleManufacturerIds) {
        if (pattern == id) {
            return true;
        }
    }
    return false;
}

bool checkRavenUuid(NimBLEAdvertisedDevice* device) {
    if (!device || !device->haveServiceUUID()) {
        return false;
    }

    int count = device->getServiceUUIDCount();
    if (count == 0) {
        return false;
    }

    for (int i = 0; i < count; ++i) {
        std::string uuid = device->getServiceUUID(i).toString();
        if (!gRavenUuidGroup.enabled) {
            return false;
        }
        for (const String& pattern : gRavenUuidGroup.values) {
            if (strcasecmp(uuid.c_str(), pattern.c_str()) == 0) {
                return true;
            }
        }
    }
    return false;
}

void writeQuotedString(Print& out, const String& value) {
    out.print('"');
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '"' || c == '\\') {
            out.print('\\');
        }
        out.print(c);
    }
    out.print('"');
}

void writeStringArray(Print& out, const std::vector<String>& values) {
    out.print('[');
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out.print(',');
        }
        writeQuotedString(out, values[i]);
    }
    out.print(']');
}

void writeManufacturerArray(Print& out) {
    out.print('[');
    for (size_t i = 0; i < gBleManufacturerIds.size(); ++i) {
        if (i > 0) {
            out.print(',');
        }
        char buffer[7];
        snprintf(buffer, sizeof(buffer), "0x%04X", gBleManufacturerIds[i]);
        writeQuotedString(out, String(buffer));
    }
    out.print(']');
}

bool readStringGroup(JsonObject root,
                     const char* key,
                     StringPatternGroup& group,
                     String (*normalize)(const String&),
                     size_t maxEntries,
                     String& error) {
    JsonObject obj = root[key];
    if (!obj) {
        error = String("Missing group: ") + key;
        return false;
    }
    group.enabled = obj["enabled"] | true;
    JsonArray values = obj["values"].as<JsonArray>();
    if (values.isNull()) {
        error = String("Missing values for group: ") + key;
        return false;
    }

    std::vector<String> updated;
    updated.reserve(maxEntries);
    for (JsonVariant entry : values) {
        if (updated.size() >= maxEntries) {
            error = String("Too many entries in group: ") + key;
            return false;
        }
        String normalized = normalize(entry.as<String>());
        if (!normalized.length()) {
            continue;
        }
        if (normalized.length() > kMaxStringLength) {
            error = String("Entry too long in group: ") + key;
            return false;
        }
        bool duplicate = false;
        for (const String& existing : updated) {
            if (existing.equalsIgnoreCase(normalized)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            updated.push_back(normalized);
        }
    }
    group.values = updated;
    return true;
}

bool readManufacturerGroup(JsonObject root, String& error) {
    JsonObject obj = root["mfr"];
    if (!obj) {
        error = "Missing group: mfr";
        return false;
    }
    gBleManufacturerEnabled = obj["enabled"] | true;
    JsonArray values = obj["values"].as<JsonArray>();
    if (values.isNull()) {
        error = "Missing values for group: mfr";
        return false;
    }

    std::vector<uint16_t> updated;
    updated.reserve(kMaxManufacturerEntries);
    for (JsonVariant entry : values) {
        if (updated.size() >= kMaxManufacturerEntries) {
            error = "Too many manufacturer IDs";
            return false;
        }
        uint16_t parsed = 0;
        if (!parseManufacturerId(entry.as<String>(), parsed)) {
            error = String("Invalid manufacturer ID: ") + entry.as<String>();
            return false;
        }
        bool duplicate = false;
        for (uint16_t existing : updated) {
            if (existing == parsed) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            updated.push_back(parsed);
        }
    }
    gBleManufacturerIds = updated;
    return true;
}

String profileJsonString() {
    String json;
    json.reserve(2048);
    json += "{\"macs\":{\"enabled\":";
    json += gFlockMacGroup.enabled ? "true" : "false";
    json += ",\"values\":";
    {
        class StringPrint : public Print {
        public:
            explicit StringPrint(String& target) : target_(target) {}
            size_t write(uint8_t c) override {
                target_ += static_cast<char>(c);
                return 1;
            }
        private:
            String& target_;
        } out(json);
        writeStringArray(out, gFlockMacGroup.values);
        json += "},\"macs_mfr\":{\"enabled\":";
        json += gFlockManufacturerGroup.enabled ? "true" : "false";
        json += ",\"values\":";
        writeStringArray(out, gFlockManufacturerGroup.values);
        json += "},\"macs_soundthinking\":{\"enabled\":";
        json += gSoundThinkingGroup.enabled ? "true" : "false";
        json += ",\"values\":";
        writeStringArray(out, gSoundThinkingGroup.values);
        json += "},\"names\":{\"enabled\":";
        json += gDeviceNameGroup.enabled ? "true" : "false";
        json += ",\"values\":";
        writeStringArray(out, gDeviceNameGroup.values);
        json += "},\"mfr\":{\"enabled\":";
        json += gBleManufacturerEnabled ? "true" : "false";
        json += ",\"values\":";
        writeManufacturerArray(out);
        json += "},\"raven\":{\"enabled\":";
        json += gRavenUuidGroup.enabled ? "true" : "false";
        json += ",\"values\":";
        writeStringArray(out, gRavenUuidGroup.values);
        json += "}}";
    }
    return json;
}

void saveProfileToPrefs() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        printf("[OINK-YOU] Detection profile save failed: prefs unavailable\n");
        return;
    }
    prefs.putString(kPatternPrefsKey, profileJsonString());
    prefs.end();
}

const char* estimateRavenFirmware(NimBLEAdvertisedDevice* device) {
    if (!device || !device->haveServiceUUID()) {
        return "?";
    }

    bool hasNewGps = false;
    bool hasOldLocation = false;
    bool hasPower = false;
    int count = device->getServiceUUIDCount();
    for (int i = 0; i < count; ++i) {
        std::string uuid = device->getServiceUUID(i).toString();
        if (strcasecmp(uuid.c_str(), oink::config::kRavenGpsService) == 0) {
            hasNewGps = true;
        }
        if (strcasecmp(uuid.c_str(), oink::config::kRavenOldLocationService) == 0) {
            hasOldLocation = true;
        }
        if (strcasecmp(uuid.c_str(), oink::config::kRavenPowerService) == 0) {
            hasPower = true;
        }
    }

    if (hasOldLocation && !hasNewGps) {
        return "1.1.x";
    }
    if (hasNewGps && !hasPower) {
        return "1.2.x";
    }
    if (hasNewGps && hasPower) {
        return "1.3.x";
    }
    return "?";
}

void attachGps(oink::Detection& detection) {
    if (!oink::scan::gpsIsFresh()) {
        return;
    }
    detection.hasGPS = true;
    detection.gpsLat = oink::gApp.gpsLat;
    detection.gpsLon = oink::gApp.gpsLon;
    detection.gpsAcc = oink::gApp.gpsAcc;
}

int addDetection(const char* mac, const char* name, int rssi, const char* method, bool isRaven, const char* ravenFW) {
    if (!oink::gApp.mutex || xSemaphoreTake(oink::gApp.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }

    for (int i = 0; i < oink::gApp.detectionCount; ++i) {
        if (strcasecmp(oink::gApp.detections[i].mac, mac) == 0) {
            oink::gApp.detections[i].count++;
            oink::gApp.detections[i].lastSeen = millis();
            oink::gApp.detections[i].rssi = rssi;
            if (name && name[0]) {
                strncpy(oink::gApp.detections[i].name, name, sizeof(oink::gApp.detections[i].name) - 1);
                oink::gApp.detections[i].name[sizeof(oink::gApp.detections[i].name) - 1] = '\0';
            }
            attachGps(oink::gApp.detections[i]);

            char notification[32];
            snprintf(notification, sizeof(notification), "%s %ddBm", method, rssi);
            oink::board::addNotification(notification);

            xSemaphoreGive(oink::gApp.mutex);
            return i;
        }
    }

    if (oink::gApp.detectionCount >= oink::config::kMaxDetections) {
        xSemaphoreGive(oink::gApp.mutex);
        return -1;
    }

    oink::Detection& detection = oink::gApp.detections[oink::gApp.detectionCount];
    memset(&detection, 0, sizeof(detection));
    strncpy(detection.mac, mac, sizeof(detection.mac) - 1);
    detection.mac[sizeof(detection.mac) - 1] = '\0';
    if (name) {
        for (int i = 0; i < static_cast<int>(sizeof(detection.name) - 1) && name[i]; ++i) {
            detection.name[i] = (name[i] == '"' || name[i] == '\\') ? '_' : name[i];
        }
    }
    detection.rssi = rssi;
    strncpy(detection.method, method, sizeof(detection.method) - 1);
    detection.method[sizeof(detection.method) - 1] = '\0';
    detection.firstSeen = millis();
    detection.lastSeen = detection.firstSeen;
    detection.count = 1;
    detection.isRaven = isRaven;
    strncpy(detection.ravenFW, ravenFW ? ravenFW : "", sizeof(detection.ravenFW) - 1);
    detection.ravenFW[sizeof(detection.ravenFW) - 1] = '\0';
    attachGps(detection);

    char notification[32];
    snprintf(notification, sizeof(notification), "%s %ddBm", method, rssi);
    oink::board::addNotification(notification);

    int idx = oink::gApp.detectionCount++;
    xSemaphoreGive(oink::gApp.mutex);
    return idx;
}

void sendBle(const char* data, size_t len) {
    if (!oink::gApp.bleClientConnected || !oink::gApp.txChar) {
        return;
    }

    uint16_t chunkSize = oink::gApp.negotiatedMtu > 3 ? oink::gApp.negotiatedMtu - 3 : 1;
    if (len <= chunkSize) {
        oink::gApp.txChar->setValue(reinterpret_cast<const uint8_t*>(data), len);
        oink::gApp.txChar->notify();
        return;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t sendSize = remaining < chunkSize ? remaining : chunkSize;
        oink::gApp.txChar->setValue(reinterpret_cast<const uint8_t*>(data + offset), sendSize);
        oink::gApp.txChar->notify();
        offset += sendSize;
    }
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc*) {
        oink::gApp.bleClientConnected = true;
        oink::gApp.companionChangePending = true;
    }

    void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) {
        oink::gApp.bleClientConnected = false;
        oink::gApp.negotiatedMtu = 23;
        NimBLEDevice::startAdvertising();
        oink::gApp.companionChangePending = true;
    }

    void onMTUChange(uint16_t mtu, ble_gap_conn_desc*) {
        oink::gApp.negotiatedMtu = mtu;
        printf("[OINK-YOU] MTU negotiated: %u\n", mtu);
    }
};

class BleCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) {
        if (!gScanningEnabled) {
            return;
        }

        NimBLEAddress addr = device->getAddress();
        std::string addrStr = addr.toString();
        char macPrefix[9];
        snprintf(macPrefix, sizeof(macPrefix), "%.8s", addrStr.c_str());

        int rssi = device->getRSSI();
        std::string name = device->haveName() ? device->getName() : "";

        bool detected = false;
        bool highConfidence = true;
        const char* method = "";
        bool isRaven = false;
        const char* ravenFW = "";

        if (checkPrefix(macPrefix, gFlockMacGroup)) {
            detected = true;
            method = "mac_prefix";
        } else if (checkPrefix(macPrefix, gSoundThinkingGroup)) {
            detected = true;
            method = "mac_prefix_soundthinking";
        } else if (checkPrefix(macPrefix, gFlockManufacturerGroup)) {
            detected = true;
            method = "mac_prefix_mfr";
            highConfidence = false;
        } else if (!name.empty() && checkDeviceName(name.c_str())) {
            detected = true;
            method = "device_name";
        } else {
            for (int i = 0; i < static_cast<int>(device->getManufacturerDataCount()); ++i) {
                std::string data = device->getManufacturerData(i);
                if (data.size() >= 2) {
                    uint16_t code = (static_cast<uint16_t>(static_cast<uint8_t>(data[1])) << 8) |
                                    static_cast<uint16_t>(static_cast<uint8_t>(data[0]));
                    if (checkManufacturerId(code)) {
                        detected = true;
                        method = "ble_mfr_id";
                        break;
                    }
                }
            }
        }

        if (!detected && checkRavenUuid(device)) {
            detected = true;
            method = "raven_uuid";
            isRaven = true;
            ravenFW = estimateRavenFirmware(device);
        }

        if (!detected) {
            return;
        }

        int idx = addDetection(addrStr.c_str(), name.c_str(), rssi, method, isRaven, ravenFW);
        int count = 0;
        oink::Detection wardriveDetection = {};
        bool haveWardriveDetection = false;
        if (idx >= 0 && oink::gApp.mutex && xSemaphoreTake(oink::gApp.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            count = oink::gApp.detections[idx].count;
            wardriveDetection = oink::gApp.detections[idx];
            haveWardriveDetection = true;
            xSemaphoreGive(oink::gApp.mutex);
        }
        printf("[OINK-YOU] DETECTED: %s %s RSSI:%d [%s] count:%d\n",
               addrStr.c_str(),
               name.c_str(),
               rssi,
               method,
               count);

        bool hasGps = oink::scan::gpsIsFresh();
        double gpsLat = hasGps ? oink::gApp.gpsLat : 0.0;
        double gpsLon = hasGps ? oink::gApp.gpsLon : 0.0;
        float gpsAcc = hasGps ? oink::gApp.gpsAcc : 0.0f;
        oink::log::appendDetectionEvent(addrStr.c_str(),
                                        name.c_str(),
                                        rssi,
                                        method,
                                        isRaven,
                                        isRaven ? ravenFW : "",
                                        hasGps,
                                        gpsLat,
                                        gpsLon,
                                        gpsAcc,
                                        count);
        if (haveWardriveDetection) {
            oink::log::appendWardriveDetection(wardriveDetection);
        }

        char gpsBuf[96] = "";
        if (hasGps) {
            snprintf(gpsBuf,
                     sizeof(gpsBuf),
                     ",\"gps\":{\"latitude\":%.8f,\"longitude\":%.8f,\"accuracy\":%.1f}",
                     gpsLat,
                     gpsLon,
                     gpsAcc);
        }

        char jsonBuf[512];
        int jsonLen = snprintf(
            jsonBuf,
            sizeof(jsonBuf),
            "{\"event\":\"detection\",\"detection_method\":\"%s\",\"protocol\":\"bluetooth_le\",\"mac_address\":\"%s\",\"device_name\":\"%s\",\"rssi\":%d,\"is_raven\":%s,\"raven_fw\":\"%s\"%s}",
            method,
            addrStr.c_str(),
            name.c_str(),
            rssi,
            isRaven ? "true" : "false",
            isRaven ? ravenFW : "",
            gpsBuf);
        printf("%s\n", jsonBuf);
        if (jsonLen > 0 && jsonLen < static_cast<int>(sizeof(jsonBuf) - 1)) {
            jsonBuf[jsonLen] = '\n';
            sendBle(jsonBuf, jsonLen + 1);
        }

        if (!oink::gApp.triggered && highConfidence) {
            oink::gApp.triggered = true;
            oink::board::detectBeep();
        }
        if (highConfidence) {
            oink::gApp.deviceInRange = true;
            oink::gApp.lastDetectionTime = millis();
            oink::gApp.lastHeartbeat = millis();
        }
    }
};

} // namespace

namespace oink::scan {

void loadProfile() {
    loadProfileDefaults();

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        printf("[OINK-YOU] Detection profile prefs unavailable; using defaults\n");
        return;
    }

    String saved = prefs.getString(kPatternPrefsKey, "");
    prefs.end();
    if (saved.isEmpty()) {
        return;
    }

    String error;
    if (!updateProfileFromJson(saved, error)) {
        printf("[OINK-YOU] Detection profile load failed: %s\n", error.c_str());
        loadProfileDefaults();
    } else {
        printf("[OINK-YOU] Detection profile loaded from NVS\n");
    }
}

void writeProfileJson(Print& out) {
    out.print(profileJsonString());
}

bool updateProfileFromJson(const String& json, String& error) {
    loadProfileDefaults();
    if (json.isEmpty()) {
        saveProfileToPrefs();
        return true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObject>()) {
        error = err ? err.c_str() : "root not object";
        loadProfileDefaults();
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root["reset_defaults"] | false) {
        saveProfileToPrefs();
        return true;
    }

    if (!readStringGroup(root, "macs", gFlockMacGroup, normalizePrefix, kMaxStringEntries, error) ||
        !readStringGroup(root, "macs_mfr", gFlockManufacturerGroup, normalizePrefix, kMaxStringEntries, error) ||
        !readStringGroup(root, "macs_soundthinking", gSoundThinkingGroup, normalizePrefix, kMaxStringEntries, error) ||
        !readStringGroup(root, "names", gDeviceNameGroup, normalizeToken, kMaxStringEntries, error) ||
        !readManufacturerGroup(root, error) ||
        !readStringGroup(root, "raven", gRavenUuidGroup, normalizeUuid, kMaxStringEntries, error)) {
        loadProfileDefaults();
        return false;
    }

    saveProfileToPrefs();
    return true;
}

void setupBle() {
    NimBLEDevice::init("oinkyou");
    NimBLEDevice::setMTU(512);

    gApp.bleScan = NimBLEDevice::getScan();
    gApp.bleScan->setAdvertisedDeviceCallbacks(new BleCallbacks());
    gApp.bleScan->setActiveScan(true);
    gApp.bleScan->setInterval(100);
    gApp.bleScan->setWindow(99);
    gApp.bleScan->start(gApp.bleScanDurationSec, false);
    gApp.lastBleScan = millis();
    printf("[OINK-YOU] BLE scanning ACTIVE\n");

    gApp.bleServer = NimBLEDevice::createServer();
    gApp.bleServer->setCallbacks(new ServerCallbacks());
    NimBLEService* service = gApp.bleServer->createService(config::kBleServiceUuid);
    gApp.txChar = service->createCharacteristic(config::kBleTxCharUuid, NIMBLE_PROPERTY::NOTIFY);
    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(config::kBleServiceUuid);
    advertising->setName("oinkyou");
    advertising->setScanResponse(true);
    advertising->start();
    printf("[OINK-YOU] BLE GATT server advertising (service %s)\n", config::kBleServiceUuid);
}

void onCompanionChange() {
    if (gApp.bleClientConnected || gApp.serialHostConnected) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        gApp.bleScanDurationSec = gApp.runtimeConfig.companionBleScanDurationSec;
        printf("[OINK-YOU] Companion mode: WiFi AP OFF, scan duration %ds\n", gApp.bleScanDurationSec);
    } else {
        WiFi.mode(WIFI_AP);
        delay(100);
        WiFi.softAP(gApp.runtimeConfig.apSsid, gApp.runtimeConfig.apPassword);
        gApp.bleScanDurationSec = gApp.runtimeConfig.standaloneBleScanDurationSec;
        printf("[OINK-YOU] Standalone mode: WiFi AP ON (%s), scan duration %ds\n", gApp.runtimeConfig.apSsid, gApp.bleScanDurationSec);
    }
}

void pollScan() {
    if (!gScanningEnabled) {
        if (gApp.bleScan && gApp.bleScan->isScanning()) {
            gApp.bleScan->stop();
            gApp.bleScan->clearResults();
        }
        return;
    }

    if (millis() - gApp.lastBleScan >= gApp.runtimeConfig.bleScanIntervalMs && !gApp.bleScan->isScanning()) {
        gApp.bleScan->start(gApp.bleScanDurationSec, false);
        gApp.lastBleScan = millis();
    }

    if (!gApp.bleScan->isScanning() && millis() - gApp.lastBleScan > static_cast<unsigned long>(gApp.bleScanDurationSec) * 1000UL) {
        gApp.bleScan->clearResults();
    }

    if (gApp.deviceInRange) {
        if (millis() - gApp.lastHeartbeat >= config::kHeartbeatIntervalMs) {
            oink::board::heartbeat();
            gApp.lastHeartbeat = millis();
        }
        if (millis() - gApp.lastDetectionTime >= config::kOutOfRangeMs) {
            printf("[OINK-YOU] Device out of range - stopping heartbeat\n");
            gApp.deviceInRange = false;
            gApp.triggered = false;
        }
    }
}

void setScanningEnabled(bool enabled) {
    gScanningEnabled = enabled;
    if (!gApp.bleScan) {
        return;
    }
    if (!enabled) {
        if (gApp.bleScan->isScanning()) {
            gApp.bleScan->stop();
        }
        gApp.bleScan->clearResults();
        gApp.deviceInRange = false;
        gApp.triggered = false;
        return;
    }
    if (!gApp.bleScan->isScanning()) {
        gApp.bleScan->start(gApp.bleScanDurationSec, false);
        gApp.lastBleScan = millis();
    }
}

bool isScanningEnabled() {
    return gScanningEnabled;
}

void updateGps(double lat, double lon, float acc) {
    gApp.gpsLat = lat;
    gApp.gpsLon = lon;
    gApp.gpsAcc = acc;
    gApp.gpsValid = true;
    gApp.gpsLastUpdate = millis();
}

bool gpsIsFresh() {
    return gApp.gpsValid && (millis() - gApp.gpsLastUpdate < config::kGpsStaleMs);
}

int countRavenDetections() {
    int raven = 0;
    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < gApp.detectionCount; ++i) {
            if (gApp.detections[i].isRaven) {
                ++raven;
            }
        }
        xSemaphoreGive(gApp.mutex);
    }
    return raven;
}

int countGpsTaggedDetections() {
    int tagged = 0;
    if (gApp.mutex && xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < gApp.detectionCount; ++i) {
            if (gApp.detections[i].hasGPS) {
                ++tagged;
            }
        }
        xSemaphoreGive(gApp.mutex);
    }
    return tagged;
}

void resetDetections() {
    if (!gApp.mutex || xSemaphoreTake(gApp.mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }
    gApp.detectionCount = 0;
    memset(gApp.detections, 0, sizeof(gApp.detections));
    gApp.triggered = false;
    gApp.deviceInRange = false;
    xSemaphoreGive(gApp.mutex);
}

} // namespace oink::scan
