#pragma once
#include <Arduino.h>

// Start the async web server (GET/POST for /, /reminders, /alarms, /config).
// Call only once — guarded by serverStarted in project_ronnie.ino.
void setupWebServer();
