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
  String whisperUrl;      // V8: local Whisper server endpoint, e.g. http://192.168.1.50:8000/transcribe
                          //     empty = voice commands disabled (offline clip plays instead)
};

extern AppConfig appConfig;

// V8: CPU frequency helpers — defined in project_ronnie.ino.
// Raise to 240 MHz around the active wake/capture/upload path, drop back to
// 80 MHz when idle for battery life.
void cpu_fast();   // setCpuFrequencyMhz(240)
void cpu_idle();   // setCpuFrequencyMhz(80)
