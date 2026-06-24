/**
 * COMPILATION STUB — project_ronnie_inferencing.h
 *
 * Drop-in replacement for the real Edge Impulse exported SDK.
 * Lets the firmware compile and run on real hardware before the
 * Edge Impulse model is trained.  The classifier always returns 0
 * confidence on every label, so no voice commands will trigger —
 * but everything else (WiFi, webserver, alarms, TTS, portal) works.
 *
 * USAGE:
 *   Copy this file to your Arduino libraries folder:
 *     Windows: Documents\Arduino\libraries\project_ronnie_inferencing\
 *
 *   Or add to Arduino IDE search path via:
 *     Sketch → Include Library → Add .ZIP Library (zip this stubs/ folder)
 *
 * REPLACE WITH:
 *   The real library exported from edgeimpulse.com once your model is trained:
 *     Edge Impulse project → Deployment → Arduino library → Build → Download
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

// ── Label definitions (v8 — two classes only) ───────────────────────────────
// Must match what you name your training labels in Edge Impulse exactly.
// v8 reduced the model to a 2-class wake word; all command/number words moved
// to the laptop Whisper server. Do NOT re-add command labels here.

#define EI_CLASSIFIER_LABEL_COUNT  2

static const char* ei_classifier_inferencing_categories[EI_CLASSIFIER_LABEL_COUNT] = {
    "hey_ronnie",   // wake word
    "noise",        // everything else (ambience, speech, confusers)
};

// ── Signal type ─────────────────────────────────────────────────────────────

typedef struct {
    int16_t* buffer;
    size_t   total_length;
} signal_t;

// ── Classifier result ───────────────────────────────────────────────────────

typedef struct {
    float value;
} ei_classification_t;

typedef struct {
    ei_classification_t classification[EI_CLASSIFIER_LABEL_COUNT];
} ei_impulse_result_t;

// ── Error codes ─────────────────────────────────────────────────────────────

typedef enum {
    EI_IMPULSE_OK    = 0,
    EI_IMPULSE_ERROR = 1,
} EI_IMPULSE_ERROR;

// ── numpy namespace stub ─────────────────────────────────────────────────────

namespace numpy {
    inline int signal_from_buffer(int16_t* buf, size_t len, signal_t* sig) {
        sig->buffer       = buf;
        sig->total_length = len;
        return 0;
    }
}

// ── Classifier stub ──────────────────────────────────────────────────────────
// Always returns 0.0 confidence on every label → no commands fire.
// This is intentional: everything except voice recognition works normally.

inline EI_IMPULSE_ERROR run_classifier(
    signal_t*            signal,
    ei_impulse_result_t* result,
    bool                 debug
) {
    (void)signal; (void)debug;
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        result->classification[i].value = 0.0f;
    }
    return EI_IMPULSE_OK;
}
