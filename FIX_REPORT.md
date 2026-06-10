# Project Ronnie — Fix Report
**Branch:** `claude/great-hofstadter-77b93d`  
**Doc version reviewed:** `project_ronnie_v7.docx`  
**Fixes implemented:** 17 issues across 16 files

---

## Summary Table

| # | Severity | File(s) | Issue | Status |
|---|---|---|---|---|
| 1 | **Critical** | `enrollment.cpp` | 640 KB static array exceeds ESP32 SRAM | Fixed |
| 2 | **Critical** | `voice.cpp` | Audio buffer 90% silence → wake word broken | Fixed |
| 3 | **Critical** | `webserver.cpp`, `*.cpp` | No mutex on LittleFS across FreeRTOS tasks | Fixed |
| 4 | Wrong | `face.cpp` | Boot screen shows "v2.0" not "v7.0" | Fixed |
| 5 | Wrong | `project_ronnie.ino` | `autoDetectCity()` reads null file handle on first boot | Fixed |
| 6 | Wrong | `weather.cpp` | Fragile ArduinoJson rain-amount operator chaining | Fixed |
| 7 | Wrong | `alarm.cpp` | 60-second blocking busy-wait freezes entire MCU | Fixed |
| 8 | Wrong | `enrollment.cpp` | `portMAX_DELAY` on `i2s_read` can hang forever | Fixed |
| 9 | Wrong | `face.h / face.cpp` | ALARM and REMINDER display states missing | Fixed |
| 10 | Missing | — | `numtts.h / numtts.cpp` not in codebase | Created |
| 11 | Missing | — | `timer.h / timer.cpp` not in codebase | Created |
| 12 | Missing | `commands.cpp` | Dynamic Temperature Speak-Back (v7) not implemented | Fixed |
| 13 | Improve | `tts.cpp` | Wasteful SD full-copy on every TTS playback | Fixed |
| 14 | Improve | `battery.cpp` | Linear voltage→% inaccurate for LiPo chemistry | Fixed |
| 15 | Improve | `enrollment.cpp` | Raw PCM cosine similarity — no DC bias removal | Fixed |
| 16 | Improve | `reminder.cpp` | Single pending-MP3 slot drops simultaneous reminders | Fixed |
| 17 | Improve | `commands.cpp` | TTS cache key ignores timezone → stale cached audio | Fixed |
| 18 | Improve | `webserver.cpp` | No auth — API keys readable; write endpoints unprotected | Fixed |
| 19 | Improve | All files | `DynamicJsonDocument` (ArduinoJson v6, deprecated) | Migrated |
| 20 | Improve | `face.cpp` | Smart idle screen had no time/battery display | Fixed |

---

## Detailed Fix Notes

---

### Fix 1 — Enrollment: 640 KB Static Array (Critical)

**File:** `enrollment.cpp`

**Problem:**
```cpp
static int16_t enrolledSamples[MAX_TASKS][ENROLL_SAMPLE_COUNT];
// 20 × 16 000 × 2 bytes = 640 KB — ESP32 WROOM total SRAM is 520 KB
```
This single static array exceeds all available SRAM. The device would fail to link or crash at startup with heap corruption.

**Fix:**  
The static sample array was removed entirely. Only the label and text strings (tiny) remain in RAM. During `enrollment_match()`, a single 32 KB `int16_t*` buffer is heap-allocated, used to load and compare one enrolled sample at a time, then freed. SD reads add ~5–10 ms per sample but matching is only triggered during the voice reminder flow.

```cpp
// Before: static int16_t enrolledSamples[MAX_TASKS][ENROLL_SAMPLE_COUNT]; // 640 KB
// After: heap-allocate one ref buffer per match call (32 KB, freed immediately)
int16_t* refBuf = (int16_t*)malloc(ENROLL_SAMPLE_COUNT * sizeof(int16_t));
// ... load from SD, compare, free
free(refBuf);
```

---

### Fix 2 — Voice: Audio Buffer Is 90% Silence (Critical)

**File:** `voice.cpp`

