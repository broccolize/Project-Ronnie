#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include "reminder.h"
#include "face.h"
#include "config.h"
#include "tts.h"

extern DFRobotDFPlayerMini dfPlayer;
extern SemaphoreHandle_t   fsMutex;
extern AppConfig           appConfig;
extern bool                wifiConnected;

#define MAX_REMINDERS 10

Reminder reminders[MAX_REMINDERS];
int      reminderCount = 0;

void reminder_save_triggered() {
    JsonDocument doc;
    JsonArray arr = doc["reminders"].to<JsonArray>();
    for (int i = 0; i < reminderCount; i++) {
        Reminder& r  = reminders[i];
        JsonObject o = arr.add<JsonObject>();
        o["hour"]      = r.hour;
        o["minute"]    = r.minute;
        o["day"]       = r.day;
        o["month"]     = r.month;
        o["year"]      = r.year;
        o["mp3"]       = r.mp3;
        o["text"]      = r.text;
        o["triggered"] = r.triggered;
    }
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    File f = LittleFS.open("/reminders.json", "w");
    serializeJson(doc, f);
    f.close();
    xSemaphoreGive(fsMutex);
}

void reminder_reload() {
    reminderCount = 0;
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    bool exists = LittleFS.exists("/reminders.json");
    if (!exists) { xSemaphoreGive(fsMutex); return; }
    File f = LittleFS.open("/reminders.json", "r");
    JsonDocument doc;
    deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(fsMutex);
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

void reminder_setup() { reminder_reload(); }

// Small queue so simultaneous reminders don't lose each other (max 3 pending).
#define PENDING_QUEUE_SIZE 3
static int           pendingMp3Queue[PENDING_QUEUE_SIZE] = {};
static unsigned long pendingMp3At[PENDING_QUEUE_SIZE]    = {};
static int           pendingHead = 0;
static int           pendingTail = 0;

static void enqueue_mp3(int mp3, unsigned long fireAt) {
    int next = (pendingTail + 1) % PENDING_QUEUE_SIZE;
    if (next == pendingHead) return;  // queue full — drop oldest would be worse
    pendingMp3Queue[pendingTail] = mp3;
    pendingMp3At[pendingTail]    = fireAt;
    pendingTail = next;
}

// V8/D4: a reminder with mp3 == 0 carries its text in the transcript itself and
// is spoken back via ElevenLabs TTS (no pre-recorded task clip). One deferred
// slot is enough — reminders rarely fire simultaneously, and the on-screen text
// is always shown regardless of whether speech succeeds.
static String        pendingTtsText = "";
static unsigned long pendingTtsAt   = 0;

void reminder_tick() {
    while (pendingHead != pendingTail) {
        if (millis() > pendingMp3At[pendingHead]) {
            dfPlayer.play(pendingMp3Queue[pendingHead]);
            pendingHead = (pendingHead + 1) % PENDING_QUEUE_SIZE;
        } else {
            break;  // queue is ordered by fire time; stop at first future entry
        }
    }

    if (pendingTtsAt > 0 && millis() > pendingTtsAt) {
        unsigned long at = pendingTtsAt;
        pendingTtsAt = 0;
        // Only attempt TTS when online with a key — otherwise the prefix clip
        // plus the on-screen reminder text already conveyed it.
        if (wifiConnected && !appConfig.ttsKey.isEmpty()) {
            String key = "rem_" + String(at);
            tts_speak(pendingTtsText, key);
        }
        pendingTtsText = "";
    }
}

void reminder_check(DateTime now) {
    static int lastDay = -1;
    if (now.day() != lastDay) {
        for (int i = 0; i < reminderCount; i++) reminders[i].triggered = false;
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
            face_set_reminder_text(r.text);
            drawFace(REMINDER_SHOW);
            dfPlayer.play(7);  // "You are forgetting something..."
            if (r.mp3 == 0) {
                // D4: speak the reminder text itself via TTS after the prefix
                pendingTtsText = String(r.text);
                pendingTtsAt   = millis() + 2500;
            } else {
                enqueue_mp3(r.mp3, millis() + 2500);
            }
        }
    }
}

void reminder_add_voice(Reminder r) {
    if (reminderCount >= MAX_REMINDERS) {
        dfPlayer.play(99);
        return;
    }
    r.triggered = false;
    reminders[reminderCount++] = r;
    reminder_save_triggered();
}
