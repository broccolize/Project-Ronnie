# Project Ronnie — v8.0 Architecture Restructure Guide

> **Purpose of this document.** This is an implementation guide for Claude Code. It describes a complete restructuring of the Ronnie firmware's voice pipeline. Follow it file-by-file. Where it says **DELETE**, **REWRITE**, or **MODIFY**, treat that as the scope boundary — do not refactor files marked "leave untouched."

---

## 1. What is changing and why

### Old architecture (v7.0)
Edge Impulse ran an on-device keyword-spotting model with **~35 labels**: the wake word, ~9 command words, ~16 number words, units, and AM/PM. On top of that, a separate **cosine-similarity enrollment system** matched user-defined task words for reminders. All recognition was on the ESP32.

**Problem:** ~35 labels at 50+ samples each (100+ for noise) is 2,000+ recordings the user cannot realistically collect. Accuracy suffers from too many classes, and the multi-stage voice flows require several classification round-trips per interaction.

### New architecture (v8.0)
Split recognition into two stages:

1. **Wake word — stays on the ESP32, Edge Impulse.** The model is reduced to **two classes only**: `hey_ronnie` and `noise`. One detector, one decision boundary → higher accuracy, ~50 real positive samples needed instead of thousands.
2. **Everything after the wake word — moves to a local Whisper server on the user's laptop.** After the wake word fires, the ESP32 records the spoken command, uploads the audio over the LAN to a self-hosted Whisper server, and gets back **plain text**. Intent is parsed from that text and routed to the existing command handlers.

### Consequences of the change
- **DELETE** the entire enrollment system and all number/command ML labels.
- The number clips (`clips/` folder) are **kept**, but now used **only for offline speech output** (speaking the time/date/temperature), never for recognition.
- The 3-stage voice flows (reminder: hour → AM/PM → task; timer: number → unit) **collapse into single-utterance parsing**. "Set a timer for five minutes" and "remind me at 7 PM to take my pills" are each understood in one shot from the transcript.
- Voice commands now **require the laptop server to be reachable over WiFi**. Offline = no voice commands (see §9 for fallback behavior).

---

## 2. Open decisions (defaults chosen — override before implementing if you disagree)

These were decided to keep momentum. Each is a real fork; change any of them and tell Claude Code.

| # | Decision | Default chosen | Alternative |
|---|----------|----------------|-------------|
| D1 | Wake acknowledgment | **Short chime (~0.2s)** so the user can speak immediately | Keep the spoken "Yes, sorry I kinda passed out!" line (adds ~1.5s before every command) |
| D2 | Backup trigger | **Keep the touch button as a second way to trigger capture** (press = skip wake word, go straight to record). Bulletproof if the wake word struggles on the plain ESP32. | Wake-word only |
| D3 | Web portal reminders | **Keep** portal reminders (they write `reminders.json`, never depended on enrollment). Only the **task-word enrollment UI** is removed. | Remove all portal reminder UI too |
| D4 | Reminder text source | **The transcript itself becomes the reminder text** (e.g. "take my pills"). No pre-recorded task MP3 needed. | — |
| D5 | Offline behavior | On any voice command with no server reachable, play the "out of network" clip (`0100.mp3`) and return to idle | Attempt a tiny on-device fallback (not recommended — adds back the data problem) |
| D6 | `CLIP_GAP` | **Reduce from 820ms to 300ms** in `numtts.cpp` for natural-sounding offline speech | Keep at 820ms |
| D7 | Whisper model | **`small.en`** via faster-whisper, int8, kept warm in RAM | `base.en` (faster, slightly less accurate) |

---

## 3. Target system architecture

