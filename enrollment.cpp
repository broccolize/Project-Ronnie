#include "enrollment.h"
#include <SD.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;
extern SemaphoreHandle_t   fsMutex;

#define ENROLL_SAMPLE_COUNT 16000   // 1 second at 16 kHz, 16-bit
#define MAX_TASKS           20

// Only labels and display texts are kept in RAM — sample audio stays on the SD
// card and is loaded one at a time during matching (avoids 640 KB static array).
static String enrolledLabels[MAX_TASKS];
static String enrolledTexts[MAX_TASKS];
static int    enrolledCount = 0;

void enrollment_setup() {
    enrolledCount = 0;
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    File f = LittleFS.open("/config.json", "r");
    if (!f) { xSemaphoreGive(fsMutex); return; }
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(fsMutex);

    for (JsonObject task : doc["tasks"].as<JsonArray>()) {
        if (!task["enrolled"].as<bool>()) continue;
        if (enrolledCount >= MAX_TASKS) break;
        enrolledLabels[enrolledCount] = task["label"].as<String>();
        enrolledTexts[enrolledCount]  = task["text"].as<String>();
        enrolledCount++;
    }
    Serial.printf("Enrollment: %d task(s) indexed\n", enrolledCount);
}

bool enrollment_record(const String& label) {
    dfPlayer.play(84);  // "Say the word clearly after the beep."
    delay(2500);
    dfPlayer.play(85);  // [beep]
    delay(300);

    int16_t buf[ENROLL_SAMPLE_COUNT];
    size_t  bytesRead = 0;
    // Use a 2-second timeout so a stuck I2S driver doesn't hang forever
    i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytesRead,
             pdMS_TO_TICKS(2000));

    if (!SD.exists("/samples")) SD.mkdir("/samples");
    String path = String("/samples/") + label + ".raw";
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)buf, sizeof(buf));
    f.close();

    xSemaphoreTake(fsMutex, portMAX_DELAY);
    File cf = LittleFS.open("/config.json", "r");
    JsonDocument doc;
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
    xSemaphoreGive(fsMutex);

    enrollment_setup();
    dfPlayer.play(86);  // "Word enrolled! You can now use it."
    return true;
}

// Compare incoming audio against all enrolled SD samples using cosine similarity.
// DC bias is removed from both signals before comparison to reduce sensitivity to
// microphone offset and ambient volume differences.
// Loads one 32 KB reference buffer at a time — no static large allocation.
int enrollment_match(int16_t* audio, size_t sampleCount) {
    if (enrolledCount == 0) return -1;

    const float THRESHOLD = 0.60f;
    float bestScore = 0.0f;
    int   bestIdx   = -1;

    // Compute DC bias of the incoming audio
    double sumA = 0;
    for (size_t j = 0; j < sampleCount; j++) sumA += audio[j];
    double meanA = sumA / (double)sampleCount;

    // Heap-allocate a single reference buffer reused for every enrolled sample
    int16_t* refBuf = (int16_t*)malloc(ENROLL_SAMPLE_COUNT * sizeof(int16_t));
    if (!refBuf) {
        Serial.println("enrollment_match: malloc failed");
        return -1;
    }

    for (int i = 0; i < enrolledCount; i++) {
        String path = String("/samples/") + enrolledLabels[i] + ".raw";
        File sf = SD.open(path, FILE_READ);
        if (!sf) continue;
        size_t rb = sf.read((uint8_t*)refBuf, ENROLL_SAMPLE_COUNT * sizeof(int16_t));
        sf.close();
        if (rb < ENROLL_SAMPLE_COUNT * sizeof(int16_t)) continue;

        // DC bias of reference
        double sumB = 0;
        for (size_t j = 0; j < ENROLL_SAMPLE_COUNT; j++) sumB += refBuf[j];
        double meanB = sumB / (double)ENROLL_SAMPLE_COUNT;

        // Cosine similarity with DC removed
        double dot = 0, normA = 0, normB = 0;
        size_t len = min(sampleCount, (size_t)ENROLL_SAMPLE_COUNT);
        for (size_t j = 0; j < len; j++) {
            double a = (double)audio[j]  - meanA;
            double b = (double)refBuf[j] - meanB;
            dot   += a * b;
            normA += a * a;
            normB += b * b;
        }
        if (normA == 0.0 || normB == 0.0) continue;

        float score = (float)(dot / (sqrt(normA) * sqrt(normB)));
        if (score > bestScore) { bestScore = score; bestIdx = i; }
    }

    free(refBuf);
    return (bestScore >= THRESHOLD) ? bestIdx : -1;
}

String enrollment_getMatchedText(int idx) {
    return (idx >= 0 && idx < enrolledCount) ? enrolledTexts[idx] : String("");
}

String enrollment_getMatchedLabel(int idx) {
    return (idx >= 0 && idx < enrolledCount) ? enrolledLabels[idx] : String("");
}