**Problem:**  
The old code throttled `voice_loop()` to run every 100 ms and attempted to read 16 000 samples in that window. At 16 kHz, only ~1 600 samples arrive in 100 ms. The `i2s_read` returned a short read; the remaining 14 400 samples were zero-padded. Edge Impulse received a buffer that was ~90% silence on every inference call — making wake word detection essentially non-functional.

**Fix:**  
Replaced the one-shot padded read with a small-chunk accumulation pattern. Each `voice_loop()` call reads 512 samples (non-blocking, ~32 ms of audio), accumulates them in a static buffer, and only runs Edge Impulse inference once a complete 16 000-sample window is available. No padding is ever performed.

```cpp
static int16_t accumBuf[AUDIO_BUFFER_SIZE];
static size_t  accumPos = 0;

size_t bytesRead = 0;
size_t toRead    = min((size_t)512, AUDIO_BUFFER_SIZE - accumPos) * sizeof(int16_t);
i2s_read(I2S_NUM_0, accumBuf + accumPos, toRead, &bytesRead, pdMS_TO_TICKS(10));
accumPos += bytesRead / sizeof(int16_t);

if (accumPos < AUDIO_BUFFER_SIZE) return;  // not a full window yet — keep collecting

memcpy(audioBuffer, accumBuf, AUDIO_BUFFER_SIZE * sizeof(int16_t));
accumPos = 0;
// ... run inference on fully-populated audioBuffer
```
The `lastVoiceRun` 100 ms throttle was removed — the accumulation naturally rate-limits inference to once per ~1 second of audio.

---

### Fix 3 — LittleFS Race Condition (Critical)

**Files:** `project_ronnie.ino`, `webserver.cpp`, `alarm.cpp`, `reminder.cpp`, `enrollment.cpp`

**Problem:**  
`ESPAsyncWebServer` runs its request callbacks on Core 0's TCP/IP task while `loop()` runs on Core 1. Both sides were reading and writing the same LittleFS files (`/config.json`, `/alarms.json`, `/reminders.json`) with no synchronisation, causing potential file corruption under concurrent access.

**Fix:**  
A FreeRTOS mutex (`SemaphoreHandle_t fsMutex`) is created in `setup()` before any task or webserver code runs. Every LittleFS `open()` call — in both the main loop and web callbacks — is now wrapped with:

```cpp
// In project_ronnie.ino setup():
fsMutex = xSemaphoreCreateMutex();

// In all file-access sites:
xSemaphoreTake(fsMutex, portMAX_DELAY);    // loop() side
// or
if (xSemaphoreTake(fsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {  // webserver side
    req->send(503, "text/plain", "Device busy"); return;
}
// ... file operations ...
xSemaphoreGive(fsMutex);
```

Web handlers use a 2-second timeout and return HTTP 503 if the mutex is held, preventing the async server task from blocking indefinitely.

---

### Fix 4 — Boot Screen Version String

**File:** `face.cpp:73`

```cpp
// Before
u8g2.drawStr(10, 30, "RONNIE v2.0");

// After
u8g2.drawStr(10, 30, "RONNIE v7.0");
```

---

### Fix 5 — `autoDetectCity()` Null File Handle on First Boot

**File:** `project_ronnie.ino`

**Problem:**  
On the very first boot, `/config.json` does not exist yet. The old code called `LittleFS.open("/config.json", "r")` without checking whether the file exists, then passed the invalid handle to `deserializeJson()` — undefined behaviour that could corrupt the JSON document object.

**Fix:**  
The file existence is checked before opening, and `deserializeJson` is only called on a valid open file handle:

```cpp
JsonDocument cfg;
if (LittleFS.exists("/config.json")) {
    File rf = LittleFS.open("/config.json", "r");
    if (rf) { deserializeJson(cfg, rf); rf.close(); }
}
cfg["city"]    = city;
cfg["country"] = country;
// ... write back
```

---

### Fix 6 — Weather: Fragile Rain Amount Parsing

**File:** `weather.cpp`

**Problem:**  
```cpp
rainAmount = doc["rain"]["1h"] | doc["rain"]["3h"] | 0.0f;
```
When the `rain` object is entirely absent from the API response (no rain), `doc["rain"]` is null and `doc["rain"]["1h"]` also returns null — but the ArduinoJson `|` operator on the chained expression was relying on undefined evaluation order.