```
                         ON THE ESP32 (WROOM-32)
  ┌──────────────────────────────────────────────────────────────┐
  │  INMP441 mic ──► Edge Impulse (2-class: hey_ronnie / noise)   │
  │                         │ wake fires (2+ consecutive windows) │
  │                         ▼                                      │
  │            chime + LISTENING face                             │
  │                         │                                      │
  │            stt_capture(): VAD-gated I2S record ──► /sd/cmd.wav │
  │                         │                                      │
  │            HTTP multipart POST  ──────────────┐                │
  └───────────────────────────────────────────────┼──────────────┘
                                                   │ LAN (WiFi)
                                                   ▼
                         ON THE LAPTOP (GTX 1650, 16GB)
  ┌──────────────────────────────────────────────────────────────┐
  │  FastAPI server  ──►  faster-whisper small.en (warm in RAM)   │
  │  POST /transcribe (WAV) ──► returns { "text": "..." }         │
  └───────────────────────────────────────────────┬──────────────┘
                                                   │ JSON text
                                                   ▼
                         BACK ON THE ESP32
  ┌──────────────────────────────────────────────────────────────┐
  │  parse intent from transcript ──► handleCommand(label, text)  │
  │  existing handlers: time / date / weather / alarm / timer /   │
  │  stopwatch / reminder / good_morning / battery                │
  │  output: ElevenLabs TTS (online) or number clips (offline)    │
  └──────────────────────────────────────────────────────────────┘
```

**Latency budget (target, model kept warm):** ~0.5–0.8s VAD trailing silence + ~0.3s upload + ~0.3–0.5s transcription ≈ **1–1.6s** from end-of-speech to start-of-response. A LAN round-trip to the local GPU is faster and more consistent than cloud Whisper.

---

## 4. Hardware / runtime notes (important, do not skip)

- **CPU frequency.** `main.ino` currently calls `setCpuFrequencyMhz(80)` for battery life. Always-on wake-word inference and the capture/upload path need more headroom. **Raise to 240MHz during listening and capture/upload, drop back to 80MHz when idle.** Implement a small helper (`cpu_fast()` / `cpu_idle()`) and call it around the active path.
- **Plain WROOM-32, not S3.** Continuous Edge Impulse inference is feasible but tight. If wake-word latency is poor, the touch-button trigger (D2) is the fallback.
- **Wake confirmation.** Fire only after **2+ consecutive positive windows** above threshold, not a single 0.80 hit. This is the single biggest false-accept reduction. (See §6.)
- **Laptop server.** GTX 1650 = 4GB VRAM. `small.en` int8 fits comfortably on GPU. The model **must be loaded once at startup and kept warm** — loading per request adds 1–2s every time.

---

## 5. PART A — Laptop Whisper server (new component)

Create a new folder **outside the Arduino project**, e.g. `ronnie-whisper-server/`.

### 5.1 Dependencies
```bash
pip install fastapi uvicorn python-multipart faster-whisper
```
faster-whisper pulls in CTranslate2. For GPU, ensure the machine has a working CUDA runtime for the installed CTranslate2 build. If GPU init fails, the server should fall back to CPU automatically (still fine for short clips, just slower).

### 5.2 Reference implementation — `server.py`
```python
from fastapi import FastAPI, UploadFile, File
from fastapi.responses import JSONResponse
from faster_whisper import WhisperModel
import tempfile, os

# Load ONCE at startup, keep warm. Try GPU (int8_float16), fall back to CPU (int8).
try:
    model = WhisperModel("small.en", device="cuda", compute_type="int8_float16")
    print("Whisper loaded on GPU")
except Exception as e:
    print(f"GPU load failed ({e}); falling back to CPU")
    model = WhisperModel("small.en", device="cpu", compute_type="int8")

app = FastAPI()

@app.get("/health")
def health():
    return {"status": "ok"}

@app.post("/transcribe")
async def transcribe(file: UploadFile = File(...)):
    # Save the uploaded WAV to a temp file
    with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as tmp:
        tmp.write(await file.read())
        path = tmp.name
    try:
        # beam_size=1 is fastest; command phrases are short and clear.
        segments, info = model.transcribe(path, beam_size=1, language="en")
        text = " ".join(seg.text for seg in segments).strip()
    finally:
        os.remove(path)
    return JSONResponse({"text": text})
```

### 5.3 Run it
```bash
uvicorn server:app --host 0.0.0.0 --port 8000
```
- `--host 0.0.0.0` is required so the ESP32 on the LAN can reach it (not just localhost).
- Note the laptop's **LAN IP** (e.g. `192.168.1.50`). This goes into Ronnie's config as `whisper_url`.
- **Open port 8000** in the laptop firewall for inbound LAN connections (Windows Defender will prompt; allow on private networks).
- Endpoint the ESP32 will call: `http://<LAN_IP>:8000/transcribe`

