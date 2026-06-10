#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "face.h"
#include "voice.h"
#include "commands.h"
#include "alarm.h"
#include "reminder.h"
#include "battery.h"
#include "webserver.h"
#include "enrollment.h"
#include "tts.h"
#include "timer.h"

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

RTC_DS3231 rtc;

bool wifiConnected = false;

AppConfig appConfig;

// Mutex protecting all LittleFS access between loop() (Core 1) and the
// ESPAsyncWebServer task (Core 0).  Initialised in setup() before any task
// or webserver code can run.
SemaphoreHandle_t fsMutex = NULL;

// V4-2: Enrollment flags — set by /enroll endpoint, consumed in loop()
String pendingEnrollLabel = "";
bool   enrollRequested    = false;

bool serverStarted = false;

// Config button: hold GPIO 4 for 3 s to enter web config
#define CONFIG_BUTTON_PIN 4
static unsigned long configBtnPressTime = 0;

// Good-Morning weather scheduler
unsigned long gmWeatherScheduledAt = 0;

void loadConfig() {
    appConfig = { "London", "", "", 0, 25, "" };
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    bool exists = LittleFS.exists("/config.json");
    if (!exists) { xSemaphoreGive(fsMutex); return; }
    File f = LittleFS.open("/config.json", "r");
    if (!f)      { xSemaphoreGive(fsMutex); return; }
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        appConfig.city           = doc["city"]            | "London";
        appConfig.owmKey         = doc["owm_key"]         | "";
        appConfig.ttsKey         = doc["tts_key"]         | "";
        appConfig.timezoneOffset = doc["timezone_offset"] | 0L;
        appConfig.volume         = doc["volume"]          | 25;
        appConfig.portalPin      = doc["portal_pin"]      | "";
    }
    f.close();
    xSemaphoreGive(fsMutex);
}

// Auto geolocation via ip-api.com — called once after WiFi connects.
void autoDetectCity() {
    HTTPClient http;
    http.begin("http://ip-api.com/json");
    http.setTimeout(5000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    JsonDocument geo;
    deserializeJson(geo, http.getStream());
    http.end();

    String city    = geo["city"]    | "";
    String country = geo["country"] | "";
    if (city.isEmpty()) return;

    // Read existing config, patch city + country, write back
    xSemaphoreTake(fsMutex, portMAX_DELAY);
    JsonDocument cfg;
    if (LittleFS.exists("/config.json")) {
        File rf = LittleFS.open("/config.json", "r");
        if (rf) { deserializeJson(cfg, rf); rf.close(); }
    }
    cfg["city"]    = city;
    cfg["country"] = country;
    File wf = LittleFS.open("/config.json", "w");
    serializeJson(cfg, wf);
    wf.close();
    xSemaphoreGive(fsMutex);

    appConfig.city = city;
    Serial.printf("Auto-detected city: %s, %s\n", city.c_str(), country.c_str());
    showText("Location:", city.c_str(), country.c_str());
    delay(2000);
}

void setup() {
    Serial.begin(115200);

    // Create the LittleFS mutex before any task that touches the filesystem
    fsMutex = xSemaphoreCreateMutex();

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed — formatting");
    }

    loadConfig();

    face_setup();
    drawFace(BOOTING);

    dfSerial.begin(9600, SERIAL_8N1, 16, 17);
    if (!dfPlayer.begin(dfSerial)) {
        Serial.println("DFPlayer init failed — check wiring and SD card");
    }
    dfPlayer.volume(appConfig.volume);

    if (!rtc.begin()) {
        Serial.println("RTC not found — check wiring");
    }
    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    timeClient.setTimeOffset(appConfig.timezoneOffset);

    pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wifiConnected = wm.autoConnect("Ronnie-Setup");

    if (wifiConnected) {
        autoDetectCity();
        timeClient.begin();
        if (timeClient.update()) {
            rtc.adjust(DateTime(timeClient.getEpochTime()));
        } else {
            Serial.println("NTP update failed — using RTC time");
        }
        setupWebServer();
        serverStarted = true;
    }

    voice_setup();
    alarm_setup();
    reminder_setup();
    battery_setup();
    enrollment_setup();
    tts_setup();
    timer_setup();

    drawFace(IDLE);
    dfPlayer.play(1);
}

void loop() {
    voice_loop();
    reminder_tick();
    alarm_ring_tick();
    timer_loop();

    // V4-2: Voice enrollment
    if (enrollRequested) {
        enrollRequested = false;
        drawFace(THINKING);
        enrollment_record(pendingEnrollLabel);
        drawFace(IDLE);
    }

    // Trigger weather after Good Morning greeting
    if (gmWeatherScheduledAt > 0 && millis() > gmWeatherScheduledAt) {
        gmWeatherScheduledAt = 0;
        handleCommand("weather", "");
    }

    // 10-second periodic checks
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 10000) {
        lastCheck = millis();
        DateTime now = rtc.now();
        alarm_check(now);
        reminder_check(now);
        battery_auto_warn();

        // Refresh smart idle status bar
        int h   = now.hour();
        bool pm = (h >= 12);
        if (h > 12) h -= 12;
        if (h == 0) h = 12;
        face_set_status(h, now.minute(), pm, battery_percent());
    }

    // WiFi reconnection watchdog
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck > 30000) {
        lastWiFiCheck = millis();
        if (wifiConnected && WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost — reconnecting");
            WiFi.reconnect();
        }
    }

    // NTP sync every hour
    static unsigned long lastNTP = 0;
    if (wifiConnected && millis() - lastNTP > 3600000) {
        lastNTP = millis();
        if (timeClient.update()) {
            rtc.adjust(DateTime(timeClient.getEpochTime()));
        }
    }

    // Config button: hold 3 s to enter config mode
    if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        if (configBtnPressTime == 0) {
            configBtnPressTime = millis();
        } else if (millis() - configBtnPressTime > 3000) {
            configBtnPressTime = 0;
            if (!serverStarted) {
                setupWebServer();
                serverStarted = true;
            } else {
                String ip = WiFi.localIP().toString();
                showText("Config Mode", ip.c_str(), "in browser");
            }
        }
    } else {
        configBtnPressTime = 0;
    }

    face_idle_tick();
}
