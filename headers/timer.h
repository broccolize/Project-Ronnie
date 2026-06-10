#pragma once
#include <Arduino.h>

// Countdown timer and stopwatch module.
// Call timer_loop() every loop() iteration to drive display updates and fire
// the done alert when the countdown reaches zero.

void timer_setup();
void timer_loop();

// ── Countdown timer ──────────────────────────────────────────────────────────
void timer_start(unsigned long seconds);  // start a countdown from 'seconds'
void timer_stop();
bool timer_running();

// ── Stopwatch ────────────────────────────────────────────────────────────────
void stopwatch_start();
void stopwatch_stop();                    // pauses accumulation; call again to resume
void stopwatch_reset();
bool stopwatch_running();
unsigned long stopwatch_elapsed_seconds();
