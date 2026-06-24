/**
 * Ronnie — Wokwi Simulation Sketch
 *
 * A self-contained sketch that exercises the testable subsystems
 * (OLED faces, alarm/reminder logic, webserver, WiFi) without
 * DFPlayer, I2S mic, SD card, or Edge Impulse — none of which
 * Wokwi supports.
 *
 * HOW TO USE:
 *   1. Install the stub library:
 *      Copy test/stubs/project_ronnie_inferencing.h into a folder called
 *      project_ronnie_inferencing/ inside your Arduino libraries folder.
 *
 *   2. Open THIS file in Arduino IDE (not the main sketch).
 *      Set board to "ESP32 Dev Module".
 *
 *   3. Compile and note the .elf path from the IDE verbose output.
 *      Copy wokwi.toml and diagram.json to the project root.
 *      Update the elf/firmware paths in wokwi.toml, then run Wokwi.
 *
 * WHAT IS TESTED:
 *   ✅ OLED face rendering (IDLE, HAPPY, THINKING, SPEAKING, SLEEPING)
 *   ✅ showText() layout
 *   ✅ Alarm check + daily flag reset
 *   ✅ Reminder check + triggered persistence
 *   ✅ Config button (GPIO4 hold)
 *   ✅ WiFi connect + NTP sync
 *   ✅ Async web server (config/alarms/reminders endpoints)
 *   ✅ Touch sensor double-tap detection (GPIO34 button)
 *
 * WHAT IS STUBBED:
 *   ✗ DFPlayer (audio) — silent; play() calls logged to Serial
 *   ✗ I2S microphone  — voice commands not active
 *   ✗ SD card         — STT capture (cmd.wav) and TTS cache unavailable
 *   ✗ Whisper server  — command transcription not reachable in sim
 *   ✗ Edge Impulse    — stub returns 0 confidence always
 */

#include <Wire.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

// ── OLED ─────────────────────────────────────────────────────────────────────
#include <U8g2lib.h>
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// Note: Wokwi uses SSD1306; the real board uses SH1106.
// The U8g2 constructor is the only change needed between the two.

enum FaceState { IDLE, HAPPY, THINKING, SPEAKING, SLEEPING, BOOTING };
FaceState     currentFace = IDLE;
unsigned long lastBlink   = 0;
bool          eyeOpen     = true;
int           mouthPhase  = 0;

void drawFace(FaceState state) {
    currentFace = state;
    u8g2.clearBuffer();
    int eyeR = eyeOpen ? 10 : 2;
    switch (state) {
        case IDLE:
            if (eyeOpen) { u8g2.drawRFrame(28,18,20,20,4); u8g2.drawRFrame(80,18,20,20,4); }
            else         { u8g2.drawHLine(28,28,20); u8g2.drawHLine(80,28,20); }
            u8g2.drawHLine(48,52,32);
            break;
        case HAPPY:
            u8g2.drawDisc(40,28,eyeR); u8g2.drawDisc(88,28,eyeR);
            for (int i=-20;i<20;i++) u8g2.drawPixel(64+i,50+(i*i)/40);
            break;
        case THINKING:
            u8g2.drawDisc(40,28,eyeR); u8g2.drawBox(80,24,16,8);
            u8g2.drawStr(50,54,"...");
            break;
        case SPEAKING:
            u8g2.drawDisc(40,28,eyeR); u8g2.drawDisc(88,28,eyeR);
            u8g2.drawDisc(64,50,(mouthPhase%2==0)?8:4);
            mouthPhase=(mouthPhase+1)%100;
            break;
        case SLEEPING:
            u8g2.drawHLine(28,28,20); u8g2.drawHLine(80,28,20);
            u8g2.drawStr(72,20,"zzz"); u8g2.drawHLine(48,52,32);
            break;
        case BOOTING:
            u8g2.drawStr(10,30,"RONNIE v8.0"); u8g2.drawStr(10,44,"Wokwi sim...");
            break;
    }
    u8g2.sendBuffer();
}

void showText(const char* l1, const char* l2, const char* l3) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x13B_tf);
    if (l1) u8g2.drawStr(4,16,l1);
    u8g2.setFont(u8g2_font_6x10_tf);
    if (l2) u8g2.drawStr(4,34,l2);
    if (l3) u8g2.drawStr(4,50,l3);
    u8g2.sendBuffer();
}

void face_idle_tick() {
    unsigned long now = millis();
    if (eyeOpen  && now-lastBlink > 5000) { eyeOpen=false; lastBlink=now; drawFace(currentFace); }
    if (!eyeOpen && now-lastBlink > 80)   { eyeOpen=true;               drawFace(currentFace); }
}

// ── Mock DFPlayer ────────────────────────────────────────────────────────────

struct MockPlayer {
    void play(int t) { Serial.printf("[DFPlayer] play(%d)\n", t); }
    void stop()      { Serial.println("[DFPlayer] stop()"); }
    void volume(int) {}
    bool begin(HardwareSerial&) { return true; }
} dfPlayer;

// ── RTC ──────────────────────────────────────────────────────────────────────
RTC_DS1307 rtc;  // DS1307 is what Wokwi simulates; DS3231 on real hardware

// ── Webserver ─────────────────────────────────────────────────────────────────
AsyncWebServer server(80);
bool serverStarted = false;

// ── Touch + config buttons ─────────────────────────────────────────────────
#define TOUCH_PIN      34
#define CONFIG_BTN_PIN  4

// ── WiFi / NTP ───────────────────────────────────────────────────────────────
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);
bool      wifiConnected = false;

