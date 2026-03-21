#include "oink_uplink.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <mbedtls/base64.h>

#include "oink_log.h"
#include "oink_scan.h"
#include "oink_settings.h"
#include "oink_state.h"

namespace {

constexpr unsigned long kClientWifiRetryMs = 15000UL;
constexpr const char* kWigleUploadHost = "api.wigle.net";
// Inferred from WiGLE's published API examples and upload documentation.
constexpr const char* kWigleUploadPath = "/api/v2/file/upload";
constexpr uint16_t kWigleUploadPort = 443;
TaskHandle_t gWigleUploadTask = nullptr;

String maskSecretHint(const char* value) {
    if (!value || !value[0]) {
        return String();
    }
    size_t len = strlen(value);
    if (len <= 4) {
        return String("***");
    }
    String out;
    out.reserve(len);
    out += value[0];
    out += "***";
    out += value[len - 1];
    return out;
}

String basicAuthHeader(const char* user, const char* pass) {
    String combined = String(user ? user : "") + ":" + String(pass ? pass : "");
    size_t encodedLen = 0;
    mbedtls_base64_encode(nullptr, 0, &encodedLen,
                          reinterpret_cast<const unsigned char*>(combined.c_str()),
                          combined.length());
    String encoded;
    encoded.reserve(encodedLen + 8);
    unsigned char* buffer = reinterpret_cast<unsigned char*>(malloc(encodedLen + 1));
    if (!buffer) {
        return String();
    }
    if (mbedtls_base64_encode(buffer,
                              encodedLen + 1,
                              &encodedLen,
                              reinterpret_cast<const unsigned char*>(combined.c_str()),
                              combined.length()) != 0) {
        free(buffer);
        return String();
    }
    buffer[encodedLen] = '\0';
    encoded = reinterpret_cast<char*>(buffer);
    free(buffer);
    return String("Basic ") + encoded;
}

void refreshClientWifiState() {
    wl_status_t status = WiFi.status();
    if (oink::settings::hasClientWifiCredentials() && status == WL_CONNECTED) {
        oink::gApp.clientWifiConfigured = true;
        oink::gApp.clientWifiConnecting = false;
        oink::gApp.clientWifiConnected = true;
        strlcpy(oink::gApp.clientWifiIp, WiFi.localIP().toString().c_str(), sizeof(oink::gApp.clientWifiIp));
        strlcpy(oink::gApp.clientWifiStatus, "connected", sizeof(oink::gApp.clientWifiStatus));
        return;
    }

    if (!oink::settings::hasClientWifiCredentials()) {
        oink::gApp.clientWifiConfigured = false;
        oink::gApp.clientWifiConnecting = false;
        oink::gApp.clientWifiConnected = false;
        oink::gApp.clientWifiIp[0] = '\0';
        strlcpy(oink::gApp.clientWifiStatus, "not configured", sizeof(oink::gApp.clientWifiStatus));
        return;
    }

    oink::gApp.clientWifiConfigured = true;
    oink::gApp.clientWifiConnected = false;
    oink::gApp.clientWifiIp[0] = '\0';
    if (status == WL_CONNECT_FAILED) {
        oink::gApp.clientWifiConnecting = false;
        strlcpy(oink::gApp.clientWifiStatus, "connect failed; retrying", sizeof(oink::gApp.clientWifiStatus));
    } else if (status == WL_NO_SSID_AVAIL) {
        oink::gApp.clientWifiConnecting = false;
        strlcpy(oink::gApp.clientWifiStatus, "SSID not found; retrying", sizeof(oink::gApp.clientWifiStatus));
    } else if (oink::gApp.clientWifiConnecting) {
        strlcpy(oink::gApp.clientWifiStatus, "connecting", sizeof(oink::gApp.clientWifiStatus));
    } else {
        strlcpy(oink::gApp.clientWifiStatus, "stored in memory", sizeof(oink::gApp.clientWifiStatus));
    }
}

void ensureClientWifiAttempt() {
    if (!oink::settings::hasClientWifiCredentials() || oink::gApp.clientWifiConnected) {
        return;
    }
    if (millis() - oink::gApp.clientWifiLastAttemptMs < kClientWifiRetryMs) {
        return;
    }

    oink::scan::onCompanionChange();
    WiFi.begin(oink::settings::clientWifiSsid(), oink::settings::clientWifiPassword());
    oink::gApp.clientWifiConnecting = true;
    oink::gApp.clientWifiLastAttemptMs = millis();
    strlcpy(oink::gApp.clientWifiStatus, "connecting", sizeof(oink::gApp.clientWifiStatus));
    printf("[OINK-YOU] Client WiFi: connecting to %s\n", oink::settings::clientWifiSsid());
}

bool sendMultipartUpload(const char* path, size_t fileBytes, String& responseBody, int& statusCode) {
    responseBody = "";
    statusCode = 0;

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(kWigleUploadHost, kWigleUploadPort)) {
        responseBody = "TLS connection failed";
        return false;
    }

