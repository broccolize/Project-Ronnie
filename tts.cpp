#include "tts.h"
#include "config.h"
#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;
extern AppConfig            appConfig;
extern bool                 wifiConnected;

const char* EL_VOICE_ID  = "21m00Tcm4TlvDq8ikWAM";  // Rachel
const char* TTS_CACHE_DIR = "/tts";

// Track which cache key is currently copied into the DFPlayer slot so we
// skip the SD copy on repeated requests for the same phrase.
static String currentSlotKey = "";

void tts_setup() {
    if (!SD.exists(TTS_CACHE_DIR)) SD.mkdir(TTS_CACHE_DIR);
}

static bool tts_download(const String& text, const String& path) {
    if (!wifiConnected)              return false;
    if (appConfig.ttsKey.isEmpty())  return false;

    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin ElevenLabs cert fingerprint for production
    HTTPClient http;
    String url = String("https://api.elevenlabs.io/v1/text-to-speech/") + EL_VOICE_ID;
    http.begin(client, url);
    http.addHeader("xi-api-key",   appConfig.ttsKey);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept",       "audio/mpeg");
    http.setTimeout(10000);

    JsonDocument req;
    req["text"]     = text;
    req["model_id"] = "eleven_monolingual_v1";
    req["voice_settings"]["stability"]        = 0.5;
    req["voice_settings"]["similarity_boost"] = 0.75;
    String body;
    serializeJson(req, body);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("ElevenLabs error: %d\n", code);
        http.end();
        return false;
    }

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
    return total > 1000;
}

static void copyToSlot(const String& src, const String& key) {
    if (currentSlotKey == key) return;  // already in slot — skip SD copy

    if (SD.exists("/mp3/0200.mp3")) SD.remove("/mp3/0200.mp3");
    File s = SD.open(src,            FILE_READ);
    File d = SD.open("/mp3/0200.mp3", FILE_WRITE);
    if (s && d) {
        uint8_t buf[512];
        while (s.available()) {
            int n = s.read(buf, sizeof(buf));
            d.write(buf, n);
        }
    }
    s.close();
    d.close();
    currentSlotKey = key;
}

void tts_speak(const String& text, const String& cacheKey) {
    String cachePath = String(TTS_CACHE_DIR) + "/" + cacheKey + ".mp3";

    if (!SD.exists(cachePath)) {
        showText("Speaking...", "Generating", "audio...");
        if (!tts_download(text, cachePath)) {
            Serial.println("TTS failed — falling back to static MP3");
            dfPlayer.play(2);
            return;
        }
    }

    copyToSlot(cachePath, cacheKey);
    dfPlayer.play(200);
    drawFace(SPEAKING);
}
