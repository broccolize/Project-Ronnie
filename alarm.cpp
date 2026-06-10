#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include "alarm.h"
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;
extern SemaphoreHandle_t   fsMutex;  // defined in project_ronnie.ino

#define TOUCH_PIN  34
#define MAX_ALARMS 5

struct Alarm {
    int  hour;
    int  minute;
    bool active;
    bool triggeredToday;
};

Alarm alarms[MAX_ALARMS];
int   alarmCount = 0;

// ── Non-blocking ring state ──────────────────────────────────────────────────
static bool          alarmRinging   = false;
static unsigned long ringingStart   = 0;
static int           tapCount_      = 0;
static unsigned long firstTapTime_  = 0;
static bool          touchHeld_     = false;

void alarm_save_triggered() {
    JsonDocument doc;
    JsonArray arr = doc["alarms"].to<JsonArray>();
    for (int i = 0; i < alarmCount; i++) {
        JsonObject a   = arr.add<JsonObject>();
        a["h"]         = alarms[i].hour;
        a["m"]         = alarms[i].minute;
        a["triggered"] = alarms[i].triggeredToday;
    }
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    File f = LittleFS.open("/alarms.json", "w");
    serializeJson(doc, f);
    f.close();
    xSemaphoreGive(fsMutex);
}

void alarm_reload() {
    alarmCount = 0;
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    bool exists = LittleFS.exists("/alarms.json");
    if (!exists) { xSemaphoreGive(fsMutex); return; }
    File f = LittleFS.open("/alarms.json", "r");
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(fsMutex);
    for (JsonObject a : doc["alarms"].as<JsonArray>()) {
        if (alarmCount < MAX_ALARMS) {
            alarms[alarmCount++] = { a["h"], a["m"], true, (bool)(a["triggered"] | false) };
        }
    }
}

void alarm_setup() {
    pinMode(TOUCH_PIN, INPUT);
    alarm_reload();
}

void alarm_set(int hour, int minute) {
    if (alarmCount >= MAX_ALARMS) {
        Serial.println("Max alarms reached");
        dfPlayer.play(99);
        return;
    }
    alarms[alarmCount++] = { hour, minute, true, false };
    alarm_save_triggered();
}

void alarm_check(DateTime now) {
    if (alarmRinging) return;  // already ringing — let alarm_ring_tick() handle it

    // Reset all triggeredToday flags at midnight
    static int lastDay = -1;
    if (now.day() != lastDay) {
        for (int i = 0; i < alarmCount; i++) alarms[i].triggeredToday = false;
        lastDay = now.day();
        alarm_save_triggered();
    }

    for (int i = 0; i < alarmCount; i++) {
        if (alarms[i].active &&
            !alarms[i].triggeredToday &&
            alarms[i].hour   == now.hour() &&
            alarms[i].minute == now.minute() &&
            now.second() < 10) {
            alarms[i].triggeredToday = true;
            alarm_save_triggered();
            alarm_ring(i);
            return;  // one alarm per tick
        }
    }
}

void alarm_ring(int /*idx*/) {
    alarmRinging  = true;
    ringingStart  = millis();
    tapCount_     = 0;
    firstTapTime_ = 0;
    touchHeld_    = false;
    dfPlayer.play(21);
    drawFace(ALARM_RING);
}

// Call every loop() iteration — non-blocking double-tap poll.
void alarm_ring_tick() {
    if (!alarmRinging) return;

    // Auto-dismiss after 60 s
    if (millis() - ringingStart > 60000) {
        alarmRinging = false;
        dfPlayer.stop();
        alarm_save_triggered();
        drawFace(IDLE);
        return;
    }

    bool pinHigh = (digitalRead(TOUCH_PIN) == HIGH);

    if (pinHigh && !touchHeld_) {
        touchHeld_ = true;  // finger down — wait for release to count
    } else if (!pinHigh && touchHeld_) {
        touchHeld_ = false;  // finger released — count the tap
        unsigned long t = millis();
        if (tapCount_ == 0) {
            firstTapTime_ = t;
            tapCount_     = 1;
        } else if (t - firstTapTime_ > 150 && t - firstTapTime_ < 700) {
            // Valid double-tap within window
            alarmRinging = false;
            dfPlayer.stop();
            alarm_save_triggered();
            drawFace(IDLE);
        } else {
            // Too fast or too slow — restart sequence
            firstTapTime_ = t;
            tapCount_     = 1;
        }
    }
}

void alarm_save() { alarm_save_triggered(); }