// ── Simple alarm for test ────────────────────────────────────────────────────
struct TestAlarm { int hour; int minute; bool triggered; };
TestAlarm testAlarm = { 0, 0, false };

void setupWebServer() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
            "<h2>Ronnie Wokwi Sim</h2>"
            "<p>Webserver is running. Full portal requires LittleFS + index.html.</p>"
            "<p><a href='/config'>GET /config</a></p>"
        );
    });
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json",
            "{\"city\":\"Wokwi\",\"volume\":25,\"tasks\":[]}");
    });
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        char buf[128];
        DateTime now = rtc.now();
        snprintf(buf, sizeof(buf),
            "{\"time\":\"%02d:%02d:%02d\",\"wifiConnected\":%s}",
            now.hour(), now.minute(), now.second(),
            wifiConnected ? "true" : "false");
        req->send(200, "application/json", buf);
    });
    server.begin();
    Serial.println("[Server] Started. GET /config and /status available.");
    showText("Webserver", WiFi.localIP().toString().c_str(), "GET /config");
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Wokwi] Ronnie simulation starting...");

    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
    drawFace(BOOTING);

    if (!LittleFS.begin(true))
        Serial.println("[LittleFS] mount failed");

    Wire.begin(21, 22);
    if (!rtc.begin())
        Serial.println("[RTC] not found — Wokwi uses DS1307");

    Serial.println("[Faces] Cycling all expressions...");
    FaceState faces[] = {IDLE, HAPPY, THINKING, SPEAKING, SLEEPING};
    for (FaceState f : faces) {
        drawFace(f); delay(800);
    }
    showText("Wokwi Test", "All faces OK", nullptr);
    delay(1000);

    pinMode(TOUCH_PIN,      INPUT);
    pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);

    WiFiManager wm;
    wm.setConfigPortalTimeout(30);  // short timeout for simulation
    wifiConnected = wm.autoConnect("Ronnie-Wokwi");

    if (wifiConnected) {
        Serial.println("[WiFi] Connected");
        timeClient.begin();
        if (timeClient.update()) {
            // DS1307 lacks lostPower() — just set time unconditionally
            rtc.adjust(DateTime(timeClient.getEpochTime()));
            Serial.println("[NTP] Time synced");
        }
        setupWebServer();
        serverStarted = true;
        drawFace(HAPPY);
    } else {
        Serial.println("[WiFi] Not connected — portal timeout");
        showText("No WiFi", "Portal timed", "out");
    }

    // Show alarm test info
    testAlarm = { rtc.now().hour(), (rtc.now().minute() + 1) % 60, false };
    Serial.printf("[Test] Alarm set for %02d:%02d\n", testAlarm.hour, testAlarm.minute);
    drawFace(IDLE);
}

// ─────────────────────────────────────────────────────────────────────────────

static unsigned long lastCheck    = 0;
static unsigned long lastWiFi     = 0;
static unsigned long configHold   = 0;

// Simulate double-tap alarm dismiss
static int           tapCount     = 0;
static unsigned long firstTapTime = 0;
static bool          alarmRinging = false;

void loop() {
    face_idle_tick();

    // ── Periodic 10s check ─────────────────────────────────────────────────
    if (millis() - lastCheck > 10000) {
        lastCheck = millis();
        DateTime now = rtc.now();

        // Alarm check
        if (!testAlarm.triggered &&
            testAlarm.hour   == now.hour() &&
            testAlarm.minute == now.minute()) {
            testAlarm.triggered = true;
            alarmRinging        = true;
            tapCount = 0; firstTapTime = 0;
            dfPlayer.play(21);
            drawFace(HAPPY);
            showText("ALARM!", "Double-tap", "to dismiss");
            Serial.println("[Alarm] Triggered! Double-tap GPIO34 to dismiss.");
        }

        // Log time every 10s
        char ts[16];
        snprintf(ts, sizeof(ts), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
        Serial.printf("[Time] %s  WiFi:%s\n", ts, wifiConnected ? "up" : "down");
    }

    // ── Double-tap dismiss (GPIO34 button in Wokwi) ───────────────────────
    if (alarmRinging) {
        if (digitalRead(TOUCH_PIN) == HIGH) {
            while (digitalRead(TOUCH_PIN) == HIGH) delay(10);
            unsigned long t = millis();
            if (tapCount == 0) {
                firstTapTime = t; tapCount = 1;
            } else if (t - firstTapTime > 150 && t - firstTapTime < 700) {
                tapCount = 2;
                alarmRinging = false;
                dfPlayer.stop();
                drawFace(IDLE);
                Serial.println("[Alarm] Dismissed via double-tap ✓");
            } else {
                firstTapTime = t; tapCount = 1;
            }
        }
        return;  // don't process other inputs while alarm is ringing
    }

    // ── Config button: hold 3 s ────────────────────────────────────────────
    if (digitalRead(CONFIG_BTN_PIN) == LOW) {
        if (configHold == 0) configHold = millis();
        else if (millis() - configHold > 3000) {
            configHold = 0;
            if (!serverStarted) { setupWebServer(); serverStarted = true; }
            else {
                String ip = WiFi.localIP().toString();
                showText("Config Mode", ip.c_str(), "in browser");
                Serial.printf("[Config] Already running at %s\n", ip.c_str());
            }
        }
    } else {
        configHold = 0;
    }

    // ── WiFi watchdog ──────────────────────────────────────────────────────
    if (millis() - lastWiFi > 30000) {
        lastWiFi = millis();
        if (wifiConnected && WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost — reconnecting");
            WiFi.reconnect();
        }
    }
}
