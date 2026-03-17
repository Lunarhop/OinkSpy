#include "oink_board.h"

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <cstring>

#include "oink_config.h"
#include "oink_gnss.h"
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
    Wardrive,
    Error,
    Paused,
    Flock,
};

void serviceAudio();

void drawN3rdSecSkull(int x, int y) {
    gDisplay.drawCircle(x + 18, y + 18, 18, U8G2_DRAW_ALL);
    gDisplay.drawRFrame(x + 3, y + 14, 18, 12, 4);
    gDisplay.drawRFrame(x + 15, y + 14, 18, 12, 4);
    gDisplay.drawBox(x + 19, y + 17, 2, 4);
    gDisplay.drawDisc(x + 12, y + 20, 4, U8G2_DRAW_ALL);
    gDisplay.drawDisc(x + 24, y + 20, 4, U8G2_DRAW_ALL);
    gDisplay.drawTriangle(x + 18, y + 23, x + 14, y + 33, x + 22, y + 33);
    gDisplay.drawRFrame(x + 8, y + 34, 20, 13, 3);
    gDisplay.drawVLine(x + 13, y + 35, 10);
    gDisplay.drawVLine(x + 18, y + 35, 10);
    gDisplay.drawVLine(x + 23, y + 35, 10);
    gDisplay.drawLine(x + 6, y + 41, x + 2, y + 49);
    gDisplay.drawLine(x + 30, y + 41, x + 34, y + 49);
    gDisplay.drawPixel(x + 2, y + 12);
    gDisplay.drawPixel(x + 33, y + 12);
}

void drawStartupSplash() {
    gDisplay.clearBuffer();

    // Glitch streaks inspired by the supplied image.
    for (int i = 0; i < 7; ++i) {
        int y = 4 + i * 8;
        int x = (i % 2 == 0) ? 56 : 66;
        int w = 18 + (i % 3) * 10;
        gDisplay.drawHLine(x, y, w);
        if (i % 2 == 0) {
            gDisplay.drawHLine(x + 8, y + 1, w - 6);
        }
    }

    drawN3rdSecSkull(4, 6);

    gDisplay.setFont(u8g2_font_9x15B_tr);
    gDisplay.drawStr(46, 24, "N3RD");
    gDisplay.drawStr(58, 42, "SEC");

    gDisplay.setFont(u8g2_font_4x6_tr);
    gDisplay.drawStr(10, 56, "Your network.");
    gDisplay.drawStr(58, 56, "Our playground.");

    gDisplay.drawHLine(44, 28, 70);
    gDisplay.drawHLine(52, 30, 55);
    gDisplay.sendBuffer();
}

void showStartupSplash(unsigned long durationMs) {
    drawStartupSplash();
    unsigned long started = millis();
    while (millis() - started < durationMs) {
        serviceAudio();
        delay(5);
    }
}

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
    bool storageExpected = oink::gApp.runtimeConfig.sdLoggingEnabled || oink::gApp.runtimeConfig.wardrive.enabled;
    bool storageProblem =
        (storageExpected && !oink::gApp.sdReady) ||
        (oink::gApp.sdReady && !oink::gApp.sdLoggingHealthy);

    if (!oink::scan::isScanningEnabled()) {
        return PigMood::Paused;
    }
    if (storageProblem) {
        return PigMood::Error;
    }
    if (oink::gApp.wardriveActive) {
        return PigMood::Wardrive;
    }
    return PigMood::Flock;
}

const char* currentModeLabel(PigMood mood) {
    switch (mood) {
        case PigMood::Wardrive:
            return "Wardrive";
        case PigMood::Flock:
            return "Flock drive";
        case PigMood::Error:
            return "Storage issue";
        case PigMood::Paused:
            return "Scan paused";
        case PigMood::Idle:
        default:
            return "Flock drive";
    }
}

