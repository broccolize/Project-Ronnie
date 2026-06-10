#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>       // required for WiFi.localIP()
#include <SD.h>         // V4-2: delete enrolled sample files from SD
#include "webserver.h"
#include "face.h"
#include "alarm.h"      // alarm_reload()
#include "reminder.h"   // reminder_reload()
#include "enrollment.h" // V4-2: enrollment_setup() reload after task changes

// V4-2: Flags written here, consumed in loop() of project_ronnie.ino
extern String pendingEnrollLabel;
extern bool   enrollRequested;

AsyncWebServer server(80);

void setupWebServer() {
    // Allow browser-side JS to make requests from any origin
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");

    // ── Config portal UI ──────────────────────────────────────────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html",
                "<h2>Ronnie Config</h2><p>Upload index.html via LittleFS.</p>");
        }
    });

    // ── Reminders ─────────────────────────────────────────────────────────
    server.on("/reminders", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/reminders.json", "application/json");
    });
    server.on("/reminders", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > 4096) { req->send(413, "text/plain", "Payload too large"); return; }
            DynamicJsonDocument doc(4096);
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            File f = LittleFS.open("/reminders.json", "w");
            f.write(data, len);
            f.close();
            reminder_reload();  // sync in-memory state — no reboot needed
            req->send(200, "text/plain", "Saved");
        }
    );

    // ── Alarms ────────────────────────────────────────────────────────────
    server.on("/alarms", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/alarms.json", "application/json");
    });
    server.on("/alarms", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > 1024) { req->send(413, "text/plain", "Too large"); return; }
            DynamicJsonDocument doc(1024);
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            File f = LittleFS.open("/alarms.json", "w");
            f.write(data, len);
            f.close();
            alarm_reload();     // sync in-memory state — no reboot needed
            req->send(200, "text/plain", "Saved");
        }
    );

    // ── Device config ─────────────────────────────────────────────────────
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/config.json", "application/json");
    });
    server.on("/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > 512) { req->send(413, "text/plain", "Too large"); return; }
            // Validate JSON before writing — prevents corrupting config.json
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            File f = LittleFS.open("/config.json", "w");
            f.write(data, len);
            f.close();
            // City, timezone, volume all require a restart to take full effect
            req->send(200, "text/plain", "Config saved. Restart Ronnie.");
        }
    );

    // ── V4-2: Tasks (voice enrollment vocabulary) ─────────────────────────────

    // GET /tasks — return the tasks array from config.json as JSON
    server.on("/tasks", HTTP_GET, [](AsyncWebServerRequest* req) {
        File f = LittleFS.open("/config.json", "r");
        if (!f) { req->send(500, "text/plain", "Config not found"); return; }
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, f);
        f.close();
        String out;
        serializeJson(doc["tasks"], out);
        req->send(200, "application/json", out);
    });

    // POST /tasks — append a new task definition (label + text) to config.json
    server.on("/tasks", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > 256) { req->send(413, "text/plain", "Too large"); return; }
            DynamicJsonDocument incoming(256);
            if (deserializeJson(incoming, (char*)data)) {
                req->send(400, "text/plain", "Invalid JSON");
                return;
            }
            File f = LittleFS.open("/config.json", "r");
            DynamicJsonDocument doc(4096);
            deserializeJson(doc, f);
            f.close();
            if (!doc.containsKey("tasks")) doc.createNestedArray("tasks");
            JsonObject t = doc["tasks"].as<JsonArray>().createNestedObject();
            t["label"]    = incoming["label"];
            t["text"]     = incoming["text"];
            t["mp3"]      = incoming["mp3"] | 10;
            t["enrolled"] = false;
            File fw = LittleFS.open("/config.json", "w");
            serializeJson(doc, fw);
            fw.close();
            req->send(200, "text/plain", "Task saved. Now enroll a voice sample.");
        }
    );

    // POST /tasks/delete — remove a task by label from config.json + delete SD sample
    server.on("/tasks/delete", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            DynamicJsonDocument incoming(128);
            deserializeJson(incoming, (char*)data);
            const char* del = incoming["label"];

            // Delete voice sample from SD if it exists
            String sPath = String("/samples/") + del + ".raw";
            if (SD.exists(sPath)) SD.remove(sPath);

            // Rebuild config.json without the deleted task
            File f = LittleFS.open("/config.json", "r");
            DynamicJsonDocument doc(4096);
            deserializeJson(doc, f);
            f.close();

            DynamicJsonDocument newDoc(4096);
            newDoc["city"]             = doc["city"];
            newDoc["country"]          = doc["country"];
            newDoc["owm_key"]          = doc["owm_key"];
            newDoc["tts_key"]          = doc["tts_key"];
            newDoc["timezone_offset"]  = doc["timezone_offset"];
            newDoc["volume"]           = doc["volume"];
            JsonArray newTasks = newDoc.createNestedArray("tasks");
            for (JsonObject task : doc["tasks"].as<JsonArray>()) {
                if (strcmp(task["label"], del) != 0) {
                    JsonObject nt    = newTasks.createNestedObject();
                    nt["label"]      = task["label"];
                    nt["text"]       = task["text"];
                    nt["mp3"]        = task["mp3"];
                    nt["enrolled"]   = task["enrolled"];
                }
            }
            File fw = LittleFS.open("/config.json", "w");
            serializeJson(newDoc, fw);
            fw.close();

            enrollment_setup();  // reload in-RAM samples without the deleted entry
            req->send(200, "text/plain", "Deleted");
        }
    );

    // POST /enroll — trigger voice recording for a label on the next loop() tick
    server.on("/enroll", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            DynamicJsonDocument incoming(128);
            deserializeJson(incoming, (char*)data);
            pendingEnrollLabel = incoming["label"].as<String>();
            enrollRequested    = true;
            req->send(200, "text/plain",
                "Enrollment started. Ronnie will beep — say the word now.");
        }
    );

    server.begin();
    String ip = WiFi.localIP().toString();
    showText("Config Mode", ip.c_str(), "in browser");
}
