#pragma once
#include <Arduino.h>

// Central config struct — loaded once from /config.json at boot.
// Defined in project_ronnie.ino; extern-declared here so every module
// can include this header instead of forward-declaring manually.
struct AppConfig {
  String city;
  String owmKey;
  long   timezoneOffset;
  int    volume;
};

extern AppConfig appConfig;
