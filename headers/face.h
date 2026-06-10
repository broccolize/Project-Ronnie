#pragma once
#include <U8g2lib.h>

enum FaceState {
    IDLE, HAPPY, THINKING, SPEAKING, SLEEPING, BOOTING,
    ALARM_RING,      // ringing alarm with "Double-tap to dismiss"
    REMINDER_SHOW,   // active reminder with task text
    TIMER_FACE,      // countdown timer large-digit display
    STOPWATCH_FACE   // stopwatch large-digit display
};

void face_setup();
void face_idle_tick();
void drawFace(FaceState state);
void showText(const char* line1, const char* line2, const char* line3);

// Update the clock + battery values shown on the smart idle screen.
// Call from the 10-second tick in loop() so the display stays current.
void face_set_status(int hour, int minute, bool isPM, int battPct);

// Store the task text shown when state == REMINDER_SHOW.
void face_set_reminder_text(const char* text);

// Show a large-digit timer/stopwatch readout (label = "TIMER" or "STOPWATCH").
void showTimerText(const char* label, const char* digits);

