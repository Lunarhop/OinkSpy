#pragma once

namespace oink::board {

enum class ControlEvent {
    None,
    ShortPress,
    LongPress,
};

void initializePins();
void initializeDisplay();
void addNotification(const char* desc);
void bootBeep();
void detectBeep();
void heartbeat();
void confirmBeep();
void bookmarkBeep();
void errorBeep();
void toggleAudioMode();
void pollControls();
bool nextControlEvent(ControlEvent& event);
void serviceUi();

} // namespace oink::board
