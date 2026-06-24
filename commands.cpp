#include "commands.h"
#include "config.h"
#include "face.h"
#include "weather.h"
#include "rtc_manager.h"
#include "alarm.h"
#include "reminder.h"
#include "battery.h"
#include "tts.h"
#include "numtts.h"
#include "timer.h"
#include <DFRobotDFPlayerMini.h>

extern AppConfig appConfig;

// ── Text parsers (V8) ─────────────────────────────────────────────────────────
// Whisper returns natural English ("seven forty five", "7:45", "five minutes").
// These helpers extract numbers, clock times, durations, and reminder text from
// that transcript so the single-utterance commands can be understood in one shot.

// Map a single word to 0–59 (ones, teens, and the tens 20/30/40/50). -1 if none.
static int oneWordNumber(const String& w) {
    static const char* ones[] = {
        "zero","one","two","three","four","five","six","seven","eight","nine",
        "ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen",
        "seventeen","eighteen","nineteen"
    };
    for (int i = 0; i < 20; i++) if (w == ones[i]) return i;
    if (w == "twenty") return 20;
    if (w == "thirty") return 30;
    if (w == "forty")  return 40;
    if (w == "fifty")  return 50;
    return -1;
}

// First run of decimal digits in s, as an int. -1 if none.
static int firstDigits(const String& s) {
    int i = 0, n = s.length();
    while (i < n && !isdigit(s[i])) i++;
    if (i >= n) return -1;
    int v = 0;
    while (i < n && isdigit(s[i])) { v = v * 10 + (s[i] - '0'); i++; }
    return v;
}

// Convert spelled-out number words in s to an int, combining "twenty five".
// Returns the first number found, or -1.
static int wordsToNumber(const String& s) {
    int start = 0, n = s.length();
    while (start < n) {
        while (start < n && s[start] == ' ') start++;
        int end = start;
        while (end < n && s[end] != ' ') end++;
        String tok = s.substring(start, end);
        int v = oneWordNumber(tok);
        if (v >= 0) {
            // tens followed by a ones word → combine (e.g. "twenty five" = 25)
            if (v % 10 == 0 && v >= 20) {
                int ns = end;
                while (ns < n && s[ns] == ' ') ns++;
                int ne = ns;
                while (ne < n && s[ne] != ' ') ne++;
                int v2 = oneWordNumber(s.substring(ns, ne));
                if (v2 >= 1 && v2 <= 9) return v + v2;
            }
            return v;
        }
        start = end;
    }
    return -1;
}

// First number in s, preferring digits, falling back to words.
static int parseNumber(const String& s) {
    int d = firstDigits(s);
    if (d >= 0) return d;
    return wordsToNumber(s);
}

struct ParsedTime { int hour; int minute; bool valid; };

// Parse a clock time from a lower-cased transcript. Handles "7:45", "7 pm",
// "seven", "seven forty five" (words), and applies AM/PM.
static ParsedTime parseClockTime(const String& s) {
    ParsedTime pt = { -1, 0, false };

    int colon = s.indexOf(':');
    if (colon > 0) {
        // Walk back over the hour digits, forward over the minute digits
        int hs = colon - 1;
        while (hs > 0 && isdigit(s[hs - 1])) hs--;
        pt.hour   = s.substring(hs, colon).toInt();
        int me = colon + 1;
        while (me < (int)s.length() && isdigit(s[me])) me++;
        pt.minute = s.substring(colon + 1, me).toInt();
    } else {
        int h = parseNumber(s);
        if (h < 0) return pt;
        pt.hour   = h;
        pt.minute = 0;
    }

    // AM/PM (accept "pm", "p m"). Apply 12-hour → 24-hour conversion.
    bool pm = (s.indexOf("pm") >= 0) || (s.indexOf("p m") >= 0);
    bool am = (s.indexOf("am") >= 0) || (s.indexOf("a m") >= 0);
    if (pm && pt.hour < 12) pt.hour += 12;
    if (am && pt.hour == 12) pt.hour = 0;

    pt.valid = (pt.hour >= 0 && pt.hour <= 23 && pt.minute >= 0 && pt.minute <= 59);
    return pt;
}

// Parse a timer duration into seconds. Defaults to minutes if no unit given.
static unsigned long parseTimerDuration(const String& s) {
    int num = parseNumber(s);
    if (num <= 0) return 0;
    if (s.indexOf("hour") >= 0)                              return (unsigned long)num * 3600UL;
    if (s.indexOf("second") >= 0 || s.indexOf("sec") >= 0)   return (unsigned long)num;
    return (unsigned long)num * 60UL;  // minutes (default)
}

// Extract the reminder text from the original-case transcript.
// "remind me at 7 pm to take my pills" → "take my pills".
static String extractReminderText(const String& orig) {
    String low = orig; low.toLowerCase();
    int i = low.indexOf(" to ");
    if (i >= 0) { String t = orig.substring(i + 4); t.trim(); return t; }
    i = low.indexOf(" about ");
    if (i >= 0) { String t = orig.substring(i + 7); t.trim(); return t; }
    String t = orig; t.trim();
    return t;
}

