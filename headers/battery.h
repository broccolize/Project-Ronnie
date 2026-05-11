#pragma once
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// Shared dfPlayer instance — defined in project_ronnie.ino.
extern DFRobotDFPlayerMini dfPlayer;

void battery_setup();
int  battery_percent();
void battery_auto_warn();
void battery_respond_to_query();
