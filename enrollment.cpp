#include "enrollment.h"
#include <SD.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;

#define ENROLL_SAMPLE_COUNT 16000   // 1 second at 16 kHz, 16-bit
#define MAX_TASKS           20

// All enrolled samples are loaded into RAM at boot so matching is fast
// (no SD reads per inference).
static int16_t enrolledSamples[MAX_TASKS][ENROLL_SAMPLE_COUNT];
static String  enrolledLabels[MAX_TASKS];
static String  enrolledTexts[MAX_TASKS];
static int     enrolledCount = 0;

// Load all .raw voice samples whose tasks are marked enrolled:true in config.json.
void enrollment_setup() {
    enrolledCount = 0;

    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, f);
    f.close();

    JsonArray tasks = doc["tasks"].as<JsonArray>();
    for (JsonObject task : tasks) {
        if (!task["enrolled"].as<bool>()) continue;

        String label = task["label"].as<String>();
        String text  = task["text"].as<String>();
        String path  = String("/samples/") + label + ".raw";

        if (!SD.exists(path)) continue;
        File sf = SD.open(path, FILE_READ);
        if (!sf) continue;

        sf.read((uint8_t*)enrolledSamples[enrolledCount],
                ENROLL_SAMPLE_COUNT * sizeof(int16_t));
        sf.close();

        enrolledLabels[enrolledCount] = label;
        enrolledTexts[enrolledCount]  = text;
        enrolledCount++;

        if (enrolledCount >= MAX_TASKS) break;
    }
    Serial.printf("Enrollment: %d task(s) loaded\n", enrolledCount);
}

// Record one voice sample for the given label:
//   1. Play "Say the word clearly after the beep" (MP3 0084)
//   2. Play the beep (MP3 0085)
//   3. Record 1 second of raw PCM audio from the I2S mic
//   4. Save to /samples/<label>.raw on the SD card
//   5. Mark enrolled:true in config.json and reload into RAM
bool enrollment_record(const String& label) {
    dfPlayer.play(84);  // "Say the word clearly after the beep."
    delay(2500);
    dfPlayer.play(85);  // [beep]
    delay(300);

    int16_t buf[ENROLL_SAMPLE_COUNT];
    size_t  bytesRead = 0;
    i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytesRead, portMAX_DELAY);

    // Save raw PCM to SD
    if (!SD.exists("/samples")) SD.mkdir("/samples");
    String path = String("/samples/") + label + ".raw";
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)buf, sizeof(buf));
    f.close();

    // Mark as enrolled in config.json
    File cf = LittleFS.open("/config.json", "r");
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, cf);
    cf.close();

    for (JsonObject task : doc["tasks"].as<JsonArray>()) {
        if (task["label"].as<String>() == label) {
            task["enrolled"] = true;
            break;
        }
    }
    File cfw = LittleFS.open("/config.json", "w");
    serializeJson(doc, cfw);
    cfw.close();

    enrollment_setup();    // reload updated samples into RAM
    dfPlayer.play(86);     // "Word enrolled! You can now use it."
    return true;
}

// Compare incoming audio against all enrolled samples using cosine similarity.
// Returns the index of the best match if it exceeds the threshold, else -1.
int enrollment_match(int16_t* audio, size_t sampleCount) {
    if (enrolledCount == 0) return -1;

    const float THRESHOLD = 0.60f;  // raise to 0.70 if false accepts; lower to 0.50 if rejects
    float bestScore = 0.0f;
    int   bestIdx   = -1;

    for (int i = 0; i < enrolledCount; i++) {
        double dot = 0, normA = 0, normB = 0;
        size_t len = min(sampleCount, (size_t)ENROLL_SAMPLE_COUNT);

        for (size_t j = 0; j < len; j++) {
            double a = audio[j];
            double b = enrolledSamples[i][j];
            dot   += a * b;
            normA += a * a;
            normB += b * b;
        }
        if (normA == 0.0 || normB == 0.0) continue;

        float score = (float)(dot / (sqrt(normA) * sqrt(normB)));
        if (score > bestScore) {
            bestScore = score;
            bestIdx   = i;
        }
    }

    return (bestScore >= THRESHOLD) ? bestIdx : -1;
}

String enrollment_getMatchedText(int idx) {
    return (idx >= 0 && idx < enrolledCount) ? enrolledTexts[idx] : String("");
}

String enrollment_getMatchedLabel(int idx) {
    return (idx >= 0 && idx < enrolledCount) ? enrolledLabels[idx] : String("");
}
