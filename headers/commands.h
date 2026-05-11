#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// Shared globals — all defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;
extern RTC_DS3231           rtc;
extern bool                 wifiConnected;
extern unsigned long        gmWeatherScheduledAt;

// Dispatch a recognised voice command.
// label     — one of the Edge Impulse category strings
// rawSpeech — full speech transcript (used for alarm/reminder time parsing)
void handleCommand(const char* label, const String& rawSpeech);
