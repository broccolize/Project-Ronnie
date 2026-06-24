#pragma once
#include <Arduino.h>

// V8: Speech-to-text capture + upload module.
// After the wake word fires (or the touch button is pressed), this module
// records a spoken command from the INMP441 (VAD-gated), writes it to the SD
// card as a 16 kHz/16-bit/mono WAV, uploads it to the local Whisper server
// over the LAN, and returns the transcript.

void stt_setup();   // create SD scratch dir if needed (call once at boot)

// Capture a spoken command, upload to appConfig.whisperUrl, return the
// transcript. Returns "" on any failure (no server configured, WiFi down,
// no speech detected, HTTP error, or parse error) — the caller then plays
// the offline / "didn't catch that" clip.
String stt_capture_and_transcribe();
