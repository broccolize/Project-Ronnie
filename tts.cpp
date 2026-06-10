#include "tts.h"
#include "config.h"     // AppConfig struct + extern appConfig
#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;
extern AppConfig            appConfig;
extern bool                 wifiConnected;

// ElevenLabs voice ID — Rachel (clear, friendly).
// Browse alternatives at elevenlabs.io/voice-library.
const char* EL_VOICE_ID  = "21m00Tcm4TlvDq8ikWAM";
const char* TTS_CACHE_DIR = "/tts";

void tts_setup() {
    if (!SD.exists(TTS_CACHE_DIR)) {
        SD.mkdir(TTS_CACHE_DIR);
    }
}

// Download TTS audio from ElevenLabs and stream it directly to the SD card.
// Returns true if the file was written and contains plausible audio (>1 KB).
static bool tts_download(const String& text, const String& path) {
    if (!wifiConnected)          return false;
    if (appConfig.ttsKey.isEmpty()) return false;

    WiFiClientSecure client;
    client.setInsecure();  // skip cert verification — acceptable for audio cache

    HTTPClient http;
    String url = String("https://api.elevenlabs.io/v1/text-to-speech/") + EL_VOICE_ID;
    http.begin(client, url);
    http.addHeader("xi-api-key",    appConfig.ttsKey);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Accept",        "audio/mpeg");
    http.setTimeout(10000);

    DynamicJsonDocument req(256);
    req["text"]     = text;
    req["model_id"] = "eleven_monolingual_v1";
    JsonObject vs = req.createNestedObject("voice_settings");
    vs["stability"]       = 0.5;
    vs["similarity_boost"] = 0.75;
    String body;
    serializeJson(req, body);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("ElevenLabs error: %d\n", code);
        http.end();
        return false;
    }

    // Stream response directly to SD card to avoid heap exhaustion
    WiFiClient* stream = http.getStreamPtr();
    File f = SD.open(path, FILE_WRITE);
    if (!f) { http.end(); return false; }

    uint8_t       buf[512];
    size_t        total   = 0;
    unsigned long timeout = millis() + 10000;

    while (http.connected() && millis() < timeout) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            f.write(buf, n);
            total += n;
        } else {
            delay(1);
        }
    }
    f.close();
    http.end();

    Serial.printf("TTS downloaded: %u bytes → %s\n", total, path.c_str());
    return total > 1000;  // sanity check — a valid MP3 is always >1 KB
}

// Main TTS function — called from commands.cpp.
// Checks the SD cache, downloads from ElevenLabs if missing, then plays via DFPlayer.
// Falls back to a static pre-recorded MP3 (track 2) if TTS is unavailable.
void tts_speak(const String& text, const String& cacheKey) {
    String path = String(TTS_CACHE_DIR) + "/" + cacheKey + ".mp3";

    if (!SD.exists(path)) {
        showText("Speaking...", "Generating", "audio...");
        if (!tts_download(text, path)) {
            Serial.println("TTS failed — falling back to static MP3");
            dfPlayer.play(2);  // fallback: "The time is..." (user reads OLED)
            return;
        }
    }

    // DFPlayer plays files from the SD mp3/ folder by track number.
    // We use slot 200 as a dedicated TTS playback slot — copy and play.
    if (SD.exists("/mp3/0200.mp3")) SD.remove("/mp3/0200.mp3");

    File src = SD.open(path,          FILE_READ);
    File dst = SD.open("/mp3/0200.mp3", FILE_WRITE);
    if (src && dst) {
        uint8_t buf[512];
        while (src.available()) {
            int n = src.read(buf, sizeof(buf));
            dst.write(buf, n);
        }
    }
    src.close();
    dst.close();

    dfPlayer.play(200);  // play the TTS audio from slot 200
    drawFace(SPEAKING);
}
