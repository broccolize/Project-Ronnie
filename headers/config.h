#pragma once
#include <Arduino.h>

// Central config struct — loaded once from /config.json at boot.
// Defined in project_ronnie.ino; extern-declared here so every module
// can include this header instead of forward-declaring manually.
struct AppConfig {
  String city;
  String owmKey;
  String ttsKey;          // V4-6: ElevenLabs API key
  long   timezoneOffset;
  int    volume;
  String portalPin;       // optional token; if set, write endpoints require X-Ronnie-Pin header
};

extern AppConfig appConfig;
