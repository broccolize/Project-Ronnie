#pragma once
#include <Arduino.h>

// Offline number-clip TTS — chains DFPlayer clips from the /03/ SD folder.
// Clips 001–059 are individual number words ("one" … "fifty-nine").
// Clip 060 = "minus", clip 061 = "degrees".
//
// All speak_ calls are fire-and-forget (DFPlayer queues internally).
// Use numtts_speak_temperature() for weather feels-like output.

#define NUMTTS_CLIPS_FOLDER 3   // SD folder /03/ — change if your SD layout differs

void numtts_speak_int(int value);              // speaks a number 1–59
void numtts_speak_temperature(float tempC);    // speaks value + "degrees", handles negatives
