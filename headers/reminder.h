#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// Shared dfPlayer instance — defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;

void reminder_setup();                // load from disk (call once at boot)
void reminder_reload();               // reload reminders from disk — call after portal saves
void reminder_check(DateTime now);    // call every 10 s from loop()
void reminder_tick();                 // call every loop() iteration — fires delayed MP3