### 5.4 Warm-up test
```bash
curl http://<LAN_IP>:8000/health         # -> {"status":"ok"}
curl -F "file=@test.wav" http://<LAN_IP>:8000/transcribe   # -> {"text":"..."}
```

---

## 6. PART B — Edge Impulse wake word (retrain, two classes only)

**Do not change the I2S capture or the inference invocation in firmware** — only the model and the post-processing change.

### 6.1 Labels
| Label | Samples | Notes |
|-------|---------|-------|
| `hey_ronnie` | 50+ | Real recordings of the **user's own voice**, varied distance (30cm–1m), tone, and speed. Single-user device, so speaker-dependent is fine. |
| `noise` | 200+ | The effort goes HERE. Mix: room ambience, general speech/conversation, TV, keyboard, AND **confuser phrases** ("hey buddy", "hey Bonnie", "Ronnie" alone, "hey"). Pull in public background/speech datasets to bulk this up — negatives don't need careful recording. |

This is the inverse of the old burden: with one positive class, **false triggers are the main failure mode, and they come from weak negatives.** Underfunding the noise/confuser class is the #1 cause of a wake word that fires on everything.

### 6.2 Impulse settings
Keep as v7: 1000ms window, 500ms stride, MFCC block, small NN. Export Arduino library, EON compiler on. The exported `#include` name stays `project_ronnie_inferencing.h` so firmware includes don't change.

### 6.3 Firmware post-processing (in `voice.cpp`, see §7)
- Run the classifier each cycle as before.
- **Require N≥2 consecutive windows** where `hey_ronnie` is the top class above threshold before declaring a wake. Track a small counter; reset it on any non-wake window.
- Threshold stays ~0.80 (tune up if false accepts persist).

---

## 7. PART C — Firmware changes, file by file

### 7.1 Files to DELETE
- `enrollment.h`
- `enrollment.cpp`
- Remove all references to `enrollment_*` from every file that included them (`main.ino`, `voice.cpp`, `webserver.cpp`).

### 7.2 New file — `stt.h` / `stt.cpp`
Owns capture + upload + transcript retrieval. See §8 for the full spec.

**`stt.h`**
```cpp
#pragma once
#include <Arduino.h>
void   stt_setup();
// Captures a spoken command (VAD-gated), uploads to the local Whisper
// server, and returns the transcript. Returns "" on any failure.
String stt_capture_and_transcribe();
```

### 7.3 REWRITE — `voice.h` / `voice.cpp`
**Strip everything except wake detection.** Remove the entire `VoiceState` machine (`VS_REMINDER_*`, `VS_TIMER_*`), `labelToHour`, the reminder/timer flow blocks, and the enrollment-match branch.

New `voice_loop()` shape:
```
voice_loop():
  run I2S read + classifier   (unchanged capture)
  find top label + score
  if top == "hey_ronnie" and score > 0.80:
      wakeStreak++
  else:
      wakeStreak = 0
  if wakeStreak >= 2:
      wakeStreak = 0
      cpu_fast()
      face: LISTENING                  // new lightweight state, or reuse THINKING
      dfPlayer.play(<CHIME>)           // D1: short chime, NOT 0068 spoken line
      String text = stt_capture_and_transcribe()
      cpu_idle()
      if text == "":
          dfPlayer.play(98)            // "couldn't get that" (or 0100 if offline)
          drawSmartIdle(...)
      else:
          handleCommand("", text)      // label now derived from text inside handleCommand
```
`voice.h` reduces to `voice_setup()` and `voice_loop()` plus the I2S externs. Delete `extern VoiceState voiceState;` everywhere.

**Touch-button trigger (D2):** in `main.ino` loop (or here), if the touch sensor reads a deliberate press while idle, call the same wake body (chime → `stt_capture_and_transcribe()` → `handleCommand`). Keep the existing double-tap-to-dismiss logic for alarms separate.

### 7.4 REWRITE — `commands.h` / `commands.cpp`
`handleCommand()` now receives the **full transcript** and derives intent from it.