    const char* slash = strrchr(path, '/');
    const char* filename = slash ? slash + 1 : path;
    const String boundary = "----oinkspywigle7d9a";
    String auth = basicAuthHeader(oink::settings::wigleApiName(), oink::settings::wigleApiToken());
    if (auth.isEmpty()) {
        responseBody = "Failed to prepare authorization header";
        return false;
    }

    String preamble;
    preamble.reserve(256);
    preamble += "--" + boundary + "\r\n";
    preamble += "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"\r\n";
    preamble += "Content-Type: text/csv\r\n\r\n";
    String epilogue = "\r\n--" + boundary + "--\r\n";
    size_t contentLength = preamble.length() + fileBytes + epilogue.length();

    client.printf("POST %s HTTP/1.1\r\n", kWigleUploadPath);
    client.printf("Host: %s\r\n", kWigleUploadHost);
    client.print("Authorization: ");
    client.print(auth);
    client.print("\r\n");
    client.print("Accept: application/json\r\n");
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %u\r\n", static_cast<unsigned>(contentLength));
    client.print("Connection: close\r\n\r\n");
    client.print(preamble);
    if (!oink::log::writeSdFileToStream(path, client)) {
        responseBody = "Failed to stream WiGLE CSV from SD";
        client.stop();
        return false;
    }
    client.print(epilogue);

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    int firstSpace = statusLine.indexOf(' ');
    int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
    if (firstSpace > 0 && secondSpace > firstSpace) {
        statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt();
    }

    bool inBody = false;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (!inBody) {
            if (line == "\r" || line.length() == 0) {
                inBody = true;
            }
            continue;
        }
        responseBody += line;
    }
    client.stop();
    return statusCode >= 200 && statusCode < 300;
}

