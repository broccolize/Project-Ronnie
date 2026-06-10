#include "timer.h"
#include "face.h"
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;

// ── Countdown timer ──────────────────────────────────────────────────────────
static bool          timerRunning  = false;
static unsigned long timerEnd      = 0;
static unsigned long lastTimerDisp = 0;

// ── Stopwatch ────────────────────────────────────────────────────────────────
static bool          swRunning = false;
static unsigned long swStart   = 0;
static unsigned long swAccum   = 0;  // ms accumulated before last pause

void timer_setup() {}

void timer_start(unsigned long seconds) {
    timerRunning  = true;
    timerEnd      = millis() + seconds * 1000UL;
    lastTimerDisp = 0;
    drawFace(TIMER_FACE);
}

void timer_stop() {
    timerRunning = false;
    drawFace(IDLE);
}

bool timer_running() { return timerRunning; }

void stopwatch_start() {
    if (!swRunning) {
        swStart   = millis();
        swRunning = true;
        drawFace(STOPWATCH_FACE);
    }
}

void stopwatch_stop() {
    if (swRunning) {
        swAccum  += millis() - swStart;
        swRunning = false;
        drawFace(IDLE);
    }
}

void stopwatch_reset() {
    swRunning = false;
    swAccum   = 0;
    drawFace(IDLE);
}

bool stopwatch_running() { return swRunning; }

unsigned long stopwatch_elapsed_seconds() {
    unsigned long total = swAccum;
    if (swRunning) total += millis() - swStart;
    return total / 1000UL;
}

// Call every loop() iteration to update the display and fire the done alert.
void timer_loop() {
    unsigned long now = millis();

    if (timerRunning) {
        if (now >= timerEnd) {
            timerRunning = false;
            dfPlayer.play(30);  // "Timer done!" — record this clip and place at mp3/0030.mp3
            drawFace(HAPPY);
            return;
        }
        // Refresh display at 2 Hz — avoids thrashing the I2C bus
        if (now - lastTimerDisp >= 500) {
            lastTimerDisp = now;
            unsigned long rem = (timerEnd - now) / 1000UL;
            unsigned long h = rem / 3600;
            unsigned long m = (rem % 3600) / 60;
            unsigned long s = rem % 60;
            char buf[16];
            if (h > 0) snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
            else       snprintf(buf, sizeof(buf), "%02lu:%02lu", m, s);
            showTimerText("TIMER", buf);
        }
        return;
    }

    if (swRunning) {
        if (now - lastTimerDisp >= 500) {
            lastTimerDisp = now;
            unsigned long el = stopwatch_elapsed_seconds();
            unsigned long h  = el / 3600;
            unsigned long m  = (el % 3600) / 60;
            unsigned long s  = el % 60;
            char buf[16];
            if (h > 0) snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
            else       snprintf(buf, sizeof(buf), "%02lu:%02lu", m, s);
            showTimerText("STOPWATCH", buf);
        }
    }
}