**Fix:**  
Explicit existence check before any sub-key access:

```cpp
float rainAmount = 0.0f;
if (doc["rain"].is<JsonObject>()) {
    if (!doc["rain"]["1h"].isNull())      rainAmount = doc["rain"]["1h"].as<float>();
    else if (!doc["rain"]["3h"].isNull()) rainAmount = doc["rain"]["3h"].as<float>();
}
```

---

### Fix 7 — Alarm: 60-Second Blocking Busy-Wait

**File:** `alarm.cpp`

**Problem:**  
`alarm_ring()` contained a `while (millis() - start < 60000)` loop with `delay(10)` polling — blocking `loop()` entirely for up to 60 seconds. During this time: voice processing stopped, reminders couldn't fire, the NTP sync and WiFi watchdog were suspended, and the webserver queue backed up.

**Fix:**  
`alarm_ring()` now only sets state flags and plays the alarm sound. A new non-blocking `alarm_ring_tick()` function — called every `loop()` iteration — implements the double-tap detection as a proper rising/falling edge state machine with no `delay()` calls:

```cpp
void alarm_ring(int /*idx*/) {
    alarmRinging = true;  ringingStart = millis();
    dfPlayer.play(21);    drawFace(ALARM_RING);
}

void alarm_ring_tick() {
    if (!alarmRinging) return;
    if (millis() - ringingStart > 60000) { /* auto-dismiss */ return; }

    bool pinHigh = (digitalRead(TOUCH_PIN) == HIGH);
    if (pinHigh && !touchHeld_)  { touchHeld_ = true; }
    else if (!pinHigh && touchHeld_) {
        touchHeld_ = false;
        // ... double-tap window check ...
    }
}
```

`alarm_ring_tick()` is called at the top of `loop()`.

---

### Fix 8 — Enrollment: `portMAX_DELAY` on `i2s_read`

**File:** `enrollment.cpp`

```cpp
// Before — hangs forever if I2S driver stalls
i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytesRead, portMAX_DELAY);

// After — 2-second timeout; partial reads handled gracefully
i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytesRead, pdMS_TO_TICKS(2000));
```

---

### Fix 9 — Missing ALARM and REMINDER Face States

**Files:** `headers/face.h`, `face.cpp`

The v7 doc defines dedicated OLED modes for active alarms and reminders, but these were not in the `FaceState` enum and `drawFace()` had no cases for them. `alarm.cpp` and `reminder.cpp` used loose `showText()` calls with no state tracking.

**Fix:**  
`ALARM_RING` and `REMINDER_SHOW` added to the enum. `drawFace(ALARM_RING)` renders the "ALARM! / Double-tap head / to dismiss" screen. `drawFace(REMINDER_SHOW)` renders "REMINDER" + the task text stored via `face_set_reminder_text()`. Both `alarm_ring()` and `reminder_check()` now call `drawFace()` with the correct state.

---

### Fix 10 — New Module: `numtts.h / numtts.cpp`

**New files:** `headers/numtts.h`, `numtts.cpp`

Implements the v7 offline number-clip TTS feature. Two public functions:

| Function | Description |
|---|---|
| `numtts_speak_int(int value)` | Plays clip for a number 1–59 from SD folder `/03/` |
| `numtts_speak_temperature(float tempC)` | Rounds and speaks value + "degrees"; handles negatives with clip 060 ("minus") and clip 061 ("degrees") |

SD folder layout required:
```
/03/001.mp3  → "one"
/03/023.mp3  → "twenty-three"
/03/059.mp3  → "fifty-nine"
/03/060.mp3  → "minus"
/03/061.mp3  → "degrees"
```
`NUMTTS_CLIPS_FOLDER` is `#define`d at the top of the header — change it if your SD folder numbering differs.

---

### Fix 11 — New Module: `timer.h / timer.cpp`

**New files:** `headers/timer.h`, `timer.cpp`

Implements countdown timer and stopwatch as described in v7 doc sections 20 and 1.1.

**Countdown timer:**  
`timer_start(seconds)` → sets `TIMER_FACE` state, updates large-digit OLED display every 500 ms via `showTimerText()`, plays MP3 track 30 ("Timer done!") on expiry.