// ── Command dispatch (V8) ─────────────────────────────────────────────────────
// label is ignored — intent is derived from the full transcript.
void handleCommand(const char* /*unused label*/, const String& transcript) {
    String s = transcript;
    s.toLowerCase();
    Serial.printf("handleCommand: \"%s\"\n", s.c_str());

    if (s.indexOf("time") >= 0 && s.indexOf("timer") < 0) {
        String t = getFormattedTime();
        showText("The Time Is", t.c_str(), NULL);
        // Include timezone offset in key so cached audio is never wrong after a
        // timezone change
        char key[32];
        snprintf(key, sizeof(key), "time_%ld_%s", appConfig.timezoneOffset, t.c_str());
        String k = String(key);
        k.replace(":", ""); k.replace(" ", ""); k.toLowerCase();
        tts_speak("The time is " + t, k);

    } else if (s.indexOf("date") >= 0) {
        String d = getFormattedDate();
        showText("Today's Date", d.c_str(), NULL);
        char key[32];
        snprintf(key, sizeof(key), "date_%ld_%s", appConfig.timezoneOffset, d.c_str());
        String k = String(key);
        k.replace(" ", ""); k.toLowerCase();
        tts_speak("Today is " + d, k);

    } else if (s.indexOf("weather") >= 0) {
        if (!wifiConnected) { dfPlayer.play(100); return; }
        drawFace(THINKING);
        WeatherData wd = fetchWeather();
        if (wd.error) { dfPlayer.play(98); return; }

        char line2[30];
        sprintf(line2, "Feels like %d C", (int)wd.feelsLike);
        showText(wd.condition.c_str(), line2, wd.suggestion.c_str());

        // Play condition clip then chain offline number clips for temperature (v7)
        if      (wd.condition == "Sunny")  dfPlayer.play(4);
        else if (wd.condition == "Rainy")  dfPlayer.play(5);
        else if (wd.condition == "Cloudy") dfPlayer.play(6);
        else                               dfPlayer.play(8);
        drawFace(SPEAKING);

        // Speak the feels-like temperature via number clips (v7 feature)
        delay(1200);  // wait for condition clip to finish before chaining
        numtts_speak_temperature(wd.feelsLike);

        if      (wd.playUmbrella)    { delay(1500); dfPlayer.play(40); }
        else if (wd.playWarmClothes) { delay(1500); dfPlayer.play(41); }

    } else if (s.indexOf("alarm") >= 0) {
        ParsedTime pt = parseClockTime(s);
        if (!pt.valid) { dfPlayer.play(99); return; }
        alarm_set(pt.hour, pt.minute);
        char buf[20];
        snprintf(buf, sizeof(buf), "%02d:%02d", pt.hour, pt.minute);
        showText("Alarm set", buf, NULL);
        dfPlayer.play(20);  // "Got you!"
        drawFace(HAPPY);

    } else if (s.indexOf("timer") >= 0) {
        unsigned long secs = parseTimerDuration(s);
        if (secs == 0) { dfPlayer.play(99); return; }
        timer_start(secs);  // starts countdown immediately (no follow-up prompt)
        dfPlayer.play(20);

    } else if (s.indexOf("stopwatch") >= 0) {
        if (s.indexOf("stop") >= 0 && stopwatch_running()) {
            stopwatch_stop();
            unsigned long el = stopwatch_elapsed_seconds();
            char msg[20];
            snprintf(msg, sizeof(msg), "%02lu:%02lu", el / 60, el % 60);
            showText("Stopped at:", msg, NULL);
            dfPlayer.play(31);  // "Stopwatch stopped"
        } else {
            stopwatch_start();
            dfPlayer.play(32);  // "Stopwatch started"
        }

    } else if (s.indexOf("remind") >= 0) {
        ParsedTime pt = parseClockTime(s);
        if (!pt.valid) { dfPlayer.play(99); return; }
        String text = extractReminderText(transcript);
        Reminder r = {};
        r.hour   = pt.hour;
        r.minute = pt.minute;
        r.mp3    = 0;  // D4: speak r.text via TTS, no pre-recorded task MP3
        strlcpy(r.text, text.c_str(), sizeof(r.text));
        reminder_add_voice(r);
        showText("Reminder set", text.c_str(), NULL);
        dfPlayer.play(20);  // "Got you!"
        drawFace(HAPPY);

    } else if (s.indexOf("good morning") >= 0) {
        int h = getCurrentHour();
        if      (h < 8) dfPlayer.play(50);
        else if (h < 9) dfPlayer.play(51);
        else            dfPlayer.play(52);
        drawFace(HAPPY);
        if (wifiConnected) gmWeatherScheduledAt = millis() + 3000;

    } else if (s.indexOf("hungry") >= 0 || s.indexOf("battery") >= 0) {
        battery_respond_to_query();

    } else {
        dfPlayer.play(99);  // didn't catch that
        drawFace(IDLE);
    }
}
