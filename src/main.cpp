#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>

#include "oink_board.h"
#include "oink_gnss.h"
#include "oink_log.h"
#include "oink_portal.h"
#include "oink_rtc.h"
#include "oink_scan.h"
#include "oink_settings.h"
#include "oink_state.h"
#include "oink_time.h"
#include "oink_uplink.h"

using namespace oink;

namespace {

void handleApEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        if (gApp.apClientCount < 255) {
            ++gApp.apClientCount;
        }
        gApp.lastApClientEventMs = millis();
        printf("[OINK-YOU] AP client joined: " MACSTR " aid=%u\n",
               MAC2STR(info.wifi_ap_staconnected.mac),
               info.wifi_ap_staconnected.aid);
        printf("[OINK-YOU] AP clients: %u\n", static_cast<unsigned>(gApp.apClientCount));
        return;
    }

    if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        if (gApp.apClientCount > 0) {
            --gApp.apClientCount;
        }
        gApp.lastApClientEventMs = millis();
        printf("[OINK-YOU] AP client left: " MACSTR " aid=%u\n",
               MAC2STR(info.wifi_ap_stadisconnected.mac),
               info.wifi_ap_stadisconnected.aid);
        printf("[OINK-YOU] AP clients: %u\n", static_cast<unsigned>(gApp.apClientCount));
    }
}

void processControlEvents() {
    oink::board::ControlEvent event = oink::board::ControlEvent::None;
    while (oink::board::nextControlEvent(event)) {
        switch (event) {
            case oink::board::ControlEvent::ShortPress:
            {
                bool enableWardrive = !gApp.runtimeConfig.wardrive.enabled;
                String error;
                if (!oink::settings::setWardriveEnabled(enableWardrive, true, error)) {
                    oink::board::addNotification("MODE FAILED");
                    oink::board::errorBeep();
                    printf("[OINK-YOU] Button short press: mode switch failed: %s\n", error.c_str());
                } else {
                    oink::board::addNotification(gApp.runtimeConfig.wardrive.enabled ? "WDRIVE ON" : "WDRIVE OFF");
                    oink::board::confirmBeep();
                    printf("[OINK-YOU] Button short press: mode %s\n",
                           gApp.runtimeConfig.wardrive.enabled ? "ENABLED" : "DISABLED");
                    if (!error.isEmpty()) {
                        printf("[OINK-YOU] Button short press note: %s\n", error.c_str());
                    }
                }
                break;
            }
            case oink::board::ControlEvent::LongPress:
                oink::board::toggleAudioMode();
                oink::board::addNotification(gApp.buzzerOn ? "AUDIO ON" : "SILENT");
                printf("[OINK-YOU] Button long press: audio %s\n", gApp.buzzerOn ? "ON" : "SILENT");
                break;
            case oink::board::ControlEvent::None:
            default:
                break;
        }
    }
}

char gSerialLine[96] = "";
size_t gSerialLineLength = 0;

void handleSerialByte(char c) {
    if (c == '\r' || c == '\n') {
        if (gSerialLineLength == 0) {
            return;
        }
        gSerialLine[gSerialLineLength] = '\0';
        if (!oink::gnss::handleCommand(gSerialLine, Serial) &&
            !oink::settings::handleCommand(gSerialLine, Serial)) {
            printf("[OINK-YOU] Serial command ignored: %s\n", gSerialLine);
        }
        gSerialLineLength = 0;
        gSerialLine[0] = '\0';
        return;
    }

    if (c == '\b' || c == 127) {
        if (gSerialLineLength > 0) {
            --gSerialLineLength;
            gSerialLine[gSerialLineLength] = '\0';
        }
        return;
    }

    if (!isprint(static_cast<unsigned char>(c)) || gSerialLineLength + 1 >= sizeof(gSerialLine)) {
        return;
    }

    gSerialLine[gSerialLineLength++] = c;
}

