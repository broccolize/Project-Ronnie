#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SD.h>
#include "webserver.h"
#include "face.h"
#include "alarm.h"
#include "reminder.h"
#include "enrollment.h"
#include "config.h"

extern String pendingEnrollLabel;
extern bool   enrollRequested;
extern AppConfig appConfig;

// LittleFS mutex — defined in project_ronnie.ino, protects concurrent access
// between the async-server task (Core 0) and loop() (Core 1).
extern SemaphoreHandle_t fsMutex;

AsyncWebServer server(80);

// Returns true if the request carries a valid portal PIN (or no PIN is set).
static bool pinOk(AsyncWebServerRequest* req) {
    if (appConfig.portalPin.isEmpty()) return true;
    if (!req->hasHeader("X-Ronnie-Pin")) return false;
    return req->header("X-Ronnie-Pin") == appConfig.portalPin;
}

void setupWebServer() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "X-Ronnie-Pin,Content-Type");

    // ── Portal UI ─────────────────────────────────────────────────────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html"))
            req->send(LittleFS, "/index.html", "text/html");
        else
            req->send(200, "text/html",
                "<h2>Ronnie Config</h2><p>Upload index.html via LittleFS.</p>");
    });

    // ── Reminders ─────────────────────────────────────────────────────────────
    server.on("/reminders", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/reminders.json", "application/json");
    });
    server.on("/reminders", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            if (total > 4096) { req->send(413, "text/plain", "Payload too large"); return; }
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                req->send(503, "text/plain", "Device busy"); return;
            }
            File f = LittleFS.open("/reminders.json", "w");
            f.write(data, len);
            f.close();
            xSemaphoreGive(fsMutex);
            reminder_reload();
            req->send(200, "text/plain", "Saved");
        }
    );

    // ── Alarms ────────────────────────────────────────────────────────────────
    server.on("/alarms", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/alarms.json", "application/json");
    });
    server.on("/alarms", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            if (total > 1024) { req->send(413, "text/plain", "Too large"); return; }
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                req->send(503, "text/plain", "Device busy"); return;
            }
            File f = LittleFS.open("/alarms.json", "w");
            f.write(data, len);
            f.close();
            xSemaphoreGive(fsMutex);
            alarm_reload();
            req->send(200, "text/plain", "Saved");
        }
    );

    // ── Device config ─────────────────────────────────────────────────────────
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Serve config without the portal PIN so the key is never exposed
        if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            req->send(503, "text/plain", "Device busy"); return;
        }
        File f = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        deserializeJson(doc, f);
        f.close();
        xSemaphoreGive(fsMutex);
        doc.remove("portal_pin");  // never send the PIN back to the browser
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });
    server.on("/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            if (total > 512) { req->send(413, "text/plain", "Too large"); return; }
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { req->send(400, "text/plain", "Invalid JSON"); return; }
            if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                req->send(503, "text/plain", "Device busy"); return;
            }
            File f = LittleFS.open("/config.json", "w");
            f.write(data, len);
            f.close();
            xSemaphoreGive(fsMutex);
            req->send(200, "text/plain", "Config saved. Restart Ronnie.");
        }
    );

    // ── Tasks (voice enrollment vocabulary) ───────────────────────────────────
    server.on("/tasks", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            req->send(503, "text/plain", "Device busy"); return;
        }
        File f = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        deserializeJson(doc, f);
        f.close();
        xSemaphoreGive(fsMutex);
        String out;
        serializeJson(doc["tasks"], out);
        req->send(200, "application/json", out);
    });

    server.on("/tasks", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            if (total > 256) { req->send(413, "text/plain", "Too large"); return; }
            JsonDocument incoming;
            if (deserializeJson(incoming, (const char*)data, len)) {
                req->send(400, "text/plain", "Invalid JSON"); return;
            }
            if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                req->send(503, "text/plain", "Device busy"); return;
            }
            File f = LittleFS.open("/config.json", "r");
            JsonDocument doc;
            deserializeJson(doc, f);
            f.close();
            if (!doc["tasks"].is<JsonArray>()) doc["tasks"].to<JsonArray>();
            JsonObject t  = doc["tasks"].as<JsonArray>().add<JsonObject>();
            t["label"]    = incoming["label"];
            t["text"]     = incoming["text"];
            t["mp3"]      = incoming["mp3"] | 10;
            t["enrolled"] = false;
            File fw = LittleFS.open("/config.json", "w");
            serializeJson(doc, fw);
            fw.close();
            xSemaphoreGive(fsMutex);
            req->send(200, "text/plain", "Task saved. Now enroll a voice sample.");
        }
    );

    server.on("/tasks/delete", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            JsonDocument incoming;
            if (deserializeJson(incoming, (const char*)data, len)) {
                req->send(400, "text/plain", "Invalid JSON"); return;
            }
            const char* del = incoming["label"] | "";
            if (strlen(del) == 0) { req->send(400, "text/plain", "Missing label"); return; }

            String sPath = String("/samples/") + del + ".raw";
            if (SD.exists(sPath)) SD.remove(sPath);

            if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                req->send(503, "text/plain", "Device busy"); return;
            }
            File f = LittleFS.open("/config.json", "r");
            JsonDocument doc;
            deserializeJson(doc, f);
            f.close();

            // Rebuild the tasks array without the deleted entry
            JsonDocument newDoc;
            newDoc["city"]            = doc["city"];
            newDoc["country"]         = doc["country"];
            newDoc["owm_key"]         = doc["owm_key"];
            newDoc["tts_key"]         = doc["tts_key"];
            newDoc["timezone_offset"] = doc["timezone_offset"];
            newDoc["volume"]          = doc["volume"];
            if (!doc["portal_pin"].isNull()) newDoc["portal_pin"] = doc["portal_pin"];
            JsonArray newTasks = newDoc["tasks"].to<JsonArray>();
            for (JsonObject task : doc["tasks"].as<JsonArray>()) {
                if (strcmp(task["label"] | "", del) != 0) {
                    JsonObject nt  = newTasks.add<JsonObject>();
                    nt["label"]    = task["label"];
                    nt["text"]     = task["text"];
                    nt["mp3"]      = task["mp3"];
                    nt["enrolled"] = task["enrolled"];
                }
            }
            File fw = LittleFS.open("/config.json", "w");
            serializeJson(newDoc, fw);
            fw.close();
            xSemaphoreGive(fsMutex);

            enrollment_setup();
            req->send(200, "text/plain", "Deleted");
        }
    );

    // ── Enrollment trigger ─────────────────────────────────────────────────────
    server.on("/enroll", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!pinOk(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
            JsonDocument incoming;
            if (deserializeJson(incoming, (const char*)data, len)) {
                req->send(400, "text/plain", "Invalid JSON"); return;
            }
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
