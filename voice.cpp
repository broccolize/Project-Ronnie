#include <project_ronnie_inferencing.h>  // Edge Impulse exported SDK — not in repo
#include <driver/i2s.h>
#include "voice.h"
#include "commands.h"
#include "face.h"   // needed for drawFace() calls
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;

#define I2S_WS            25
#define I2S_SCK           26
#define I2S_SD            33
#define AUDIO_BUFFER_SIZE 16000

// Heap-allocated to avoid a 32 KB stack overflow
static int16_t*       audioBuffer  = NULL;
static bool           awake        = false;
static unsigned long  awakeUntil   = 0;
static unsigned long  lastVoiceRun = 0;

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

    signal_t         signal;
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
    if (maxScore < 0.80 || bestIdx < 0) return;

    const char* label = ei_classifier_inferencing_categories[bestIdx];

    if (strcmp(label, "hey_ronnie") == 0) {
        awake      = true;
        awakeUntil = millis() + 5000;
        drawFace(HAPPY);
        dfPlayer.play(68);  // "Yes, sorry I kinda passed out"
        return;
    }

    if (!awake || millis() > awakeUntil) {
        awake = false;
        return;
    }
    awake = false;
    handleCommand(label, "");
}
