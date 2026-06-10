#include "numtts.h"
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;

// Speak a number 1–59 using the pre-recorded clips in SD folder /03/.
// File naming on the SD card: /03/001.mp3 = "one", /03/023.mp3 = "twenty-three", etc.
// Returns immediately (DFPlayer queues the play internally).
void numtts_speak_int(int value) {
    if (value < 1 || value > 59) return;
    dfPlayer.playFolder(NUMTTS_CLIPS_FOLDER, value);
}

// Speak a temperature value followed by "degrees".
// Handles negatives using clip 060 ("minus") and clips 001–059 for the absolute value.
// Clip 061 = "degrees".
// Example: -5.2 → plays "minus" (060) + delay + "five" (005) + delay + "degrees" (061)
//          23.7 → plays "twenty-three" (023) + delay + "degrees" (061)
void numtts_speak_temperature(float tempC) {
    int t = (int)(tempC + (tempC >= 0 ? 0.5f : -0.5f));  // round to nearest int

    if (t < 0) {
        dfPlayer.playFolder(NUMTTS_CLIPS_FOLDER, 60);  // "minus"
        delay(700);
        t = -t;
    }

    if (t >= 1 && t <= 59) {
        dfPlayer.playFolder(NUMTTS_CLIPS_FOLDER, t);
        delay(900);
    }

    dfPlayer.playFolder(NUMTTS_CLIPS_FOLDER, 61);  // "degrees"
}
