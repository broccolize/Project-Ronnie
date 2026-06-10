#include <project_ronnie_inferencing.h>  // Edge Impulse exported SDK — not in repo
#include <driver/i2s.h>
#include "voice.h"
#include "commands.h"
#include "face.h"
#include "enrollment.h"   // V4-2: cosine-similarity task matching
#include "reminder.h"     // V4-1: reminder_add_voice()
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;
extern RTC_DS3231           rtc;

#define I2S_WS            25
#define I2S_SCK           26
#define I2S_SD            33
#define AUDIO_BUFFER_SIZE 16000

// Heap-allocated to avoid a 32 KB stack overflow
static int16_t*      audioBuffer  = NULL;
static bool          awake        = false;
static unsigned long awakeUntil   = 0;
static unsigned long lastVoiceRun = 0;

// ── V4-1: VOICE STATE MACHINE ─────────────────────────────────────────────
// Handles the 3-stage reminder conversation:
//   VS_IDLE → (say "reminder") → VS_REMINDER_WAIT_HOUR
//           → (say hour word)  → VS_REMINDER_WAIT_AMPM
//           → (say "am"/"pm")  → VS_REMINDER_WAIT_TASK
//           → (say task word)  → reminder saved → VS_IDLE

enum VoiceState {
    VS_IDLE,
    VS_REMINDER_WAIT_HOUR,
    VS_REMINDER_WAIT_AMPM,
    VS_REMINDER_WAIT_TASK
};

static VoiceState voiceState = VS_IDLE;
static int        pendingHour = -1;

// Map Edge Impulse number keyword labels to integer hours (1–12)
static int labelToHour(const char* label) {
    const char* nums[] = {
        "one","two","three","four","five","six",
        "seven","eight","nine","ten","eleven","twelve"
    };
    for (int i = 0; i < 12; i++) {
        if (strcmp(label, nums[i]) == 0) return i + 1;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────

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
    if (err != ESP_OK) {
        Serial.printf("I2S install failed: %d\n", err);
        return;
    }
    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) {
        Serial.printf("I2S set_pin failed: %d\n", err);
        return;
    }

    audioBuffer = (int16_t*)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audioBuffer) {
        Serial.println("FATAL: audio buffer malloc failed");
    }
}

void voice_loop() {
    if (!audioBuffer) return;

    // Throttle to ~10 Hz to avoid 100% CPU usage
    if (millis() - lastVoiceRun < 100) return;
    lastVoiceRun = millis();

    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, audioBuffer, AUDIO_BUFFER_SIZE * sizeof(int16_t), &bytesRead, 100);

    // Pad with silence if DMA returned a short read
    size_t samplesRead = bytesRead / sizeof(int16_t);
    if (samplesRead < AUDIO_BUFFER_SIZE) {
        memset(audioBuffer + samplesRead, 0,
               (AUDIO_BUFFER_SIZE - samplesRead) * sizeof(int16_t));
    }

    // ── V4-1: TASK STAGE — use enrollment matcher, NOT Edge Impulse ─────────
    // During VS_REMINDER_WAIT_TASK we compare raw audio against SD samples.
    if (voiceState == VS_REMINDER_WAIT_TASK) {
        if (!awake || millis() > awakeUntil) {
            // Timed out waiting for task word
            voiceState = VS_IDLE;
            awake      = false;
            pendingHour = -1;
            dfPlayer.play(99);  // "Sorry, I didn't catch that"
            return;
        }

        int matchIdx = enrollment_match(audioBuffer, AUDIO_BUFFER_SIZE);
        if (matchIdx < 0) return;  // no match yet — keep listening

        String taskText = enrollment_getMatchedText(matchIdx);

        // Build and save the voice-set reminder
        Reminder r = {};
        r.hour   = pendingHour;
        r.minute = 0;
        r.day    = 0;
        r.month  = 0;
        r.year   = 0;
        r.mp3    = 10;
        strlcpy(r.text, taskText.c_str(), sizeof(r.text));

        reminder_add_voice(r);

        dfPlayer.play(20);   // "Got it! Alarm set."
        drawFace(HAPPY);
        voiceState  = VS_IDLE;
        pendingHour = -1;
        awake       = false;
        return;
    }

    // ── RUN EDGE IMPULSE on all other states ─────────────────────────────────
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
    if (maxScore < 0.80f || bestIdx < 0) return;

    const char* label = ei_classifier_inferencing_categories[bestIdx];

    // ── WAKE WORD ─────────────────────────────────────────────────────────────
    if (strcmp(label, "hey_ronnie") == 0) {
        awake      = true;
        awakeUntil = millis() + 5000;
        voiceState = VS_IDLE;  // reset any pending state on new wake
        drawFace(HAPPY);
        dfPlayer.play(68);     // "Yes, sorry I kinda passed out"
        return;
    }

    if (!awake || millis() > awakeUntil) {
        awake = false;
        return;
    }

    // ── V4-1: REMINDER STATE MACHINE ─────────────────────────────────────────

    // Stage 1 — waiting for hour word (one … twelve)
    if (voiceState == VS_REMINDER_WAIT_HOUR) {
        int h = labelToHour(label);
        if (h < 0) {
            dfPlayer.play(99);    // unrecognised — cancel flow
            voiceState = VS_IDLE;
            awake      = false;
            return;
        }
        pendingHour = h;
        voiceState  = VS_REMINDER_WAIT_AMPM;
        dfPlayer.play(81);        // "AM or PM?"
        awakeUntil = millis() + 8000;
        return;
    }

    // Stage 2 — waiting for "am" or "pm"
    if (voiceState == VS_REMINDER_WAIT_AMPM) {
        if (strcmp(label, "pm") == 0 && pendingHour < 12) pendingHour += 12;
        if (strcmp(label, "am") == 0 && pendingHour == 12) pendingHour  = 0;
        voiceState = VS_REMINDER_WAIT_TASK;
        dfPlayer.play(82);        // "What should I remind you about?"
        awakeUntil = millis() + 8000;
        awake      = true;        // stay listening for task word
        return;
    }

    // ── NORMAL COMMANDS (VS_IDLE) ─────────────────────────────────────────────

    // "reminder" command now starts the 3-stage voice flow instead of
    // redirecting to the web portal (portal path still works via webserver.cpp).
    if (strcmp(label, "reminder") == 0) {
        voiceState = VS_REMINDER_WAIT_HOUR;
        dfPlayer.play(80);        // "At what hour?"
        awakeUntil = millis() + 8000;
        return;
    }

    // All other commands — dispatch normally
    awake = false;
    handleCommand(label, "");
}
