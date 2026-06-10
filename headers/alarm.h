#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// Shared dfPlayer instance — defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;

void alarm_setup();                   // hardware init + load from disk (call once at boot)
void alarm_reload();                  // reload alarms from disk — call after portal saves
void alarm_set(int hour, int minute);
void alarm_check(DateTime now);
void alarm_ring(int idx);
void alarm_ring_tick();               // non-blocking double-tap poll — call every loop() iteration
void alarm_save();                    // public alias for alarm_save_triggered()
