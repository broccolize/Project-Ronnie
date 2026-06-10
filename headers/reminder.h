#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// Shared dfPlayer instance — defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;

// Reminder data structure — declared here so voice.cpp and enrollment.cpp can use it.
struct Reminder {
    int  hour;
    int  minute;
    int  day;
    int  month;
    int  year;
    char text[64];
    int  mp3;
    bool triggered;
};

void reminder_setup();                // load from disk (call once at boot)
void reminder_reload();               // reload reminders from disk — call after portal saves
void reminder_check(DateTime now);    // call every 10 s from loop()
void reminder_tick();                 // call every loop() iteration — fires delayed MP3
void reminder_add_voice(Reminder r);  // V4-1: save a voice-set reminder at runtime
