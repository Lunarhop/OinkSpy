#pragma once

#include <Arduino.h>
#include <time.h>

namespace oink::timeutil {

void begin();
void poll();
bool isSynced();
bool setFromEpoch(time_t epoch, const char* source);
time_t currentEpoch();
const char* timeSourceLabel();
bool sourceIs(const char* label);
void formatIso8601(char* buffer, size_t size);
void currentDayToken(char* buffer, size_t size);

} // namespace oink::timeutil

