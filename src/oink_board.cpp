#include "oink_board.h"

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <cstring>

#include "oink_config.h"
#include "oink_scan.h"
#include "oink_state.h"

namespace {

using oink::board::ControlEvent;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C gDisplay(U8G2_R0, U8X8_PIN_NONE);

struct AudioStep {
    int startFreq;
    int endFreq;
    unsigned long durationMs;
    unsigned long gapMs;
    int warbleHz;
};

constexpr size_t kControlQueueSize = 8;
ControlEvent gControlQueue[kControlQueueSize] = {};
size_t gControlHead = 0;
size_t gControlTail = 0;

bool gButtonStablePressed = false;
bool gButtonLastReading = false;
unsigned long gButtonLastTransitionMs = 0;
unsigned long gButtonPressStartMs = 0;
bool gLongPressQueued = false;
uint8_t gPendingShortPresses = 0;
unsigned long gLastShortReleaseMs = 0;

const AudioStep* gActivePattern = nullptr;
size_t gActivePatternLength = 0;
size_t gActivePatternIndex = 0;
unsigned long gAudioStepStartMs = 0;
unsigned long gAudioStepEndMs = 0;
unsigned long gAudioLastTickMs = 0;
bool gAudioStepToneActive = false;
bool gAudioPlaying = false;

constexpr AudioStep kBootPattern[] = {
    {850, 380, 180, 100, 40},
    {780, 350, 150, 100, 50},
    {820, 280, 220, 80, 60},
    {600, 600, 25, 40, 0},
    {550, 550, 25, 0, 0},
};

constexpr AudioStep kDetectPattern[] = {
    {400, 900, 100, 60, 30},
    {450, 950, 100, 60, 30},
    {900, 350, 200, 0, 50},
};

constexpr AudioStep kHeartbeatPattern[] = {
    {500, 400, 80, 120, 20},
    {480, 380, 80, 0, 20},
};

constexpr AudioStep kConfirmPattern[] = {
    {720, 720, 40, 30, 0},
    {860, 860, 45, 0, 0},
};

constexpr AudioStep kBookmarkPattern[] = {
    {520, 640, 60, 25, 0},
    {640, 760, 60, 25, 0},
    {760, 920, 70, 0, 0},
};

constexpr AudioStep kErrorPattern[] = {
    {220, 220, 90, 35, 0},
    {180, 180, 120, 0, 0},
};

constexpr uint8_t kPigPanelX = 82;
constexpr uint8_t kPigPanelY = 28;
constexpr uint8_t kPigPanelW = 42;
constexpr uint8_t kPigPanelH = 34;

enum class PigMood {
    Idle,
    Tracking,
    Alert,
    Paused,
    Companion,
};

void enqueueEvent(ControlEvent event) {
    size_t next = (gControlTail + 1) % kControlQueueSize;
    if (next == gControlHead) {
        return;
    }
    gControlQueue[gControlTail] = event;
    gControlTail = next;
}

void startPattern(const AudioStep* pattern, size_t length) {
    if (!oink::gApp.buzzerOn || !pattern || length == 0) {
        noTone(oink::config::kBuzzerPin);
        gAudioPlaying = false;
        gActivePattern = nullptr;
        gActivePatternLength = 0;
        gActivePatternIndex = 0;
        gAudioStepToneActive = false;
        return;
    }

    gActivePattern = pattern;
    gActivePatternLength = length;
    gActivePatternIndex = 0;
    gAudioStepStartMs = millis();
    gAudioStepEndMs = gAudioStepStartMs + pattern[0].durationMs + pattern[0].gapMs;
    gAudioLastTickMs = 0;
    gAudioStepToneActive = false;
    gAudioPlaying = true;
}

void stopPattern() {
    noTone(oink::config::kBuzzerPin);
    gAudioPlaying = false;
    gActivePattern = nullptr;
    gActivePatternLength = 0;
    gActivePatternIndex = 0;
    gAudioStepToneActive = false;
}

void serviceAudio() {
    if (!gAudioPlaying || !gActivePattern || gActivePatternIndex >= gActivePatternLength) {
        return;
    }

    if (!oink::gApp.buzzerOn) {
        stopPattern();
        return;
    }

    unsigned long now = millis();
    const AudioStep& step = gActivePattern[gActivePatternIndex];
    unsigned long toneEndMs = gAudioStepStartMs + step.durationMs;

    if (now >= gAudioStepEndMs) {
        noTone(oink::config::kBuzzerPin);
        ++gActivePatternIndex;
        if (gActivePatternIndex >= gActivePatternLength) {
            stopPattern();
            return;
        }
        gAudioStepStartMs = now;
        const AudioStep& next = gActivePattern[gActivePatternIndex];
        gAudioStepEndMs = now + next.durationMs + next.gapMs;
        gAudioLastTickMs = 0;
        gAudioStepToneActive = false;
        return;
    }

    if (now >= toneEndMs) {
        if (gAudioStepToneActive) {
            noTone(oink::config::kBuzzerPin);
            gAudioStepToneActive = false;
        }
        return;
    }

    if (gAudioLastTickMs != 0 && now - gAudioLastTickMs < oink::config::kAudioTickMs) {
        return;
    }
    gAudioLastTickMs = now;

    int freq = step.startFreq;
    if (step.durationMs > 0 && step.startFreq != step.endFreq) {
        float progress = static_cast<float>(now - gAudioStepStartMs) / static_cast<float>(step.durationMs);
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        freq = step.startFreq + static_cast<int>((step.endFreq - step.startFreq) * progress);
    }
    if (step.warbleHz > 0) {
        unsigned long slice = (now - gAudioStepStartMs) / oink::config::kAudioTickMs;
        if ((slice % 6) < 3) {
            freq += step.warbleHz;
        } else {
            freq -= step.warbleHz;
        }
    }
    if (freq < 100) {
        freq = 100;
    }

    tone(oink::config::kBuzzerPin, freq);
    gAudioStepToneActive = true;
}

const char* alertModeLabel() {
    return oink::gApp.buzzerOn ? "AUDIO ON" : "SILENT";
}

void copyTrimmed(char* dest, size_t size, const char* src, size_t maxChars) {
    if (!dest || size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len <= maxChars) {
        strncpy(dest, src, size - 1);
        dest[size - 1] = '\0';
        return;
    }

    size_t keep = maxChars;
    if (keep >= size) {
        keep = size - 1;
    }
    if (keep > 0) {
        --keep;
    }
    strncpy(dest, src, keep);
    dest[keep] = '.';
    dest[keep + 1] = '\0';
}

PigMood currentPigMood() {
    if (oink::gApp.alertFlashOn || oink::gApp.deviceInRange) {
        return PigMood::Alert;
    }
    if (!oink::scan::isScanningEnabled()) {
        return PigMood::Paused;
    }
    if (oink::gApp.bleClientConnected || oink::gApp.serialHostConnected) {
        return PigMood::Companion;
    }
    if (oink::gApp.detectionCount > 0) {
        return PigMood::Tracking;
    }
    return PigMood::Idle;
}

void drawPigBuddy(PigMood mood) {
    const int blinkFrame = (millis() / 220) % 12;
    const bool blink = (mood == PigMood::Paused) ? ((millis() / 320) % 10 > 6) : (blinkFrame == 0);
    const int x = kPigPanelX;
    const int y = kPigPanelY;

    gDisplay.drawRFrame(x, y, kPigPanelW, kPigPanelH, 4);
    gDisplay.drawPixel(x + 4, y + 4);
    gDisplay.drawPixel(x + kPigPanelW - 5, y + 4);

    gDisplay.drawLine(x + 9, y + 7, x + 14, y + 1);
    gDisplay.drawLine(x + 14, y + 1, x + 18, y + 9);
    gDisplay.drawLine(x + 18, y + 9, x + 9, y + 7);
    gDisplay.drawLine(x + 24, y + 9, x + 28, y + 1);
    gDisplay.drawLine(x + 28, y + 1, x + 33, y + 7);
    gDisplay.drawLine(x + 33, y + 7, x + 24, y + 9);

    gDisplay.drawCircle(x + 21, y + 18, 11, U8G2_DRAW_ALL);
    gDisplay.drawCircle(x + 21, y + 18, 10, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);

    if (blink) {
        gDisplay.drawHLine(x + 16, y + 15, 4);
        gDisplay.drawHLine(x + 24, y + 15, 4);
    } else if (mood == PigMood::Companion) {
        gDisplay.drawCircle(x + 17, y + 15, 1, U8G2_DRAW_ALL);
        gDisplay.drawLine(x + 24, y + 13, x + 28, y + 17);
        gDisplay.drawLine(x + 24, y + 17, x + 28, y + 13);
    } else {
        gDisplay.drawDisc(x + 17, y + 15, 1, U8G2_DRAW_ALL);
        gDisplay.drawDisc(x + 25, y + 15, 1, U8G2_DRAW_ALL);
    }

    gDisplay.drawRFrame(x + 15, y + 19, 12, 8, 3);
    gDisplay.drawDisc(x + 19, y + 23, 1, U8G2_DRAW_ALL);
    gDisplay.drawDisc(x + 23, y + 23, 1, U8G2_DRAW_ALL);

    switch (mood) {
        case PigMood::Alert:
            gDisplay.drawCircle(x + 21, y + 29, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
            gDisplay.drawLine(x + 7, y + 13, x + 4, y + 10);
            gDisplay.drawLine(x + 35, y + 13, x + 38, y + 10);
            gDisplay.drawLine(x + 40, y + 3, x + 40, y + 12);
            gDisplay.drawDisc(x + 40, y + 15, 1, U8G2_DRAW_ALL);
            break;
        case PigMood::Paused:
            gDisplay.drawLine(x + 17, y + 28, x + 20, y + 26);
            gDisplay.drawLine(x + 20, y + 26, x + 22, y + 26);
            gDisplay.drawLine(x + 22, y + 26, x + 25, y + 28);
            gDisplay.setFont(u8g2_font_4x6_tr);
            gDisplay.drawStr(x + 31, y + 9, "z");
            gDisplay.drawStr(x + 35, y + 5, "z");
            break;
        case PigMood::Companion:
            gDisplay.drawLine(x + 18, y + 28, x + 21, y + 30);
            gDisplay.drawLine(x + 21, y + 30, x + 24, y + 28);
            gDisplay.drawLine(x + 26, y + 26, x + 29, y + 23);
            break;
        case PigMood::Tracking:
            gDisplay.drawLine(x + 18, y + 28, x + 21, y + 26);
            gDisplay.drawLine(x + 21, y + 26, x + 24, y + 28);
            break;
        case PigMood::Idle:
        default:
            gDisplay.drawLine(x + 17, y + 27, x + 19, y + 28);
            gDisplay.drawLine(x + 19, y + 28, x + 21, y + 28);
            gDisplay.drawLine(x + 21, y + 28, x + 23, y + 28);
            gDisplay.drawLine(x + 23, y + 28, x + 25, y + 27);
            break;
    }
}

} // namespace

namespace oink::board {

void initializePins() {
    oink::gApp.buzzerOn = true;
    pinMode(config::kBuzzerPin, OUTPUT);
    digitalWrite(config::kBuzzerPin, LOW);
    pinMode(config::kStatusLedPin, OUTPUT);
    digitalWrite(config::kStatusLedPin, LOW);
    pinMode(config::kButtonPin, INPUT_PULLUP);

    gButtonStablePressed = false;
    gButtonLastReading = false;
    gButtonLastTransitionMs = millis();
}

void initializeDisplay() {
    Wire.begin(config::kI2cSdaPin, config::kI2cSclPin);
    gDisplay.begin();
    gDisplay.setFont(u8g2_font_6x12_tr);
    gDisplay.setContrast(180);
    oink::gApp.oledReady = true;
    printf("[OINK-YOU] OLED ready (SSD1306 128x64 on SDA=%u SCL=%u)\n",
           config::kI2cSdaPin,
           config::kI2cSclPin);
}

void addNotification(const char* desc) {
    strncpy(oink::gApp.notifications[oink::gApp.notificationHead].shortDesc,
            desc,
            sizeof(oink::gApp.notifications[0].shortDesc) - 1);
    oink::gApp.notifications[oink::gApp.notificationHead].shortDesc[sizeof(oink::gApp.notifications[0].shortDesc) - 1] = '\0';
    oink::gApp.notifications[oink::gApp.notificationHead].timestamp = millis();
    oink::gApp.notificationHead = (oink::gApp.notificationHead + 1) % 4;
}

void bootBeep() {
    printf("[OINK-YOU] Boot sound (buzzer %s)\n", oink::gApp.buzzerOn ? "ON" : "OFF");
    startPattern(kBootPattern, sizeof(kBootPattern) / sizeof(kBootPattern[0]));
}

void detectBeep() {
    printf("[OINK-YOU] Detection alert!\n");
    startPattern(kDetectPattern, sizeof(kDetectPattern) / sizeof(kDetectPattern[0]));
    if (oink::gApp.oledReady) {
        addNotification("HIGH CONFIDENCE");
        oink::gApp.lastAlertFlash = millis();
        oink::gApp.alertFlashOn = true;
    }
}

void heartbeat() {
    startPattern(kHeartbeatPattern, sizeof(kHeartbeatPattern) / sizeof(kHeartbeatPattern[0]));
}

void confirmBeep() {
    startPattern(kConfirmPattern, sizeof(kConfirmPattern) / sizeof(kConfirmPattern[0]));
}

void bookmarkBeep() {
    startPattern(kBookmarkPattern, sizeof(kBookmarkPattern) / sizeof(kBookmarkPattern[0]));
}

void errorBeep() {
    startPattern(kErrorPattern, sizeof(kErrorPattern) / sizeof(kErrorPattern[0]));
}

void toggleAudioMode() {
    oink::gApp.buzzerOn = !oink::gApp.buzzerOn;
    if (!oink::gApp.buzzerOn) {
        stopPattern();
        addNotification("SILENT MODE");
        return;
    }
    addNotification("AUDIO ON");
    confirmBeep();
}

void pollControls() {
    unsigned long now = millis();
    bool reading = digitalRead(config::kButtonPin) == LOW;

    if (reading != gButtonLastReading) {
        gButtonLastReading = reading;
        gButtonLastTransitionMs = now;
    }

    if (now - gButtonLastTransitionMs >= config::kButtonDebounceMs && reading != gButtonStablePressed) {
        gButtonStablePressed = reading;
        if (gButtonStablePressed) {
            gButtonPressStartMs = now;
            gLongPressQueued = false;
        } else if (!gLongPressQueued) {
            if (gPendingShortPresses == 1 && now - gLastShortReleaseMs <= config::kButtonDoublePressMs) {
                enqueueEvent(ControlEvent::DoublePress);
                gPendingShortPresses = 0;
                gLastShortReleaseMs = 0;
            } else {
                if (gPendingShortPresses > 0) {
                    enqueueEvent(ControlEvent::ShortPress);
                }
                gPendingShortPresses = 1;
                gLastShortReleaseMs = now;
            }
        }
    }

    if (gButtonStablePressed && !gLongPressQueued && now - gButtonPressStartMs >= config::kButtonLongPressMs) {
        gLongPressQueued = true;
        gPendingShortPresses = 0;
        gLastShortReleaseMs = 0;
        enqueueEvent(ControlEvent::LongPress);
    }

    if (!gButtonStablePressed && gPendingShortPresses == 1 && now - gLastShortReleaseMs >= config::kButtonDoublePressMs) {
        enqueueEvent(ControlEvent::ShortPress);
        gPendingShortPresses = 0;
        gLastShortReleaseMs = 0;
    }
}

bool nextControlEvent(ControlEvent& event) {
    if (gControlHead == gControlTail) {
        event = ControlEvent::None;
        return false;
    }
    event = gControlQueue[gControlHead];
    gControlHead = (gControlHead + 1) % kControlQueueSize;
    return true;
}

void serviceUi() {
    serviceAudio();

    if (!oink::gApp.oledReady) {
        return;
    }

    if (oink::gApp.alertFlashOn && millis() - oink::gApp.lastAlertFlash > config::kAlertFlashMs) {
        oink::gApp.alertFlashOn = false;
    }
    if (millis() - oink::gApp.lastOledRefresh < config::kUiRefreshMs) {
        return;
    }
    oink::gApp.lastOledRefresh = millis();

    gDisplay.clearBuffer();
    gDisplay.setFont(u8g2_font_6x12_tr);
    gDisplay.drawStr(0, 12, "OINKSPY");

    char countBuf[12];
    snprintf(countBuf, sizeof(countBuf), "%d", oink::gApp.detectionCount);
    gDisplay.drawStr(96, 12, countBuf);

    const char* status = "Scanning...";
    if (!oink::scan::isScanningEnabled()) {
        status = "Scan paused";
    } else if (oink::gApp.bleClientConnected || oink::gApp.serialHostConnected) {
        status = "Companion";
    } else if (oink::gApp.deviceInRange) {
        status = "Device near";
    }
    gDisplay.drawStr(0, 28, status);

    gDisplay.setFont(u8g2_font_5x7_tr);
    gDisplay.drawStr(0, 37, alertModeLabel());

    gDisplay.drawVLine(78, 28, 34);
    drawPigBuddy(currentPigMood());

    if (oink::gApp.alertFlashOn) {
        gDisplay.setFont(u8g2_font_9x15B_tr);
        gDisplay.drawStr(0, 55, "DETECTED!");
    } else {
        gDisplay.setFont(u8g2_font_5x7_tr);
        int y = 46;
        for (int i = 0; i < 2; ++i) {
            uint8_t idx = (oink::gApp.notificationHead + 4 - 1 - i) % 4;
            if (oink::gApp.notifications[idx].shortDesc[0]) {
                char trimmed[14];
                copyTrimmed(trimmed, sizeof(trimmed), oink::gApp.notifications[idx].shortDesc, 12);
                gDisplay.drawStr(0, y, trimmed);
                y += 9;
            }
        }
    }

    gDisplay.sendBuffer();
}

} // namespace oink::board

