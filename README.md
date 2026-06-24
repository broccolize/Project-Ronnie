# Project Ronnie — ESP32 Desktop Buddy

A voice-activated desktop companion built on the ESP32. Ronnie wakes on **"Hey Ronnie"**, then understands a full spoken command via a self-hosted Whisper server on your laptop — tells the time and date, reports weather, manages alarms and reminders, runs a countdown timer and stopwatch, and speaks responses through a speaker — all with an animated OLED face and a web configuration portal.

**Build cost:** $30–60 USD &nbsp;|&nbsp; **Build time:** 2–3 weekends &nbsp;|&nbsp; **Difficulty:** Intermediate

> **v8 architecture.** Recognition is split in two: a 2-class wake word (`hey_ronnie` / `noise`) runs on-device via Edge Impulse, and everything after the wake word is transcribed by a local Whisper server on your laptop. See [Voice Architecture](#voice-architecture-v8) below.

---

## Features

| Category | What Ronnie can do |
|---|---|
| **Time & Date** | Speaks the current time and date via ElevenLabs TTS (or offline clips as fallback) |
| **Weather** | Fetches conditions and feels-like temperature from OpenWeatherMap; speaks the temperature using offline number clips |
| **Alarms** | Set by voice in one sentence (*"set an alarm for 7 PM"*) or via the portal; dismiss with a double-tap on the head |
| **Reminders** | Set by voice in one sentence (*"remind me at 7 PM to take my pills"*) — the spoken phrase becomes the reminder text — or via the portal |
| **Timer** | Set by voice in one sentence (*"set a timer for five minutes"*); large-digit OLED display and spoken alert on completion |
| **Stopwatch** | Start/stop by voice; elapsed time shown on OLED |
| **Battery** | Reports charge level with personality responses; low-battery auto-warning |
| **Good Morning** | Time-aware greeting followed by an automatic weather summary |
| **Smart Idle Screen** | Animated face + live clock top-left + battery icon top-right |
| **TTS** | ElevenLabs online (cached on SD card) with offline number-clip fallback |
| **Auto Geolocation** | Detects city from public IP on first WiFi boot via ip-api.com |
| **Web Portal** | Config, alarm, and reminder management from a browser |
| **WiFi Resilience** | Auto-reconnects every 30 s; NTP sync every hour |

---

## Voice Architecture (v8)

Recognition runs in two stages:

1. **Wake word — on the ESP32.** Edge Impulse runs a tiny **2-class** model (`hey_ronnie` / `noise`). A wake fires only after **2+ consecutive** positive windows above threshold, which sharply cuts false triggers. A short chime acknowledges the wake.
2. **Command — on your laptop.** After the wake (or a touch-button press), the ESP32 records the spoken command (VAD-gated), uploads the WAV over your LAN to a **self-hosted Whisper server**, and gets back plain text. Intent is parsed from that transcript and routed to the existing command handlers.

```
INMP441 ─► Edge Impulse (hey_ronnie / noise) ─► chime ─► record WAV
                                                            │  LAN (HTTP)
                                                            ▼
                          Laptop: FastAPI + faster-whisper (small.en)
                                                            │  { "text": ... }
                                                            ▼
                          ESP32: parse intent ─► time / date / weather /
                          alarm / timer / stopwatch / reminder / battery
```

**Consequences:** voice commands require the laptop server to be reachable over WiFi. With no server configured or reachable, Ronnie plays the "out of network" clip and stays idle (the clock is still visible on the OLED). The number clips are kept for **offline speech output only** (speaking the time/temperature), never for recognition.

> The laptop Whisper server (`server.py`) is a separate component and is **not included in this repo** — see the restructure guide (`RONNIE_v8_RESTRUCTURE_GUIDE.md`, §5) for the reference implementation and run instructions.

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

### 2. Edge Impulse — Wake Word Model (2 classes)

1. Create a project at [edgeimpulse.com](https://edgeimpulse.com)
2. Record samples for just **two** labels (see below)
3. Train and export as an **Arduino library** (the exported include stays `project_ronnie_inferencing.h`)
4. Place the exported `.zip` in your Arduino libraries folder

<details>
<summary>Label list (v8 — two classes only)</summary>

```
hey_ronnie   50+ samples of your own voice, varied distance/tone/speed
noise        200+ samples: room ambience, speech, TV, keyboard, AND
             confusers ("hey buddy", "hey Bonnie", "Ronnie", "hey")
```

The effort goes into the **noise** class — with one positive class, false triggers
are the main failure mode, and they come from weak negatives. All command/number
words from earlier versions are gone; commands are transcribed by Whisper instead.

</details>

### 3. Whisper Server (laptop)

Voice commands are transcribed by a local [faster-whisper](https://github.com/SYSTRAN/faster-whisper) server on your laptop (`small.en`, kept warm in RAM). Stand it up, note the laptop's LAN IP, and set `whisper_url` in the config (or via the portal) to `http://<LAN_IP>:8000/transcribe`. The reference `server.py` and setup steps are in `RONNIE_v8_RESTRUCTURE_GUIDE.md` §5.

### 4. API Keys

| Service | Used for | Free tier |
|---|---|---|
| [OpenWeatherMap](https://openweathermap.org/api) | Weather data | 1 000 calls/day |
| [ElevenLabs](https://elevenlabs.io) | Text-to-speech | 10 000 chars/month |

### 5. MicroSD Card

Format the card as FAT32. Create these folders and populate them:

```
/mp3/         ← numbered static response clips (0001.mp3 … 0100.mp3)
/03/          ← number-word clips for offline speech output
  001.mp3     "one"
  002.mp3     "two"
  ...
  059.mp3     "fifty-nine"
  060.mp3     "minus"
  061.mp3     "degrees"
/tts/         ← auto-created at boot; ElevenLabs audio cache
/cmd.wav      ← auto-created; scratch buffer for the recorded command before upload
```

See the full MP3 track list in `project_ronnie_v7.docx` section 4.

### 6. LittleFS — Web Portal Files

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
  "country": "UK",
  "timezone_offset": 3600,
  "owm_key": "YOUR_OPENWEATHERMAP_KEY",
  "tts_key": "YOUR_ELEVENLABS_KEY",
  "whisper_url": "http://192.168.1.50:8000/transcribe",
  "volume": 25,
  "portal_pin": ""
}
```

> `whisper_url` is your laptop's Whisper server endpoint. Leave it blank to disable voice commands (Ronnie will play the offline clip on every wake).
>
> Set `portal_pin` to a short string (e.g. `"1234"`) to require an `X-Ronnie-Pin` header on all write requests to the portal. Leave blank to keep the portal open on your local network.

---

## Project Structure

```
project_ronnie.ino      Main sketch — setup(), loop(), config loading
face.cpp / .h           OLED display — faces, smart idle, timer digits
voice.cpp / .h          I²S mic + 2-class Edge Impulse wake word + touch trigger
stt.cpp / .h            Command capture (VAD) + multipart WAV upload to Whisper
commands.cpp / .h       Transcript intent parsing — time, date, weather, alarm, etc.
weather.cpp / .h        OpenWeatherMap API fetch
tts.cpp / .h            ElevenLabs TTS — download, cache, play
numtts.cpp / .h         Offline number-clip chaining (temperature speak-back)
timer.cpp / .h          Countdown timer + stopwatch
alarm.cpp / .h          Alarm storage, check, non-blocking double-tap dismiss
reminder.cpp / .h       Reminder storage, check, voice and portal flow
battery.cpp / .h        ADC battery measurement (piecewise LiPo curve)
rtc_manager.cpp / .h    DS3231 formatted time/date helpers
webserver.cpp / .h      ESPAsyncWebServer — config, alarms, reminders
headers/config.h        AppConfig struct + cpu_fast/cpu_idle helpers
data/                   LittleFS files — index.html, config.json, JSON stores
test/                   Host-side stubs and mock server for unit testing
FIX_REPORT.md           Detailed changelog from v7 code review
RONNIE_v8_RESTRUCTURE_GUIDE.md   v8 voice-pipeline restructure spec
```

---

## First Boot

1. Flash `project_ronnie.ino` and upload LittleFS data
2. Power on — Ronnie displays the booting screen
3. Connect your phone/laptop to the **Ronnie-Setup** WiFi network
4. Enter your home WiFi credentials in the captive portal (auto-opens)
5. Ronnie connects, detects your city via IP, syncs NTP, and plays the startup jingle
6. Open `http://<ronnie-ip>` in a browser, set your **Whisper Server URL** (and API keys), and save
   - Hold the config button (GPIO 4) for 3 seconds to re-display the IP on the OLED
7. Start the Whisper server on your laptop so voice commands work

---

## Voice Commands

Say **"Hey Ronnie"**, wait for the chime, then speak a full command. Each command is understood in one shot from the transcript (no follow-up prompts). You can also press the touch button to skip the wake word and record directly.

| Command | Response |
|---|---|
| *"What's the time?"* | Speaks time + shows on OLED |
| *"What's the date?"* | Speaks date + shows on OLED |
| *"How's the weather?"* | Condition + temperature + suggestion |
| *"Good morning"* | Greeting + weather summary after 3 s |
| *"Are you hungry?"* | Battery level with personality |
| *"Set an alarm for 7 PM"* | Saves the alarm and confirms |
| *"Set a timer for five minutes"* | Starts the countdown immediately |
| *"Remind me at 7 PM to take my pills"* | Saves a reminder; the phrase becomes the text |
| *"Start / stop the stopwatch"* | Starts/stops stopwatch; shows on OLED |

> If the Whisper server is unreachable, Ronnie plays the "out of network" clip. An unrecognized phrase plays the "didn't catch that" clip.

---

## Web Portal

Navigate to `http://<ronnie-ip>` while on the same WiFi network.

- **Config** — city, timezone, API keys, **Whisper server URL**, volume, portal PIN
- **Alarms** — add / remove alarms (hour + minute)
- **Reminders** — add complex reminders with specific date/time and note text

---

## License

MIT — see [LICENSE](LICENSE) for details.

---

## Acknowledgements

- [Edge Impulse](https://edgeimpulse.com) — on-device ML for wake word detection
- [faster-whisper](https://github.com/SYSTRAN/faster-whisper) — local speech-to-text for command recognition
- [ElevenLabs](https://elevenlabs.io) — neural text-to-speech
- [OpenWeatherMap](https://openweathermap.org) — weather data API
- [U8g2](https://github.com/olikraus/u8g2) — OLED graphics library
- [DFRobotDFPlayerMini](https://github.com/DFRobot/DFRobotDFPlayerMini) — MP3 player library
- [ESPAsyncWebServer](https://github.com/ESP-ME/ESPAsyncWebServer) — async web server for ESP32
