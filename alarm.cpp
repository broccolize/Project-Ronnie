#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include "alarm.h"
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;

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

// Persist the full alarm list (including triggeredToday flags) to disk
void alarm_save_triggered() {
    DynamicJsonDocument doc(512);
    JsonArray arr = doc.createNestedArray("alarms");
    for (int i = 0; i < alarmCount; i++) {
        JsonObject a = arr.createNestedObject();
        a["h"]         = alarms[i].hour;
        a["m"]         = alarms[i].minute;
        a["triggered"] = alarms[i].triggeredToday;
    }
    File f = LittleFS.open("/alarms.json", "w");
    serializeJson(doc, f);
    f.close();
}

// Load alarms from disk into the in-memory array.
// Called by both alarm_setup() and alarm_reload().
void alarm_reload() {
    alarmCount = 0;  // clear existing in-memory state
    if (!LittleFS.exists("/alarms.json")) return;
    File f = LittleFS.open("/alarms.json", "r");
    DynamicJsonDocument doc(512);
    deserializeJson(doc, f);
    f.close();
    for (JsonObject a : doc["alarms"].as<JsonArray>()) {
        if (alarmCount < MAX_ALARMS) {
            alarms[alarmCount++] = { a["h"], a["m"], true, (bool)(a["triggered"] | false) };
        }
    }
}

void alarm_setup() {
    pinMode(TOUCH_PIN, INPUT);  // hardware init — only called once at boot
    alarm_reload();             // load persisted alarms from disk
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
    // Reset all triggeredToday flags at midnight
    static int lastDay = -1;
    if (now.day() != lastDay) {
        for (int i = 0; i < alarmCount; i++) {
            alarms[i].triggeredToday = false;
        }
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
            alarm_save_triggered();  // persist before the blocking ring call
            alarm_ring(i);
        }
    }
}

void alarm_ring(int idx) {
    dfPlayer.play(21);
    drawFace(HAPPY);
    showText("ALARM!", "Double-tap", "to dismiss");

    // Require two taps within a 150–700 ms window to dismiss
    int           tapCount     = 0;
    unsigned long firstTapTime = 0;
    unsigned long start        = millis();

    while (millis() - start < 60000 && tapCount < 2) {
        if (digitalRead(TOUCH_PIN) == HIGH) {
            // Wait for full release before counting
            while (digitalRead(TOUCH_PIN) == HIGH) delay(10);
            unsigned long t = millis();
            if (tapCount == 0) {
                firstTapTime = t;
                tapCount = 1;
            } else if (t - firstTapTime > 150 && t - firstTapTime < 700) {
                tapCount = 2;
                break;
            } else {
                // Too fast or too slow — restart tap sequence
                firstTapTime = t;
                tapCount = 1;
            }
        }
        delay(10);
    }

    dfPlayer.stop();
    // active stays true so the alarm repeats tomorrow.
    // triggeredToday is already set; alarm_check() clears it at midnight.
    alarm_save_triggered();
    drawFace(IDLE);
}

void alarm_save() { alarm_save_triggered(); }  // public alias