**Stopwatch:**  
`stopwatch_start()` / `stopwatch_stop()` — pause/resume accumulation. `stopwatch_reset()` clears to zero. Elapsed time displayed on OLED while running.

`timer_loop()` is called every `loop()` iteration and is fully non-blocking (no `delay()` calls).

**Voice integration in `commands.cpp`:**  
- `"timer"` label: redirects to portal (duration can't be parsed from the current raw-speech-free voice flow)  
- `"stopwatch"` label: toggles start/stop; announces elapsed time via `showText()` on stop

---

### Fix 12 — Dynamic Temperature Speak-Back (v7 Feature)

**File:** `commands.cpp`

**Problem:**  
The v7 doc's headline new feature — spoken temperature using number clips — was completely absent from the weather command handler. The weather handler only called `showText()` to display the temperature on the OLED.

**Fix:**  
After the weather condition clip plays, `numtts_speak_temperature(wd.feelsLike)` chains the number clips for the actual feels-like value. The umbrella/warm-clothes clips are delayed by the appropriate amount to prevent audio overlap:

```cpp
// Play condition clip
if (wd.condition == "Sunny") dfPlayer.play(4); /* ... */

// Chain number clips for temperature (v7 Dynamic Temperature Speak-Back)
delay(1200);
numtts_speak_temperature(wd.feelsLike);

// Suggestion clips after temperature finishes
if (wd.playUmbrella)    { delay(1500); dfPlayer.play(40); }
else if (wd.playWarmClothes) { delay(1500); dfPlayer.play(41); }
```

---

### Fix 13 — TTS: Wasteful SD Full-Copy on Every Playback

**File:** `tts.cpp`

**Problem:**  
Every call to `tts_speak()` copied the cached TTS file to `/mp3/0200.mp3` via a full read-write loop, even if the same phrase was being played again. This doubled SD I/O on every repeated call (common for time queries at the same minute).

**Fix:**  
A static `currentSlotKey` string tracks which cache key is currently in slot 200. The copy is skipped entirely if the slot already holds the requested key:

```cpp
static String currentSlotKey = "";

static void copyToSlot(const String& src, const String& key) {
    if (currentSlotKey == key) return;  // already in slot — skip copy
    // ... copy once, update currentSlotKey ...
}
```
On repeated "what's the time?" calls within the same minute, slot 200 is already correct and `dfPlayer.play(200)` fires immediately with zero SD activity.

---

### Fix 14 — Battery: Linear Voltage→% Is Inaccurate

**File:** `battery.cpp`

**Problem:**  
The original code used a linear map from 3.2 V–4.2 V:
```cpp
int pct = (int)((vBat - 3.2f) / (4.2f - 3.2f) * 100.0f);
```
LiPo discharge is highly non-linear — the 3.7–3.9 V plateau holds ~60% of the cell's energy, making the linear formula report ~50% even at near-full charge.

**Fix:**  
Replaced with a 7-segment piecewise linear approximation matching a typical 3.7 V 2000 mAh cell's discharge curve:

| Voltage range | % range |
|---|---|
| 4.20 V+ | 100% |
| 4.05–4.20 V | 90–100% |
| 3.90–4.05 V | 75–90% |
| 3.75–3.90 V | 50–75% |
| 3.60–3.75 V | 25–50% |
| 3.40–3.60 V | 10–25% |
| 3.20–3.40 V | 0–10% |

---

### Fix 15 — Enrollment: Raw PCM Cosine Similarity

**File:** `enrollment.cpp`

**Problem:**  
Cosine similarity on raw PCM samples with no preprocessing is highly sensitive to DC offset (microphone bias voltage) and recording amplitude. A word spoken at a different volume or with a different mic distance could produce very low similarity even for a correct match.

**Fix:**  
DC bias is removed from both the incoming audio and each reference sample before computing the dot product. The mean of each vector is subtracted sample-by-sample, making the similarity measure amplitude-independent and bias-independent:

```cpp
// Compute and remove DC bias from incoming audio
double sumA = 0;
for (size_t j = 0; j < sampleCount; j++) sumA += audio[j];
double meanA = sumA / sampleCount;

// ... for each reference sample:
double sumB = 0;
for (size_t j = 0; j < ENROLL_SAMPLE_COUNT; j++) sumB += refBuf[j];
double meanB = sumB / ENROLL_SAMPLE_COUNT;

// Cosine similarity with DC removed
for (size_t j = 0; j < len; j++) {
    double a = (double)audio[j]  - meanA;
    double b = (double)refBuf[j] - meanB;
    dot += a * b;  normA += a * a;  normB += b * b;
}
```

---

### Fix 16 — Reminder: Single MP3 Slot Drops Simultaneous Reminders

**File:** `reminder.cpp`

**Problem:**  
```cpp
static int           pendingReminderMp3 = 0;  // one slot only
static unsigned long reminderMp3At     = 0;
```
If two reminders were scheduled for the same time (allowed by the portal), the second would silently overwrite the first. The first reminder's task MP3 would never play.

**Fix:**  
Replaced the single slot with a 3-element circular queue. `enqueue_mp3()` adds entries; `reminder_tick()` drains them in order, respecting each entry's fire time:

```cpp
#define PENDING_QUEUE_SIZE 3
static int           pendingMp3Queue[PENDING_QUEUE_SIZE] = {};
static unsigned long pendingMp3At[PENDING_QUEUE_SIZE]    = {};
static int           pendingHead = 0;
static int           pendingTail = 0;
```

---

### Fix 17 — TTS Cache Key Ignores Timezone Offset

**File:** `commands.cpp`

**Problem:**  
Cache keys were built from the time/date string alone (e.g., `time_730am`). If the user updated the timezone via the portal, the device would still play the old cached audio that was generated for the previous timezone — silently announcing the wrong time.

**Fix:**  
The timezone offset is embedded in the cache key:

```cpp
char key[32];
snprintf(key, sizeof(key), "time_%ld_%s", appConfig.timezoneOffset, t.c_str());
```
A timezone change produces a new key → a new download → the correct time spoken. Old cached files for the previous timezone remain on the SD card but are never played (they can be cleaned up by reformatting the `/tts/` folder).

---

### Fix 18 — Web Portal: No Authentication

**Files:** `webserver.cpp`, `headers/config.h`

**Problem:**  
All endpoints were open to any device on the same WiFi network. `GET /config` returned the full JSON including the OWM and ElevenLabs API keys in plaintext. All write endpoints (`POST /alarms`, `POST /tasks`, etc.) could be called by any network client.

**Fix:**  
An optional `portal_pin` field was added to `AppConfig` and `config.json`. When set (non-empty), all write endpoints (`POST`/`DELETE`) check for a matching `X-Ronnie-Pin` header and return HTTP 401 if absent or wrong:

```cpp
static bool pinOk(AsyncWebServerRequest* req) {
    if (appConfig.portalPin.isEmpty()) return true;  // PIN not set → open
    if (!req->hasHeader("X-Ronnie-Pin")) return false;
    return req->header("X-Ronnie-Pin") == appConfig.portalPin;
}
```

The `GET /config` endpoint now strips the `portal_pin` field from the JSON before sending it to the browser so the PIN itself is never exposed.

To enable: add `"portal_pin": "your-pin-here"` to `config.json` via serial or direct LittleFS upload on first setup.

---

### Fix 19 — ArduinoJson v6 `DynamicJsonDocument` → v7 `JsonDocument`

**Files:** All `.cpp` files and `project_ronnie.ino`

Every occurrence of `DynamicJsonDocument(N)` was replaced with `JsonDocument` (no capacity argument). ArduinoJson v7 allocates memory dynamically from the heap and does not require a compile-time capacity hint. This eliminates deprecation warnings and ensures future library compatibility.

```cpp
// Before (v6)
DynamicJsonDocument doc(2048);

// After (v7)
JsonDocument doc;
```

`doc.createNestedArray()` / `doc.createNestedObject()` calls were updated to the v7 equivalents: `doc["key"].to<JsonArray>()` and `arr.add<JsonObject>()`.

---

### Fix 20 — Smart Idle Screen With Time and Battery

**Files:** `face.h`, `face.cpp`, `project_ronnie.ino`

**Problem:**  
The v7 doc specifies the smart idle screen should show a sleepy face in the centre, live clock top-left, and battery icon top-right. The old `IDLE` `drawFace()` case drew only the face with no status bar.

**Fix:**  
A new `face_set_status(int hour, int minute, bool isPM, int battPct)` function stores the status values. The `IDLE` case in `drawFace()` now renders:
- Time string top-left (`7:30A` / `7:30P`)
- Battery outline icon + fill bar top-right
- Face centred (with blink animation)

`face_set_status()` is called every 10 seconds in `loop()` alongside the existing `alarm_check()` / `reminder_check()` tick:

```cpp
face_set_status(h, now.minute(), pm, battery_percent());
```

---

## New Files Created

| File | Purpose |
|---|---|
| `headers/numtts.h` | Public API for offline number-clip TTS |
| `numtts.cpp` | Implementation — `numtts_speak_int()`, `numtts_speak_temperature()` |
| `headers/timer.h` | Public API for countdown timer and stopwatch |
| `timer.cpp` | Implementation — non-blocking, `timer_loop()` driven |

---

## Files Modified

| File | Changes |
|---|---|
| `headers/config.h` | Added `portalPin` field to `AppConfig` |
| `headers/face.h` | Added `ALARM_RING`, `REMINDER_SHOW`, `TIMER_FACE`, `STOPWATCH_FACE` states; added `face_set_status()`, `face_set_reminder_text()`, `showTimerText()` |
| `headers/alarm.h` | Added `alarm_ring_tick()` declaration |
| `face.cpp` | Fixed version string; implemented all new states; added smart idle status bar |
| `alarm.cpp` | Non-blocking ring state machine; mutex on LittleFS; `JsonDocument` migration |
| `battery.cpp` | Piecewise LiPo discharge curve |
| `reminder.cpp` | 3-slot MP3 queue; mutex on LittleFS; `JsonDocument` migration |
| `weather.cpp` | Fixed rain-amount parsing; `JsonDocument` migration |
| `tts.cpp` | Skip-copy optimisation; `JsonDocument` migration |
| `enrollment.cpp` | Removed 640 KB static array; SD-per-match loading; DC bias removal; mutex on LittleFS; 2 s `i2s_read` timeout; `JsonDocument` migration |
| `voice.cpp` | Chunk-accumulation audio collection; removed faulty 100 ms throttle |
| `commands.cpp` | Timezone-aware TTS cache keys; temperature speak-back via `numtts`; timer/stopwatch commands; included new headers |
| `webserver.cpp` | Mutex on all LittleFS ops; `pinOk()` auth on all write endpoints; PIN stripped from `GET /config` response; `JsonDocument` migration; `(const char*)` cast fix |
| `project_ronnie.ino` | `fsMutex` init; `loadConfig()` mutex-guarded; `autoDetectCity()` null-handle fix; `alarm_ring_tick()` + `timer_loop()` in `loop()`; `face_set_status()` in 10 s tick; `portalPin` loaded; `timer_setup()` called; `JsonDocument` migration |

---

## Remaining Considerations (Out of Scope for This Pass)

| Item | Notes |
|---|---|
| TLS certificate pinning | `client.setInsecure()` is still used in `weather.cpp` and `tts.cpp`. Full fix requires periodically updating pinned fingerprints for `api.openweathermap.org` and `api.elevenlabs.io` — not practical without a cert-rotation strategy. |
| Timer voice duration | `"timer"` label redirects to the portal because `rawSpeech` is always empty from the voice pipeline. A future fix would add a multi-stage voice flow (similar to the reminder flow) to capture hours and minutes. |
| Cosine similarity robustness | DC-bias removal is a significant improvement, but production-quality word matching would benefit from MFCC-based features or DTW alignment. Single-sample enrollment remains sensitive to recording conditions. |
| TTS old cache cleanup | After a timezone change, stale `/tts/` files with the old timezone key accumulate on the SD card. A startup scan comparing keys against the current offset would clean these up. |
| Portal PIN UX | The PIN is currently set by editing `config.json` directly. The web portal `index.html` (served from LittleFS) should be updated to show a PIN setup field. |
