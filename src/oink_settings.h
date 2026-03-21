#pragma once

#include <Arduino.h>

namespace oink::settings {

void load();
const char* deviceId();
unsigned long bootCount();
String serializeRuntimeConfigJson();
bool persistRuntimeConfig(bool preferSd, String& error);
bool setWardriveEnabled(bool enabled, bool persist, String& error);
void writeWardriveConfigJson(Print& out);
bool hasClientWifiCredentials();
const char* clientWifiSsid();
const char* clientWifiPassword();
bool setClientWifiCredentials(const char* ssid, const char* password, String& error);
bool clearClientWifiCredentials(String& error);
bool hasWigleCredentials();
const char* wigleApiName();
const char* wigleApiToken();
bool setWigleCredentials(const char* apiName, const char* apiToken, String& error);
bool clearWigleCredentials(String& error);
bool handleCommand(const char* line, Stream& out);

} // namespace oink::settings
