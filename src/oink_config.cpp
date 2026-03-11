#include "oink_config.h"

namespace oink::config {

const char* const kFlockMacPrefixes[] = {
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84",
    "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
    "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea",
    "b4:1e:52"
};
const size_t kFlockMacPrefixCount = sizeof(kFlockMacPrefixes) / sizeof(kFlockMacPrefixes[0]);

const char* const kFlockManufacturerPrefixes[] = {
    "f4:6a:dd", "f8:a2:d6", "e0:0a:f6", "00:f4:8d", "d0:39:57",
    "e8:d0:fc"
};
const size_t kFlockManufacturerPrefixCount = sizeof(kFlockManufacturerPrefixes) / sizeof(kFlockManufacturerPrefixes[0]);

const char* const kSoundThinkingPrefixes[] = {
    "d4:11:d6"
};
const size_t kSoundThinkingPrefixCount = sizeof(kSoundThinkingPrefixes) / sizeof(kSoundThinkingPrefixes[0]);

const char* const kDeviceNamePatterns[] = {
    "FS Ext Battery", "Penguin", "Flock", "Pigvision"
};
const size_t kDeviceNamePatternCount = sizeof(kDeviceNamePatterns) / sizeof(kDeviceNamePatterns[0]);

const uint16_t kBleManufacturerIds[] = {0x09C8};
const size_t kBleManufacturerIdCount = sizeof(kBleManufacturerIds) / sizeof(kBleManufacturerIds[0]);

const char* const kRavenServiceUuids[] = {
    kRavenDeviceInfoService,
    kRavenGpsService,
    kRavenPowerService,
    kRavenNetworkService,
    kRavenUploadService,
    kRavenErrorService,
    kRavenOldHealthService,
    kRavenOldLocationService,
};
const size_t kRavenServiceUuidCount = sizeof(kRavenServiceUuids) / sizeof(kRavenServiceUuids[0]);

} // namespace oink::config
