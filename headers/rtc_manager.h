#pragma once
#include <Arduino.h>
#include <RTClib.h>

// Shared RTC instance — defined in project_ronnie.ino.
extern RTC_DS3231 rtc;

String getFormattedTime();   // e.g. "3:05 PM"
String getFormattedDate();   // e.g. "11 May 2026"
int    getCurrentHour();
int    getCurrentMinute();
