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
bool handleCommand(const char* line, Stream& out);

} // namespace oink::settings
