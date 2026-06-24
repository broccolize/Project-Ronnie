#include "stt.h"
#include "config.h"
#include <SD.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>

extern AppConfig appConfig;
extern bool      wifiConnected;

// ── Tunable VAD / capture constants ───────────────────────────────────────────
// Energy = mean(|sample|) over a frame. Calibrate START/SILENCE to your room:
// watch the Serial energy prints during a few captures and pick thresholds that
// sit comfortably above ambient noise (SILENCE) and at speaking level (START).
#define START_THRESHOLD    1500   // mean abs energy that begins capture
#define SILENCE_THRESHOLD   600   // below this counts as silence
#define SILENCE_HANG_MS     720   // trailing silence (ms) that ends capture
#define MAX_CAPTURE_MS     8000   // hard cap on capture length
#define NO_SPEECH_TIMEOUT  3000   // if speech never starts, abort -> ""

#define FRAME_SAMPLES       512   // ~32 ms at 16 kHz
#define FRAME_MS             32

static const char* WAV_PATH = "/cmd.wav";

void stt_setup() {
    // Initialise the ESP32-attached SPI MicroSD card. tts.cpp also relies on
    // this card (for its TTS cache) but never called SD.begin() — do it here,
    // once, before any module touches SD. Default CS = GPIO5 (ESP32 VSPI SS);
    // change the argument if your MicroSD breakout uses a different CS pin.
    if (!SD.begin()) {
        Serial.println("stt: SD.begin() failed — voice capture and TTS cache unavailable");
    }
}

// ── Little-endian helpers for the WAV header ──────────────────────────────────
static void put32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

// Patch a 4-byte little-endian length at the current file position.
static void writeLE_chunk(File& f, uint32_t v) {
    uint8_t b[4];
    put32(b, v);
    f.write(b, 4);
}

// Write a 44-byte RIFF/WAVE/fmt /data header. dataLen = PCM byte count.
// 16 kHz, 16-bit, mono.
static void writeWavHeader(File& f, uint32_t dataLen) {
    uint8_t h[44];
    memcpy(h + 0,  "RIFF", 4);
    put32(h + 4,  36 + dataLen);          // ChunkSize
    memcpy(h + 8,  "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    put32(h + 16, 16);                     // Subchunk1Size (PCM)
    put16(h + 20, 1);                      // AudioFormat = PCM
    put16(h + 22, 1);                      // NumChannels = mono
    put32(h + 24, 16000);                  // SampleRate
    put32(h + 28, 32000);                  // ByteRate = 16000 * 1 * 2
    put16(h + 32, 2);                      // BlockAlign = 1 * 2
    put16(h + 34, 16);                     // BitsPerSample
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataLen);                // Subchunk2Size
    f.write(h, 44);
}

// ── 1. VAD-gated record from I2S into /cmd.wav ────────────────────────────────
// Returns the number of PCM bytes captured, or 0 if no speech was detected.
static uint32_t record_to_sd() {
    if (SD.exists(WAV_PATH)) SD.remove(WAV_PATH);
    File f = SD.open(WAV_PATH, FILE_WRITE);
    if (!f) { Serial.println("stt: cannot open WAV for write"); return 0; }

    writeWavHeader(f, 0);  // placeholder — patched after capture

    static int16_t frame[FRAME_SAMPLES];
    uint32_t dataLen   = 0;
    bool     started   = false;
    uint32_t silenceMs = 0;
    uint32_t totalMs   = 0;
    uint32_t waitedMs  = 0;

    while (true) {
        size_t bytesRead = 0;
        i2s_read(I2S_NUM_0, frame, sizeof(frame), &bytesRead, pdMS_TO_TICKS(50));
        size_t n = bytesRead / sizeof(int16_t);
        if (n == 0) continue;

        // Frame energy = mean absolute sample value
        uint64_t sum = 0;
        for (size_t i = 0; i < n; i++) sum += abs(frame[i]);
        uint32_t energy = (uint32_t)(sum / n);

        if (!started) {
            waitedMs += FRAME_MS;
            if (energy > START_THRESHOLD) {
                started = true;
            } else if (waitedMs > NO_SPEECH_TIMEOUT) {
                f.close();
                SD.remove(WAV_PATH);
                Serial.println("stt: no speech detected");
                return 0;
            }
            continue;  // don't record pre-roll silence
        }

        // Capturing
        f.write((uint8_t*)frame, n * sizeof(int16_t));
        dataLen += n * sizeof(int16_t);
        totalMs += FRAME_MS;

        if (energy < SILENCE_THRESHOLD) silenceMs += FRAME_MS;
        else                            silenceMs  = 0;

        if (silenceMs > SILENCE_HANG_MS) break;   // end of speech
        if (totalMs   > MAX_CAPTURE_MS)  break;    // hard cap
    }

    // Patch the header now that we know the data length
    f.seek(4);  writeLE_chunk(f, 36 + dataLen);
    f.seek(40); writeLE_chunk(f, dataLen);
    f.close();
    Serial.printf("stt: captured %u PCM bytes\n", dataLen);
    return dataLen;
}