void wigleUploadTask(void*) {
    char path[96];
    size_t fileBytes = 0;
    String error;
    if (!oink::log::prepareWigleCsv(path, sizeof(path), fileBytes, error)) {
        strlcpy(oink::gApp.wigleUploadStatus, error.c_str(), sizeof(oink::gApp.wigleUploadStatus));
        oink::gApp.wigleUploadStatusCode = 0;
        oink::gApp.wigleUploadInProgress = false;
        oink::gApp.wigleUploadRequested = false;
        gWigleUploadTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    refreshClientWifiState();
    if (!oink::gApp.clientWifiConnected) {
        strlcpy(oink::gApp.wigleUploadStatus, "Client WiFi disconnected before upload", sizeof(oink::gApp.wigleUploadStatus));
        oink::gApp.wigleUploadStatusCode = 0;
        oink::gApp.wigleUploadInProgress = false;
        oink::gApp.wigleUploadRequested = false;
        gWigleUploadTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    printf("[OINK-YOU] WiGLE upload starting: path=%s bytes=%u wifi=%s ip=%s user=%s\n",
           path,
           static_cast<unsigned>(fileBytes),
           oink::gApp.clientWifiConnected ? "connected" : "down",
           oink::gApp.clientWifiIp,
           maskSecretHint(oink::settings::wigleApiName()).c_str());

    String response;
    int statusCode = 0;
    bool ok = sendMultipartUpload(path, fileBytes, response, statusCode);
    oink::gApp.wigleUploadStatusCode = statusCode;
    oink::gApp.wigleUploadLastMs = millis();
    strlcpy(oink::gApp.wigleLastUploadPath, path, sizeof(oink::gApp.wigleLastUploadPath));
    if (ok) {
        strlcpy(oink::gApp.wigleUploadStatus, "upload complete", sizeof(oink::gApp.wigleUploadStatus));
        printf("[OINK-YOU] WiGLE upload complete: %s (%u bytes)\n", path, static_cast<unsigned>(fileBytes));
    } else {
        String message = response.length() ? response : "upload failed";
        message.replace('\n', ' ');
        strlcpy(oink::gApp.wigleUploadStatus, message.c_str(), sizeof(oink::gApp.wigleUploadStatus));
        printf("[OINK-YOU] WiGLE upload failed: status=%d path=%s response=%s\n",
               statusCode,
               path,
               message.c_str());
    }
    oink::gApp.wigleUploadInProgress = false;
    oink::gApp.wigleUploadRequested = false;
    gWigleUploadTask = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

namespace oink::uplink {

void begin() {
    refreshClientWifiState();
}

void poll() {
    refreshClientWifiState();
    ensureClientWifiAttempt();

    if (oink::gApp.wigleUploadRequested && !oink::gApp.wigleUploadInProgress && gWigleUploadTask == nullptr) {
        oink::gApp.wigleUploadRequested = false;
        oink::gApp.wigleUploadInProgress = true;
        strlcpy(oink::gApp.wigleUploadStatus, "uploading", sizeof(oink::gApp.wigleUploadStatus));
        if (xTaskCreatePinnedToCore(wigleUploadTask, "wigleUpload", 8192, nullptr, 1, &gWigleUploadTask, 1) != pdPASS) {
            oink::gApp.wigleUploadInProgress = false;
            strlcpy(oink::gApp.wigleUploadStatus, "Failed to start upload task", sizeof(oink::gApp.wigleUploadStatus));
            printf("[OINK-YOU] WiGLE upload task creation failed\n");
        }
    }
}

void writeClientWifiStatusJson(Print& out) {
    refreshClientWifiState();
    out.printf("{\"configured\":%s,\"connecting\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"status\":\"%s\"}",
               oink::gApp.clientWifiConfigured ? "true" : "false",
               oink::gApp.clientWifiConnecting ? "true" : "false",
               oink::gApp.clientWifiConnected ? "true" : "false",
               oink::gApp.clientWifiConfigured ? oink::gApp.clientWifiSsid : "",
               oink::gApp.clientWifiIp,
               oink::gApp.clientWifiStatus);
}

void writeWigleStatusJson(Print& out) {
    size_t wigleBytes = 0;
    String blockedReason;
    bool ready = oink::log::prepareWigleCsv(nullptr, 0, wigleBytes, blockedReason);
    const char* candidatePath = oink::gApp.wigleUploadCandidatePath[0]
                                    ? oink::gApp.wigleUploadCandidatePath
                                    : (oink::gApp.wigleLastClosedPath[0] ? oink::gApp.wigleLastClosedPath : oink::gApp.wigleCurrentPath);
    out.printf("{\"configured\":%s,\"api_name_hint\":\"%s\",\"upload_in_progress\":%s,\"last_status_code\":%d,\"last_status\":\"%s\",\"current_path\":\"%s\",\"current_file_bytes\":%u,\"last_upload_path\":\"%s\",\"ready\":%s,\"blocked_reason\":\"%s\",\"active_path\":\"%s\",\"active_file_bytes\":%u,\"last_closed_path\":\"%s\",\"last_closed_file_bytes\":%u}",
               oink::settings::hasWigleCredentials() ? "true" : "false",
               maskSecretHint(oink::settings::wigleApiName()).c_str(),
               oink::gApp.wigleUploadInProgress ? "true" : "false",
               oink::gApp.wigleUploadStatusCode,
               oink::gApp.wigleUploadStatus,
               candidatePath,
               static_cast<unsigned>(wigleBytes),
               oink::gApp.wigleLastUploadPath,
               ready ? "true" : "false",
               blockedReason.c_str(),
               oink::gApp.wigleCurrentPath,
               static_cast<unsigned>(oink::gApp.wigleCurrentFileBytes),
               oink::gApp.wigleLastClosedPath,
               static_cast<unsigned>(oink::gApp.wigleLastClosedFileBytes));
}

bool updateClientWifiFromJson(const String& json, String& error) {
    error = "";
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObject>()) {
        error = "Invalid JSON";
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root["forget"] | false) {
        if (!oink::settings::clearClientWifiCredentials(error)) {
            return false;
        }
        WiFi.disconnect(false, false);
        strlcpy(oink::gApp.clientWifiStatus, "cleared", sizeof(oink::gApp.clientWifiStatus));
        return true;
    }

    const char* ssid = root["ssid"] | "";
    const char* password = root["password"] | "";
    if (!oink::settings::setClientWifiCredentials(ssid, password, error)) {
        return false;
    }
    oink::gApp.clientWifiConnecting = false;
    oink::gApp.clientWifiConnected = false;
    oink::gApp.clientWifiLastAttemptMs = 0;
    strlcpy(oink::gApp.clientWifiStatus, "stored; connecting shortly", sizeof(oink::gApp.clientWifiStatus));
    return true;
}

bool updateWigleFromJson(const String& json, String& error) {
    error = "";
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObject>()) {
        error = "Invalid JSON";
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root["forget"] | false) {
        return oink::settings::clearWigleCredentials(error);
    }

    const char* apiName = root["api_name"] | "";
    const char* apiToken = root["api_token"] | "";
    return oink::settings::setWigleCredentials(apiName, apiToken, error);
}

bool requestWigleUpload(String& error) {
    error = "";
    if (oink::gApp.wigleUploadInProgress || gWigleUploadTask != nullptr) {
        error = "WiGLE upload already running";
        return false;
    }
    if (!oink::settings::hasWigleCredentials()) {
        error = "Store WiGLE credentials first";
        return false;
    }
    if (!oink::gApp.clientWifiConnected) {
        error = "Client WiFi is not connected";
        return false;
    }
    size_t fileBytes = 0;
    if (!oink::log::prepareWigleCsv(nullptr, 0, fileBytes, error)) {
        return false;
    }
    printf("[OINK-YOU] WiGLE upload queued: bytes=%u candidate=%s\n",
           static_cast<unsigned>(fileBytes),
           oink::gApp.wigleUploadCandidatePath);
    oink::gApp.wigleUploadRequested = true;
    strlcpy(oink::gApp.wigleUploadStatus, "queued", sizeof(oink::gApp.wigleUploadStatus));
    return true;
}

} // namespace oink::uplink
