#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>       // required for WiFi.localIP()
#include "webserver.h"
#include "face.h"
#include "alarm.h"      // alarm_reload()
#include "reminder.h"   // reminder_reload()

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

    server.begin();
    String ip = WiFi.localIP().toString();
    showText("Config Mode", ip.c_str(), "in browser");
}