// ── 2. Multipart upload of /cmd.wav to the Whisper server ─────────────────────
// Parses appConfig.whisperUrl (http://host:port/path), streams the WAV as
// multipart/form-data field "file", and returns the parsed transcript ("" on
// any failure).
static String post_wav_to_server(uint32_t fileSize) {
    String url = appConfig.whisperUrl;
    if (!url.startsWith("http://")) return "";   // plain HTTP only on the LAN

    String rest = url.substring(7);              // strip "http://"
    int slash   = rest.indexOf('/');
    String hostPort = (slash < 0) ? rest : rest.substring(0, slash);
    String path     = (slash < 0) ? "/"  : rest.substring(slash);

    String host = hostPort;
    uint16_t port = 8000;
    int colon = hostPort.indexOf(':');
    if (colon >= 0) {
        host = hostPort.substring(0, colon);
        port = (uint16_t)hostPort.substring(colon + 1).toInt();
    }

    WiFiClient client;
    if (!client.connect(host.c_str(), port)) {
        Serial.printf("stt: connect to %s:%u failed\n", host.c_str(), port);
        return "";
    }

    const char* boundary = "----RonnieBoundary";
    String preamble =
        String("--") + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"file\"; filename=\"cmd.wav\"\r\n" +
        "Content-Type: audio/wav\r\n\r\n";
    String trailer = String("\r\n--") + boundary + "--\r\n";

    uint32_t contentLength = preamble.length() + fileSize + trailer.length();

    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
    client.print(String("Content-Length: ") + contentLength + "\r\n");
    client.print("Connection: close\r\n\r\n");

    client.print(preamble);

    // Stream the file body from SD in 512-byte chunks
    File f = SD.open(WAV_PATH, FILE_READ);
    if (!f) { client.stop(); return ""; }
    uint8_t buf[512];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n > 0) client.write(buf, n);
    }
    f.close();

    client.print(trailer);

    // ── Read the response ─────────────────────────────────────────────────────
    unsigned long deadline = millis() + 8000;
    String resp = "";
    while (client.connected() && millis() < deadline) {
        while (client.available()) {
            resp += (char)client.read();
            deadline = millis() + 8000;  // extend while bytes flow
        }
        delay(1);
    }
    client.stop();

    if (resp.isEmpty()) return "";

    // Status line check
    int firstSpace = resp.indexOf(' ');
    if (firstSpace < 0) return "";
    int code = resp.substring(firstSpace + 1, firstSpace + 4).toInt();
    if (code != 200) {
        Serial.printf("stt: server returned %d\n", code);
        return "";
    }

    // Body = everything after the blank line; the JSON is { "text": "..." }
    int bodyStart = resp.indexOf("\r\n\r\n");
    String body = (bodyStart < 0) ? resp : resp.substring(bodyStart + 4);
    int js = body.indexOf('{');
    int je = body.lastIndexOf('}');
    if (js < 0 || je < 0 || je < js) return "";
    body = body.substring(js, je + 1);

    JsonDocument doc;
    if (deserializeJson(doc, body)) return "";
    String text = doc["text"] | "";
    text.trim();
    return text;
}

String stt_capture_and_transcribe() {
    if (appConfig.whisperUrl.isEmpty()) return "";       // voice disabled
    if (!wifiConnected || WiFi.status() != WL_CONNECTED) return "";

    uint32_t bytes = record_to_sd();
    if (bytes == 0) return "";

    return post_wav_to_server(bytes);
}
