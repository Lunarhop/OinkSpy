#pragma once

#include <Arduino.h>

namespace oink::uplink {

void begin();
void poll();
void writeClientWifiStatusJson(Print& out);
void writeWigleStatusJson(Print& out);
bool updateClientWifiFromJson(const String& json, String& error);
bool updateWigleFromJson(const String& json, String& error);
bool requestWigleUpload(String& error);

} // namespace oink::uplink
