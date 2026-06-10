#pragma once
#include <Arduino.h>
#include "reminder.h"

// V4-2: User-defined task vocabulary via on-device voice enrollment.
// Users define task words in the web portal, record one voice sample each,
// and Ronnie matches them using cosine similarity — no internet required.

// Load all enrolled voice samples from SD card into RAM (call once at boot).
void enrollment_setup();

// Record a 1-second voice sample for the given label and save to SD card.
// Also marks the task as enrolled in config.json and reloads into RAM.
// Returns true on success.
bool enrollment_record(const String& label);

// Compare incoming audio against all enrolled samples using cosine similarity.
// Returns the index of the best match (>= 60% threshold), or -1 if no match.
int enrollment_match(int16_t* audioBuffer, size_t sampleCount);

// Return the reminder text string for a matched index (e.g. "Take pills").
String enrollment_getMatchedText(int matchedIndex);

// Return the label string for a matched index (e.g. "pills").
String enrollment_getMatchedLabel(int matchedIndex);
