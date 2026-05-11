#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include "config.h"     // AppConfig struct + extern appConfig
#include "face.h"
#include "voice.h"
#include "commands.h"
#include "alarm.h"
#include "reminder.h"
#include "battery.h"
#include "webserver.h"

// dfPlayer declared here; extern in headers that need it
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

RTC_DS3231 rtc;

bool wifiConnected = false;

// AppConfig struct lives in headers/config.h.
// Storage (definition) stays here; every other module uses the extern.
AppConfig appConfig;

// Guard: setupWebServer() / server.begin() must only be called once.
bool serverStarted = false;

void loadConfig() {
    appConfig = { "London", "", 0, 25 };  // safe defaults
    if (!LittleFS.exists("/config.json")) return;
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, f)) {
        appConfig.city           = doc["city"]            | "London";
        appConfig.owmKey         = doc["owm_key"]         | "";
        appConfig.timezoneOffset = doc["timezone_offset"] | 0L;
        appConfig.volume         = doc["volume"]          | 25;
    }
    f.close();
}

// Config button: hold GPIO 4 for 3 s to enter web config
#define CONFIG_BUTTON_PIN 4
static unsigned long configBtnPressTime = 0;

// Good-Morning weather scheduler (millis-based, non-blocking)
unsigned long gmWeatherScheduledAt = 0;

void setup() {
    Serial.begin(115200);

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

    drawFace(IDLE);
    dfPlayer.play(1);
}

void loop() {
    voice_loop();
    reminder_tick();  // fires delayed reminder MP3 when its timer expires

    // Trigger weather after Good Morning greeting has finished playing
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
                // Server already running — just re-display the IP
                String ip = WiFi.localIP().toString();
                showText("Config Mode", ip.c_str(), "in browser");
            }
        }
    } else {
        configBtnPressTime = 0;
    }

    face_idle_tick();
}