void updateStatusLed(PigMood mood) {
    bool ledOn = false;
    unsigned long now = millis();
    switch (mood) {
        case PigMood::Wardrive:
            ledOn = (now % 1200UL) < 700UL;
            break;
        case PigMood::Flock: {
            unsigned long phase = now % 1200UL;
            ledOn = phase < 120UL || (phase >= 220UL && phase < 340UL);
            break;
        }
        case PigMood::Error:
            ledOn = ((now / 180UL) % 2UL) == 0;
            break;
        case PigMood::Paused:
            ledOn = false;
            break;
        case PigMood::Idle:
        default:
            ledOn = ((now / 900UL) % 2UL) == 0;
            break;
    }
    digitalWrite(oink::config::kStatusLedPin, ledOn ? HIGH : LOW);
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
    } else if (mood == PigMood::Error) {
        gDisplay.drawLine(x + 15, y + 13, x + 19, y + 17);
        gDisplay.drawLine(x + 15, y + 17, x + 19, y + 13);
        gDisplay.drawLine(x + 23, y + 13, x + 27, y + 17);
        gDisplay.drawLine(x + 23, y + 17, x + 27, y + 13);
    } else if (mood == PigMood::Flock) {
        gDisplay.drawDisc(x + 17, y + 15, 1, U8G2_DRAW_ALL);
        gDisplay.drawDisc(x + 25, y + 15, 1, U8G2_DRAW_ALL);
        gDisplay.drawLine(x + 15, y + 12, x + 19, y + 11);
        gDisplay.drawLine(x + 23, y + 11, x + 27, y + 12);
    } else {
        gDisplay.drawDisc(x + 17, y + 15, 1, U8G2_DRAW_ALL);
        gDisplay.drawDisc(x + 25, y + 15, 1, U8G2_DRAW_ALL);
    }

    gDisplay.drawRFrame(x + 15, y + 19, 12, 8, 3);
    gDisplay.drawDisc(x + 19, y + 23, 1, U8G2_DRAW_ALL);
    gDisplay.drawDisc(x + 23, y + 23, 1, U8G2_DRAW_ALL);

    switch (mood) {
        case PigMood::Paused:
            gDisplay.drawLine(x + 17, y + 28, x + 20, y + 26);
            gDisplay.drawLine(x + 20, y + 26, x + 22, y + 26);
            gDisplay.drawLine(x + 22, y + 26, x + 25, y + 28);
            gDisplay.setFont(u8g2_font_4x6_tr);
            gDisplay.drawStr(x + 31, y + 9, "z");
            gDisplay.drawStr(x + 35, y + 5, "z");
            break;
        case PigMood::Flock:
            gDisplay.drawLine(x + 18, y + 28, x + 21, y + 30);
            gDisplay.drawLine(x + 21, y + 30, x + 24, y + 28);
            gDisplay.drawLine(x + 26, y + 26, x + 29, y + 23);
            gDisplay.drawLine(x + 34, y + 6, x + 39, y + 2);
            gDisplay.drawLine(x + 34, y + 10, x + 40, y + 10);
            break;
        case PigMood::Wardrive:
            gDisplay.drawLine(x + 18, y + 28, x + 21, y + 26);
            gDisplay.drawLine(x + 21, y + 26, x + 24, y + 28);
            gDisplay.drawLine(x + 29, y + 21, x + 33, y + 19);
            gDisplay.drawLine(x + 31, y + 24, x + 36, y + 22);
            gDisplay.drawPixel(x + 38, y + 21);
            break;
        case PigMood::Error:
            gDisplay.drawLine(x + 17, y + 29, x + 19, y + 27);
            gDisplay.drawLine(x + 19, y + 27, x + 21, y + 26);
            gDisplay.drawLine(x + 21, y + 26, x + 23, y + 27);
            gDisplay.drawLine(x + 23, y + 27, x + 25, y + 29);
            gDisplay.drawLine(x + 35, y + 8, x + 39, y + 4);
            gDisplay.drawLine(x + 39, y + 4, x + 39, y + 11);
            gDisplay.drawDisc(x + 39, y + 14, 1, U8G2_DRAW_ALL);
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
    showStartupSplash(1400);
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
    updateStatusLed(currentPigMood());

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

    PigMood mood = currentPigMood();
    gDisplay.drawStr(0, 28, currentModeLabel(mood));

    gDisplay.setFont(u8g2_font_5x7_tr);
    gDisplay.drawStr(0, 37, alertModeLabel());

    gDisplay.drawVLine(78, 28, 34);
    drawPigBuddy(mood);

    if (oink::gApp.alertFlashOn) {
        gDisplay.setFont(u8g2_font_9x15B_tr);
        gDisplay.drawStr(0, 55, "DETECTED!");
    } else {
        gDisplay.setFont(u8g2_font_5x7_tr);
        gDisplay.drawStr(0, 46, oink::gnss::gpsSeen() ? "GPS: seen" : "GPS: none");

        char satsBuf[16];
        int sats = oink::gnss::satellitesUsed();
        if (sats >= 0) {
            snprintf(satsBuf, sizeof(satsBuf), "Sats: %d", sats);
        } else {
            strlcpy(satsBuf, "Sats: -", sizeof(satsBuf));
        }
        gDisplay.drawStr(0, 55, satsBuf);
    }

    gDisplay.sendBuffer();
}

} // namespace oink::board

