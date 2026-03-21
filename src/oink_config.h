#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#ifndef OINK_FEATURE_GNSS
#define OINK_FEATURE_GNSS 1
#endif

#ifndef OINK_GNSS_BAUD
#define OINK_GNSS_BAUD 9600
#endif

#ifndef OINK_GNSS_UART_RX
#define OINK_GNSS_UART_RX D6
#endif

#ifndef OINK_GNSS_UART_TX
#define OINK_GNSS_UART_TX D7
#endif

#ifndef OINK_GNSS_HW_SERIAL_NUM
#define OINK_GNSS_HW_SERIAL_NUM 1
#endif

#ifndef OINK_GNSS_LED_PIN
#define OINK_GNSS_LED_PIN LED_BUILTIN
#endif

#ifndef OINK_WDRIVE_DEBUG
#define OINK_WDRIVE_DEBUG 0
#endif

namespace oink::config {

static constexpr uint8_t kButtonPin = D1;
static constexpr uint8_t kSdChipSelectPin = D2;
static constexpr uint8_t kBuzzerPin = D3;
static constexpr uint8_t kI2cSdaPin = D4;
static constexpr uint8_t kI2cSclPin = D5;
static constexpr uint8_t kGpsTxPin = D6;
static constexpr uint8_t kGpsRxPin = D7;
static constexpr uint8_t kSpiSckPin = D8;
static constexpr uint8_t kSpiMisoPin = D9;
static constexpr uint8_t kSpiMosiPin = D10;
static constexpr uint8_t kStatusLedPin = LED_BUILTIN;

static constexpr int kMaxDetections = 200;
static constexpr unsigned long kBleScanIntervalMs = 3000;
static constexpr int kStandaloneBleScanDurationSec = 2;
static constexpr int kCompanionBleScanDurationSec = 3;
static constexpr unsigned long kWardriveWifiScanIntervalMs = 10000;
// Passive AP scans run alongside BLE and SoftAP traffic, so they need a bit more
// dwell-time headroom than the bare minimum to avoid async timeout churn.
static constexpr uint32_t kWardriveWifiMaxMsPerChannel = 250;
static constexpr unsigned long kWardriveWifiApGraceMs = 15000;
static constexpr unsigned long kGpsStaleMs = 30000;
static constexpr unsigned long kSerialTimeoutMs = 5000;
static constexpr unsigned long kAlertFlashMs = 2000;
static constexpr unsigned long kHeartbeatIntervalMs = 10000;
static constexpr unsigned long kOutOfRangeMs = 30000;
static constexpr unsigned long kUiRefreshMs = 250;
static constexpr unsigned long kSaveIntervalMs = 15000;
static constexpr unsigned long kInitialSaveDelayMs = 5000;
static constexpr unsigned long kButtonDebounceMs = 30;
static constexpr unsigned long kButtonLongPressMs = 800;
static constexpr unsigned long kAudioTickMs = 8;
static constexpr size_t kGnssRingBufferSize = 256;
static constexpr size_t kGnssSentenceBufferSize = 128;
static constexpr float kGnssHdopToAccuracyMeters = 5.0f;

static constexpr const char kApSsid[] = "oinkyou";
static constexpr const char kApPassword[] = "oinkyou123";
static constexpr const char kBleServiceUuid[] = "a1b2c3d4-e5f6-7890-abcd-ef0123456789";
static constexpr const char kBleTxCharUuid[] = "a1b2c3d4-e5f6-7890-abcd-ef01234567aa";
static constexpr const char kSessionFile[] = "/session.json";
static constexpr const char kPrevSessionFile[] = "/prev_session.json";

extern const char* const kFlockMacPrefixes[];
extern const size_t kFlockMacPrefixCount;
extern const char* const kFlockManufacturerPrefixes[];
extern const size_t kFlockManufacturerPrefixCount;
extern const char* const kSoundThinkingPrefixes[];
extern const size_t kSoundThinkingPrefixCount;
extern const char* const kDeviceNamePatterns[];
extern const size_t kDeviceNamePatternCount;
extern const uint16_t kBleManufacturerIds[];
extern const size_t kBleManufacturerIdCount;
extern const char* const kRavenServiceUuids[];
extern const size_t kRavenServiceUuidCount;

static constexpr const char kRavenDeviceInfoService[] = "0000180a-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenGpsService[] = "00003100-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenPowerService[] = "00003200-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenNetworkService[] = "00003300-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenUploadService[] = "00003400-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenErrorService[] = "00003500-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenOldHealthService[] = "00001809-0000-1000-8000-00805f9b34fb";
static constexpr const char kRavenOldLocationService[] = "00001819-0000-1000-8000-00805f9b34fb";

} // namespace oink::config