**New signature:**
```cpp
void handleCommand(const char* /*unused label*/, const String& transcript);
```
Keep the parameter shape so existing call sites compile; ignore the first arg (or change all call sites to a single-arg version — your call).

**Intent routing (replace the `strcmp(label,...)` chain with text matching):**
```
String s = transcript; s.toLowerCase();

if      s contains "time"   and not "timer"      -> speakTime()
else if s contains "date"                        -> speakDate()
else if s contains "weather"                     -> weather path (unchanged)
else if s contains "alarm"                       -> parse hour/min/ampm from s -> alarm_set()
else if s contains "timer"                       -> parseTimerDuration(s) -> timer_start() DIRECTLY
else if s contains "stopwatch":
        if s contains "stop"  -> stopwatch_stop()
        else                  -> stopwatch_start()
else if s contains "remind":
        parse hour/min/ampm from s
        reminder text = remainder of s after stripping the time phrase
        build Reminder{ hour, minute, text=<that remainder>, mp3=0 } -> reminder_add_voice()
        (mp3=0 means "speak the text via TTS/clips", no pre-recorded file)
else if s contains "good morning"                -> good morning path (unchanged)
else if s contains "hungry" or "battery"         -> battery_respond_to_query()
else                                             -> dfPlayer.play(99)  // didn't catch that
```

**Reuse existing parsers:** `parseHourFromSpeech()`, `parseMinuteFromSpeech()`, `parseTimerDuration()`, `wordToNumber()` already operate on text and now finally receive real sentences. Keep them. Extend `parseTimerDuration`/hour parsing if Whisper returns digits ("7:45") vs words ("seven forty five") — handle both (it already scans for digit chars first, then word numbers).

**Multi-stage flows are GONE.** No more `VS_TIMER_WAIT_NUMBER` prompts. A timer is set from one utterance. If the utterance is missing info (e.g. "set a timer" with no duration), play `0099` and return — do not enter a waiting state.

**`reminder_add_voice` / Reminder struct:** the `text` field already exists and is spoken back. Ensure the reminder trigger path speaks `r.text` via TTS or number clips rather than relying on a per-reminder MP3 file. If `mp3 == 0`, route to TTS of `r.text`; this removes dependence on pre-recorded reminder MP3s (0010–0019 become optional).

### 7.5 MODIFY — `main.ino`
- Remove `#include "enrollment.h"`, `enrollment_setup()`, and the `enrollRequested` / `pendingEnrollLabel` block in `loop()`.
- Add `#include "stt.h"` and `stt_setup()` in `setup()`.
- Add CPU helpers: `cpu_fast()` = `setCpuFrequencyMhz(240)`, `cpu_idle()` = `setCpuFrequencyMhz(80)`. Keep `setCpuFrequencyMhz(80)` as the idle default; the wake path raises/lowers around capture.
- Config: load a new field `whisper_url` from `config.json` into `AppConfig` (e.g. `String whisperUrl;`). Default to empty; if empty, voice commands are disabled and Ronnie plays the offline clip.

### 7.6 MODIFY — `numtts.cpp`
- Change `#define CLIP_GAP 820` → `300` (D6). This is the biggest perceived-speed win; the old value made a 4-clip answer take ~5s.
- No functional change otherwise. This file is now **output-only** (speaking), never recognition.

### 7.7 MODIFY — `webserver.cpp` / `index.html`
- Remove the `/enroll` endpoint and the `/tasks` enrollment-related handling that wrote `.raw` samples. Removing task-word enrollment.
- Keep `/reminders`, `/alarms`, `/config` (D3). Reminders via portal still work.
- Add `whisper_url` to the config form in `index.html` and to the `/config` POST/GET round-trip.
- Remove the "Voice Task Words" section and its JS (`addTask`, `enroll`, `deleteTask`, `loadTasks`) from `index.html`.

### 7.8 MODIFY — `face.cpp` / `face.h`
- D1: the wake acknowledgment is a **chime**, not the spoken line. Add a brief `LISTENING` visual (or reuse `THINKING`) shown during capture, and `SPEAKING`/info screens during the response as before.
- Optionally add a `UPLOADING`/`THINKING` face while waiting on the server.

