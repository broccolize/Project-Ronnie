#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "webserver.h"
#include "face.h"
#include "alarm.h"
#include "reminder.h"
#include "config.h"

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

    // V8: voice task-word enrollment removed — reminders are set by voice
    // (single-utterance, transcript becomes the text) or via the portal below.

    server.begin();
    String ip = WiFi.localIP().toString();
    showText("Config Mode", ip.c_str(), "in browser");
}