void pollSerialHost() {
    if (Serial.available()) {
        while (Serial.available()) {
            int value = Serial.read();
            if (value >= 0) {
                handleSerialByte(static_cast<char>(value));
            }
        }
        gApp.lastSerialHeartbeat = millis();
        if (!gApp.serialHostConnected) {
            gApp.serialHostConnected = true;
            gApp.companionChangePending = true;
        }
        return;
    }

    if (gApp.serialHostConnected && millis() - gApp.lastSerialHeartbeat >= gApp.runtimeConfig.serialTimeoutMs) {
        gApp.serialHostConnected = false;
        gApp.companionChangePending = true;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    gApp.negotiatedMtu = 23;
    gApp.mutex = xSemaphoreCreateMutex();

    oink::board::initializePins();
    oink::log::beginStorage();
    oink::settings::load();
    oink::scan::loadProfile();
    oink::timeutil::begin();
    oink::gnss::begin();
    oink::log::prepareSdLogs();

    printf("\n========================================\n");
    printf("  OINK-YOU Surveillance Detector + OLED\n");
    printf("  Buzzer: %s\n", gApp.buzzerOn ? "ON" : "OFF");
    printf("========================================\n");

    oink::scan::setupBle();
    oink::board::bootBeep();
    oink::board::initializeDisplay();

    WiFi.onEvent(handleApEvent);
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    bool apStarted = WiFi.softAP(gApp.runtimeConfig.apSsid, gApp.runtimeConfig.apPassword);
    gApp.lastApClientEventMs = millis();
    printf("[OINK-YOU] AP: %s / %s\n", gApp.runtimeConfig.apSsid, gApp.runtimeConfig.apPassword);
    printf("[OINK-YOU] AP start: %s\n", apStarted ? "OK" : "FAILED");
    printf("[OINK-YOU] IP: %s\n", WiFi.softAPIP().toString().c_str());
    oink::portal::beginDns();
    oink::uplink::begin();
    oink::portal::beginServer();

    printf("[OINK-YOU] Board map: button D1, SD CS D2, buzzer D3, OLED SDA/SCL D4/D5, GNSS fixed UART D6/D7, SPI D8-D10\n");
    printf("[OINK-YOU] Device ID: %s, boot #%lu\n", oink::settings::deviceId(), oink::settings::bootCount());
    printf("[OINK-YOU] SD logging: %s\n", gApp.sdReady && gApp.runtimeConfig.sdLoggingEnabled ? "ENABLED" : "DISABLED");
    printf("[OINK-YOU] Log worker: %s\n", gApp.logWorkerReady ? "READY" : "DISABLED");
    printf("[OINK-YOU] Time source: %s (%s)\n", oink::timeutil::timeSourceLabel(), gApp.dayToken);
    oink::rtc::Status rtcStatus = oink::rtc::getStatus();
    printf("[OINK-YOU] RTC: %s\n",
           !rtcStatus.enabled ? "DISABLED" : !rtcStatus.present ? "NOT FOUND" : rtcStatus.timeValid ? rtcStatus.iso8601 : "PRESENT, TIME INVALID");
    printf("[OINK-YOU] OTA: %s\n", gApp.runtimeConfig.otaEnabled ? "ENABLED (/api/ota)" : "DISABLED");
    printf("[OINK-YOU] Wardrive: %s\n", gApp.runtimeConfig.wardrive.enabled ? "ENABLED (/api/wardrive)" : "DISABLED");
    printf("[OINK-YOU] Client WiFi: %s\n", oink::settings::hasClientWifiCredentials() ? oink::settings::clientWifiSsid() : "NOT CONFIGURED");
    printf("[OINK-YOU] WiGLE: %s\n", oink::settings::hasWigleCredentials() ? "CREDENTIALS STORED" : "NOT CONFIGURED");
    oink::gnss::printStatus(Serial);
    if (gApp.sdReady && gApp.runtimeConfig.sdLoggingEnabled) {
        printf("[OINK-YOU] SD session CSV: %s\n", oink::log::sessionCsvPath());
        printf("[OINK-YOU] SD session JSONL: %s\n", oink::log::sessionJsonlPath());
    }
    printf("[OINK-YOU] Controls: short=switch flock/wardrive mode, long=audio mode\n");
    printf("[OINK-YOU] Serial commands: gnss status, wardrive status|debug|on|off|toggle\n");
    printf("[OINK-YOU] Detection methods: MAC prefix, device name, manufacturer ID, Raven UUID\n");
    printf("[OINK-YOU] Dashboard: http://192.168.4.1\n");
    printf("[OINK-YOU] Ready - BLE GATT + AP mode + OLED\n\n");
}

void loop() {
    oink::timeutil::poll();
    oink::portal::poll();
    oink::board::pollControls();
    processControlEvents();
    oink::board::serviceUi();
    pollSerialHost();
    oink::gnss::update();

    if (gApp.companionChangePending) {
        gApp.companionChangePending = false;
        oink::scan::onCompanionChange();
    }

    oink::scan::pollScan();
    oink::uplink::poll();
    oink::log::pollAutoSave();
    oink::log::pollWardrive();
    delay(20);
}
