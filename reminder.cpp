#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include "reminder.h"
#include "face.h"

extern DFRobotDFPlayerMini dfPlayer;

#define MAX_REMINDERS 10

// Reminder struct is declared in reminder.h — do not redefine here.

Reminder reminders[MAX_REMINDERS];
int      reminderCount = 0;

// Persist triggered flags so a reboot doesn't re-fire the same reminder
void reminder_save_triggered() {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("reminders");
    for (int i = 0; i < reminderCount; i++) {
        Reminder& r  = reminders[i];
        JsonObject o = arr.createNestedObject();
        o["hour"]      = r.hour;
        o["minute"]    = r.minute;
        o["day"]       = r.day;
        o["month"]     = r.month;
        o["year"]      = r.year;
        o["mp3"]       = r.mp3;
        o["text"]      = r.text;
        o["triggered"] = r.triggered;
    }
    File f = LittleFS.open("/reminders.json", "w");
    serializeJson(doc, f);
    f.close();
}

// Load reminders from disk into the in-memory array.
// Called by both reminder_setup() and reminder_reload().
void reminder_reload() {
    reminderCount = 0;  // clear existing in-memory state
    if (!LittleFS.exists("/reminders.json")) return;
    File f = LittleFS.open("/reminders.json", "r");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, f);
    f.close();
    for (JsonObject r : doc["reminders"].as<JsonArray>()) {
        if (reminderCount >= MAX_REMINDERS) break;
        Reminder rem  = {};
        rem.hour      = r["hour"];
        rem.minute    = r["minute"];
        rem.day       = r["day"];
        rem.month     = r["month"];
        rem.year      = r["year"]      | 0;
        rem.mp3       = r["mp3"]       | 10;
        rem.triggered = r["triggered"] | false;
        strlcpy(rem.text, r["text"] | "Reminder", 64);
        reminders[reminderCount++] = rem;
    }
}

void reminder_setup() {
    reminder_reload();  // load persisted reminders from disk
}

// Non-blocking state machine: plays the task MP3 after the prefix finishes
static int           pendingReminderMp3 = 0;
static unsigned long reminderMp3At     = 0;

void reminder_tick() {
    if (pendingReminderMp3 > 0 && millis() > reminderMp3At) {
        dfPlayer.play(pendingReminderMp3);
        pendingReminderMp3 = 0;
    }
}

void reminder_check(DateTime now) {
    // Clear triggered flags at midnight so daily reminders re-arm
    static int lastDay = -1;
    if (now.day() != lastDay) {
        for (int i = 0; i < reminderCount; i++) {
            reminders[i].triggered = false;
        }
        lastDay = now.day();
        reminder_save_triggered();
    }

    for (int i = 0; i < reminderCount; i++) {
        Reminder& r = reminders[i];
        if (r.triggered) continue;

        bool timeMatch = (r.hour == now.hour() && r.minute == now.minute() && now.second() < 10);
        bool dayMatch  = (r.day   == 0 || r.day   == now.day());
        bool monMatch  = (r.month == 0 || r.month == now.month());
        bool yearMatch = (r.year  == 0 || r.year  == now.year());

        if (timeMatch && dayMatch && monMatch && yearMatch) {
            r.triggered = true;
            reminder_save_triggered();
            showText("REMINDER", r.text, NULL);
            dfPlayer.play(7);  // prefix: "You are forgetting something..."
            // Schedule task MP3 2.5 s later via reminder_tick()
            pendingReminderMp3 = r.mp3;
            reminderMp3At      = millis() + 2500;
            drawFace(SPEAKING);
        }
    }
}

// V4-1: Save a reminder created entirely by voice command at runtime.
// Called from voice.cpp after the 3-stage listening flow completes.
void reminder_add_voice(Reminder r) {
    if (reminderCount >= MAX_REMINDERS) {
        dfPlayer.play(99);  // "Sorry, I didn't catch that" — max reached
        return;
    }
    r.triggered = false;
    reminders[reminderCount++] = r;
    reminder_save_triggered();  // persist to LittleFS immediately
}