### 7.9 LEAVE UNTOUCHED
`alarm.*`, `reminder.*` (internals — only the text-speaking path noted in 7.4), `battery.*`, `timer.*`, `weather.*`, `rtc_manager.*`, `tts.*` (ElevenLabs output stays), all OLED face rendering primitives. Do not refactor these beyond the specific hooks named above.

---

## 8. PART D — `stt.cpp` capture + upload spec

### 8.1 Flow
```
stt_capture_and_transcribe():
  if appConfig.whisperUrl is empty OR WiFi not connected:
      return ""                     // caller plays offline clip

  // 1. VAD-gated record from I2S into a buffer, write WAV to SD
  open /sd/cmd.wav for write
  write 44-byte WAV header (16kHz, 16-bit, mono) -- patch sizes after
  loop reading I2S frames:
      compute frame energy (sum of abs samples / count)
      if not yet started and energy > START_THRESHOLD: started = true
      if started: append frame samples to file
      if started and energy < SILENCE_THRESHOLD: silenceMs += frameMs
      else: silenceMs = 0
      stop when silenceMs > 700ms (end of speech) OR total > 8000ms (cap)
  if nothing captured (never started within ~3s): close, return ""
  patch WAV header with final data length, close file

  // 2. Upload
  text = post_wav_to_server(/sd/cmd.wav, appConfig.whisperUrl)
  return text   // "" on HTTP/parse failure
```

### 8.2 Audio format (must match server expectations)
- **16 kHz, 16-bit signed PCM, mono.** This matches the existing I2S config (`sample_rate=16000`, `I2S_BITS_PER_SAMPLE_16BIT`, `ONLY_LEFT`). faster-whisper resamples internally but 16k mono is ideal and smallest.
- WAV header is the standard 44-byte RIFF/WAVE/fmt /data layout. Fill `sampleRate=16000`, `bitsPerSample=16`, `numChannels=1`, `byteRate=32000`, `blockAlign=2`. Write a placeholder for `ChunkSize` and `Subchunk2Size`, then seek back and patch after recording (or compute from sample count if you buffer fully).
- ~6s of speech ≈ 192 KB — trivial to store on SD and upload.

### 8.3 Multipart upload (`post_wav_to_server`)
The server expects `multipart/form-data` with a field named **`file`**, filename `cmd.wav`, content-type `audio/wav`.

Implementation notes for ESP32:
- Use `HTTPClient` over plain `WiFiClient` (the local server is `http://`, not TLS — no `WiFiClientSecure` needed, which also saves heap).
- Build the multipart body by streaming: write the boundary + part headers, then the file bytes from SD in 512-byte chunks, then the closing boundary. Set `Content-Type: multipart/form-data; boundary=----RonnieBoundary` and a correct `Content-Length` (header bytes + file size + trailer bytes — compute before sending).
- This mirrors the existing `tts.cpp` HTTP-to-SD streaming pattern, run in reverse (SD-to-HTTP).
- On `code == 200`, parse JSON `{ "text": "..." }` with ArduinoJson, return the `text` field. On any non-200, timeout, or parse error, return `""`.
- Timeout: ~8000ms (server transcription of a short warm clip is sub-second, but allow margin).

### 8.4 Tunable constants (put at top of `stt.cpp`)
```
START_THRESHOLD     // energy to begin capture (calibrate to room)
SILENCE_THRESHOLD   // energy below which counts as silence
SILENCE_HANG_MS     720   // trailing silence that ends capture
MAX_CAPTURE_MS      8000  // hard cap
NO_SPEECH_TIMEOUT   3000  // if no speech starts, abort -> ""
```

---

## 9. PART E — Offline / failure handling

| Condition | Behavior |
|-----------|----------|
| WiFi up, server reachable, transcript OK | Normal: route to `handleCommand` |
| WiFi up, server unreachable / timeout | `stt_capture_and_transcribe()` returns ""; play `0100.mp3` ("out of network") and return to idle |
| WiFi down entirely | Same: play `0100.mp3` |
| Server returns empty text (no speech / garbled) | Play `0099.mp3` ("didn't catch that"); return to idle |
| Transcript present but matches no intent | `0099.mp3` |

