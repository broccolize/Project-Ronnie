# Project Ronnie — ESP32 Desktop Buddy

A voice-activated desktop companion built on the ESP32. Ronnie responds to a wake word, tells the time and date, reports weather, manages alarms and reminders, runs a countdown timer and stopwatch, and speaks responses through a speaker — all with an animated OLED face and a web configuration portal.

**Build cost:** $30–60 USD &nbsp;|&nbsp; **Build time:** 2–3 weekends &nbsp;|&nbsp; **Difficulty:** Intermediate

---

## Features

| Category | What Ronnie can do |
|---|---|
| **Time & Date** | Speaks the current time and date via ElevenLabs TTS (or offline clips as fallback) |
| **Weather** | Fetches conditions and feels-like temperature from OpenWeatherMap; speaks the temperature using offline number clips |
| **Alarms** | Voice-set or portal-set; dismiss with a double-tap on the head |
| **Reminders** | 3-stage voice flow (hour → AM/PM → task word) or full portal setup |
| **Timer** | Countdown timer with large-digit OLED display and spoken alert on completion |
| **Stopwatch** | Start/stop by voice; elapsed time shown on OLED |
| **Battery** | Reports charge level with personality responses; low-battery auto-warning |
| **Good Morning** | Time-aware greeting followed by an automatic weather summary |
| **Smart Idle Screen** | Animated face + live clock top-left + battery icon top-right |
| **TTS** | ElevenLabs online (cached on SD card) with offline number-clip fallback |
| **Auto Geolocation** | Detects city from public IP on first WiFi boot via ip-api.com |
| **Web Portal** | Full config, alarm, reminder, and voice-task management from a browser |
| **WiFi Resilience** | Auto-reconnects every 30 s; NTP sync every hour |

---

## Hardware

| Component | Spec | Purpose |
|---|---|---|
| ESP32 Dev Board | ESP32-WROOM-32, 38-pin | Main MCU |
| OLED Display | 1.3" SH1106 128×64 I²C | Animated face + info display |
| Microphone | INMP441 I²S breakout | Wake word + voice commands |
| Speaker + Amp | DFPlayer Mini + 8 Ω 1 W speaker | Audio output |
| RTC | DS3231 I²C with CR2032 | Keeps time without WiFi |
| LiPo Battery | 3.7 V 2000 mAh JST | Portable power (10–14 h) |
| Charger | TP4056 USB-C with DW01 protection | Safe USB-C charging |
| Regulator | **MCP1700-3302E** (178 mV dropout) | 3.3 V rail ⚠️ see note below |
| Touch Sensor | TTP223 capacitive | Double-tap alarm dismiss |
| MicroSD Module | SPI breakout | MP3 files + TTS cache |
| Schottky Diode | 1N5819 or SS14 | DFPlayer power from LiPo |
| Polyfuse | Resettable 1.5 A | Overcurrent protection |
| Capacitors | 470 µF + 100 nF + 100 µF | Power rail stability |
| Resistors | 1 MΩ ×2 + 1 kΩ ×2 | Battery ADC divider + DFPlayer RX |

> **⚠️ Critical — use MCP1700-3302E, not AMS1117.**
> The AMS1117 requires at least 4.3 V input to maintain 3.3 V output. At partial LiPo charge (3.5–3.8 V) it causes ESP32 brownouts and crashes. The MCP1700 (178 mV dropout) keeps 3.3 V stable down to 3.5 V.

---

## Wiring Quick Reference

| Signal | ESP32 GPIO |
|---|---|
| OLED SDA | 21 |
| OLED SCL | 22 |
| RTC SDA | 21 (shared I²C) |
| RTC SCL | 22 (shared I²C) |
| Mic WS | 25 |
| Mic SCK | 26 |
| Mic SD | 33 |
| Mic L/R | GND |
| DFPlayer TX | 16 (UART2 RX) |
| DFPlayer RX | 17 via 1 kΩ |
| Touch Sensor | 34 |
| Battery ADC | 35 (1 MΩ + 1 MΩ divider) |
| Config Button | 4 |

> Enclose in **plastic (PLA/PETG) only** — metal blocks the ESP32 WiFi antenna.

---

## Software Setup

### 1. Arduino IDE

Install the **esp32 by Espressif Systems v2.x** board package, then install these libraries from the Library Manager:

- `U8g2`
- `DFRobotDFPlayerMini`
- `ArduinoJson` (v7)
- `NTPClient`
- `WiFiManager`
- `ESPAsyncWebServer`
- `RTClib`

### 2. Edge Impulse — Wake Word Model

