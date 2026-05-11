#pragma once
#include <U8g2lib.h>
 
// FIX #29 — removed SHOW_TIME, SHOW_WEATHER, SHOW_REMINDER (never implemented)
enum FaceState { IDLE, HAPPY, THINKING, SPEAKING, SLEEPING, BOOTING };
 
void face_setup();
void face_idle_tick();
void drawFace(FaceState state);
void showText(const char* line1, const char* line2, const char* line3);  // FIX #12

