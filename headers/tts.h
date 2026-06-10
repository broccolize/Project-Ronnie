#pragma once
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include "config.h"   // AppConfig — needed by tts.cpp

// V4-6: ElevenLabs Text-to-Speech integration.
// Ronnie speaks the actual current time and date dynamically.
// Audio is cached on the SD card so repeated calls at the same time
// play instantly without hitting the API again.

void tts_setup();  // Create /tts cache directory on SD (call once at boot)

// Speak a dynamically generated phrase.
// Checks SD cache first — only calls ElevenLabs API if not cached.
// text     — full sentence to speak (e.g. "The time is 7:30 AM")
// cacheKey — short, filename-safe key (e.g. "time_730am")
void tts_speak(const String& text, const String& cacheKey);