1. Create a project at [edgeimpulse.com](https://edgeimpulse.com)
2. Record samples for all labels listed below
3. Train and export as an **Arduino library**
4. Place the exported `.zip` in your Arduino libraries folder

<details>
<summary>Full label list</summary>

```
hey_ronnie
time
date
time_and_date
weather
set_alarm
reminder
good_morning
battery
timer
stopwatch
one  two  three  four  five  six
seven  eight  nine  ten  eleven  twelve
am  pm
```

</details>

### 3. API Keys

| Service | Used for | Free tier |
|---|---|---|
| [OpenWeatherMap](https://openweathermap.org/api) | Weather data | 1 000 calls/day |
| [ElevenLabs](https://elevenlabs.io) | Text-to-speech | 10 000 chars/month |

### 4. MicroSD Card

Format the card as FAT32. Create these folders and populate them:

```
/mp3/         ← numbered static response clips (0001.mp3 … 0100.mp3)
/clips/       ← not used by DFPlayer directly
/03/          ← number-word clips for offline TTS
  001.mp3     "one"
  002.mp3     "two"
  ...
  059.mp3     "fifty-nine"
  060.mp3     "minus"
  061.mp3     "degrees"
/tts/         ← auto-created at boot; ElevenLabs audio cache
/samples/     ← auto-created at enroll; voice enrollment raw files
```

See the full MP3 track list in `project_ronnie_v7.docx` section 4.

### 5. LittleFS — Web Portal Files

Upload the contents of the `data/` folder to LittleFS using the [Arduino LittleFS upload plugin](https://github.com/earlephilhower/arduino-littlefs-upload):

```
data/
  index.html          ← web portal UI
  config.example.json ← rename to config.json and fill in your keys
  alarms.json
  reminders.json
```

Rename `config.example.json` to `config.json` and fill in your values before uploading:

```json
{
  "city": "London",
  "timezone_offset": 3600,
  "owm_key": "YOUR_OPENWEATHERMAP_KEY",
  "tts_key": "YOUR_ELEVENLABS_KEY",
  "volume": 25,
  "portal_pin": ""
}
```

> Set `portal_pin` to a short string (e.g. `"1234"`) to require an `X-Ronnie-Pin` header on all write requests to the portal. Leave blank to keep the portal open on your local network.

---

## Project Structure

```
project_ronnie.ino      Main sketch — setup(), loop(), config loading
face.cpp / .h           OLED display — faces, smart idle, timer digits
voice.cpp / .h          I²S mic + Edge Impulse wake word + command dispatch
commands.cpp / .h       Command handler — time, date, weather, alarm, etc.
weather.cpp / .h        OpenWeatherMap API fetch
tts.cpp / .h            ElevenLabs TTS — download, cache, play
numtts.cpp / .h         Offline number-clip chaining (temperature speak-back)
timer.cpp / .h          Countdown timer + stopwatch
alarm.cpp / .h          Alarm storage, check, non-blocking double-tap dismiss
reminder.cpp / .h       Reminder storage, check, voice and portal flow
enrollment.cpp / .h     Voice task enrollment — record + cosine-similarity match
battery.cpp / .h        ADC battery measurement (piecewise LiPo curve)
rtc_manager.cpp / .h    DS3231 formatted time/date helpers
webserver.cpp / .h      ESPAsyncWebServer — config, alarms, reminders, tasks
headers/config.h        AppConfig struct shared across all modules
data/                   LittleFS files — index.html, config.json, JSON stores
test/                   Host-side stubs and mock server for unit testing
FIX_REPORT.md           Detailed changelog from v7 code review
```

---

## First Boot

1. Flash `project_ronnie.ino` and upload LittleFS data
2. Power on — Ronnie displays **"RONNIE v7.0 / Booting..."**
3. Connect your phone/laptop to the **Ronnie-Setup** WiFi network
4. Enter your home WiFi credentials in the captive portal (auto-opens)
5. Ronnie connects, detects your city via IP, syncs NTP, and plays the startup jingle
6. Open `http://<ronnie-ip>` in a browser to access the config portal
   - Hold the config button (GPIO 4) for 3 seconds to re-display the IP on the OLED

---

## Voice Commands

Say **"Hey Ronnie"** first, then any command within 5 seconds:

| Command | Response |
|---|---|
| *"What's the time?"* | Speaks time + shows on OLED |
| *"What's the date?"* | Speaks date + shows on OLED |
| *"What's the weather?"* | Condition + temperature + suggestion |
| *"Good morning"* | Greeting + weather summary after 3 s |
| *"Are you hungry?"* | Battery level with personality |
| *"Reminder"* | Starts 3-stage voice reminder flow |
| *"Stopwatch"* | Starts/stops stopwatch; shows on OLED |
| *"Set alarm"* | Redirects to web portal |
| *"Timer"* | Redirects to web portal |

---

## Web Portal

Navigate to `http://<ronnie-ip>` while on the same WiFi network.

- **Config** — city, timezone, API keys, volume, portal PIN
- **Alarms** — add / remove alarms (hour + minute)
- **Reminders** — add complex reminders with specific date/time and task MP3
- **Tasks** — define and enroll custom voice task words for the reminder flow

---

## License

MIT — see [LICENSE](LICENSE) for details.

---

## Acknowledgements

- [Edge Impulse](https://edgeimpulse.com) — on-device ML for wake word detection
- [ElevenLabs](https://elevenlabs.io) — neural text-to-speech
- [OpenWeatherMap](https://openweathermap.org) — weather data API
- [U8g2](https://github.com/olikraus/u8g2) — OLED graphics library
- [DFRobotDFPlayerMini](https://github.com/DFRobot/DFRobotDFPlayerMini) — MP3 player library
- [ESPAsyncWebServer](https://github.com/ESP-ME/ESPAsyncWebServer) — async web server for ESP32
