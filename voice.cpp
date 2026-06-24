#include <project_ronnie_inferencing.h>  // Edge Impulse exported SDK (2-class wake word) — not in repo
#include <driver/i2s.h>
#include "voice.h"
#include "commands.h"
#include "config.h"
#include "face.h"
#include "stt.h"
#include "alarm.h"
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;
extern RTC_DS3231           rtc;

#define I2S_WS            25
#define I2S_SCK           26
#define I2S_SD            33
#define AUDIO_BUFFER_SIZE 16000

#define TOUCH_PIN         34     // TTP223 — also used by alarm double-tap dismiss
#define WAKE_CHIME        21     // D1: short chime acknowledges the wake word
#define WAKE_STREAK_MIN    2     // require N consecutive positive windows (§6.3)
#define WAKE_THRESHOLD  0.80f    // top-class confidence to count as a wake hit

// Heap-allocated inference buffer
static int16_t* audioBuffer = NULL;

// Consecutive-window counter for wake confirmation (false-accept reduction)
static int wakeStreak = 0;

void voice_setup() {
    i2s_config_t cfg = {
        .mode              = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate       = 16000,
        .bits_per_sample   = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format    = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags  = 0,
        .dma_buf_count     = 8,
        .dma_buf_len       = 512,
        .use_apll          = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk        = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_SCK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) { Serial.printf("I2S install failed: %d\n", err); return; }
    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) { Serial.printf("I2S set_pin failed: %d\n", err); return; }

    audioBuffer = (int16_t*)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audioBuffer) Serial.println("FATAL: audio buffer malloc failed");
}

// Run the full wake body: chime → record → transcribe → route to handleCommand.
// Shared by the wake word and the touch-button trigger (D2).
static void run_wake_capture() {
    cpu_fast();
    drawFace(THINKING);          // LISTENING / capture visual (reuse THINKING)
    dfPlayer.play(WAKE_CHIME);   // D1: short chime, not the old 0068 spoken line
    delay(250);                  // let the chime start before we open the mic

    String text = stt_capture_and_transcribe();
    cpu_idle();

    if (text.isEmpty()) {
        // No server / WiFi down / no speech / server error → offline clip (D5, §9)
        dfPlayer.play(100);
        drawFace(IDLE);
    } else {
        Serial.printf("Transcript: %s\n", text.c_str());
        handleCommand("", text);  // intent derived from the transcript inside handleCommand
    }
}

void voice_loop() {
    if (!audioBuffer) return;

    // ── D2: touch-button trigger ──────────────────────────────────────────────
    // A deliberate press while idle skips the wake word and records directly.
    // Skip while an alarm is ringing — that press belongs to the dismiss logic.
    static bool touchPrev = false;
    bool touchNow = (digitalRead(TOUCH_PIN) == HIGH);
    if (touchNow && !touchPrev && !alarm_is_ringing()) {
        touchPrev = touchNow;
        wakeStreak = 0;
        run_wake_capture();
        return;
    }
    touchPrev = touchNow;

    // Accumulate samples in a static buffer; only run inference once a full
    // 1-second window (16 000 samples) has been collected. Reading small chunks
    // (512 samples, ~32 ms) keeps loop() responsive while ensuring the
    // classifier always receives a complete, non-padded audio window.
    static int16_t accumBuf[AUDIO_BUFFER_SIZE];
    static size_t  accumPos = 0;

    size_t bytesRead = 0;
    size_t toRead    = min((size_t)512, AUDIO_BUFFER_SIZE - accumPos) * sizeof(int16_t);
    i2s_read(I2S_NUM_0, accumBuf + accumPos, toRead,
             &bytesRead, pdMS_TO_TICKS(10));
    accumPos += bytesRead / sizeof(int16_t);

    if (accumPos < AUDIO_BUFFER_SIZE) return;  // not a full window yet

    memcpy(audioBuffer, accumBuf, AUDIO_BUFFER_SIZE * sizeof(int16_t));
    accumPos = 0;

    // ── RUN EDGE IMPULSE (2-class wake word: hey_ronnie / noise) ───────────────
    signal_t            signal;
    ei_impulse_result_t result;
    numpy::signal_from_buffer(audioBuffer, AUDIO_BUFFER_SIZE, &signal);

    EI_IMPULSE_ERROR ei_error = run_classifier(&signal, &result, false);
    if (ei_error != EI_IMPULSE_OK) {
        Serial.printf("run_classifier error: %d\n", ei_error);
        return;
    }

    float maxScore = 0;
    int   bestIdx  = -1;
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > maxScore) {
            maxScore = result.classification[i].value;
            bestIdx  = i;
        }
    }

    const char* label = (bestIdx >= 0)
        ? ei_classifier_inferencing_categories[bestIdx]
        : "";

    // Wake confirmation: require WAKE_STREAK_MIN consecutive positive windows
    // above threshold before firing — the single biggest false-accept reduction.
    if (bestIdx >= 0 && maxScore >= WAKE_THRESHOLD &&
        strcmp(label, "hey_ronnie") == 0) {
        wakeStreak++;
    } else {
        wakeStreak = 0;
        return;
    }

    if (wakeStreak >= WAKE_STREAK_MIN) {
        wakeStreak = 0;
        accumPos   = 0;   // drop any partial window before capturing the command
        run_wake_capture();
    }
}
