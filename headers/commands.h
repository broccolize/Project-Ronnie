#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// Shared globals — all defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;
extern RTC_DS3231           rtc;
extern bool                 wifiConnected;
extern unsigned long        gmWeatherScheduledAt;

// Dispatch a voice command (V8).
// label      — unused; kept so existing call sites compile
// transcript — the full Whisper transcript; intent is derived from this text
void handleCommand(const char* label, const String& transcript);