**Note on time/date offline:** because recognition now lives on the server, even "what's the time" cannot be *understood* offline. The clock is still visible on the smart idle OLED (RTC + clips), and the device still keeps time — it just can't take a spoken command. This is the accepted trade-off of moving recognition online (decision D5). If the user later wants offline time/date by voice, that would mean re-adding a tiny on-device model — out of scope here.

---

## 10. PART F — config.json changes

Add `whisper_url`; the `tasks[]` array is no longer used for enrollment (leave the key for portal-defined reminders if desired, or remove).
```json
{
  "city": "London",
  "country": "UK",
  "timezone_offset": 0,
  "owm_key": "YOUR_OPENWEATHERMAP_API_KEY_HERE",
  "tts_key": "YOUR_ELEVENLABS_API_KEY_HERE",
  "whisper_url": "http://192.168.1.50:8000/transcribe",
  "volume": 25
}
```
- `whisper_url`: the laptop server endpoint. Empty string disables voice commands.
- `tts_key`: still optional; controls online vs offline **output** only (unchanged).

---

## 11. PART G — Updated feature test checklist (replaces enrollment tests)

| # | Test | Expected |
|---|------|----------|
| 1 | Server health | `curl /health` returns ok; `/transcribe` returns text for a sample WAV |
| 2 | Wake word fires | "Hey Ronnie" → chime within ~1s; LISTENING face |
| 3 | Wake word rejects confusers | "Hey buddy", "Ronnie", "hey" alone → no trigger |
| 4 | Capture ends on silence | Speak a command, stop → capture ends ~0.7s after you stop |
| 5 | Time | "what's the time" → correct time spoken |
| 6 | Date | "what's the date" → correct date spoken |
| 7 | Weather | "how's the weather" → condition + temp + suggestion |
| 8 | Timer (one-shot) | "set a timer for five minutes" → countdown starts immediately, no follow-up prompt |
| 9 | Alarm | "set an alarm for 7 PM" → alarm saved, confirm |
| 10 | Stopwatch | "start/stop the stopwatch" → starts/stops, speaks elapsed |
| 11 | Reminder (one-shot) | "remind me at 7 PM to take my pills" → reminder saved with text "take my pills" |
| 12 | Good morning | "good morning" → greeting + weather after 3s |
| 13 | Battery | "are you hungry" → battery % + personality response |
| 14 | Touch-button trigger (D2) | Press button → capture starts without wake word |
| 15 | Server down | Stop server → command → plays `0100.mp3`, returns to idle |
| 16 | Unknown command | "tell me a joke" → `0099.mp3` |
| 17 | Latency | End of speech → start of response ≈ 1–1.6s (model warm) |
| 18 | Speech output speed | 4-clip offline answer speaks in ~2.5s (CLIP_GAP=300) |

---

## 12. Implementation order (suggested for Claude Code)

1. **Laptop server first** (§5) — stand it up, verify with `curl`. Nothing on the ESP32 works until this responds.
2. **`stt.cpp`** (§8) — the new capture+upload module; test it by triggering from a stubbed wake.
3. **Retrain Edge Impulse** wake word (§6) and update `voice.cpp` post-processing (§7.3).
4. **Rewrite `commands.cpp`** intent routing (§7.4).
5. **Delete enrollment** (§7.1) and clean `main.ino` / `webserver.cpp` / `index.html` (§7.5, §7.7).
6. **Tune** `CLIP_GAP`, thresholds, chime (§7.6, §8.4, D1).
7. **Run the §11 checklist.**

---

## 13. Things NOT to change (guardrails)

- Do not re-add any multi-class command/number ML model. Edge Impulse holds the wake word and nothing else.
- Do not use the number clips for recognition — output only.
- Do not refactor `alarm`, `reminder` (beyond the text-speak hook), `battery`, `timer`, `weather`, `rtc_manager`, `tts`, or the OLED rendering primitives.
- Do not use `WiFiClientSecure` for the local server — it's plain HTTP on the LAN; keep TLS only for ElevenLabs/OpenWeatherMap.
- Keep all file/folder names and the `project_ronnie_inferencing.h` include name stable so unrelated includes don't break.

---

*End of guide. Open questions for the user are listed in §2 as decisions D1–D7; override any of them before implementation.*
