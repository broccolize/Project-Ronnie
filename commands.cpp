#include "commands.h"
#include "face.h"
#include "weather.h"
#include "rtc_manager.h"
#include "alarm.h"
#include "reminder.h"
#include "battery.h"
#include "tts.h"        // V4-6: ElevenLabs TTS for time and date
#include <DFRobotDFPlayerMini.h>

// All externs are declared via commands.h — no duplicates needed here.

// ---------------------------------------------------------------------------
// Speech parsing helpers
// ---------------------------------------------------------------------------

static int parseHourFromSpeech(const String& speech) {
    int hour = -1;
    for (int i = 0; i < (int)speech.length(); i++) {
        if (isdigit(speech[i])) {
            hour = speech[i] - '0';
            if (i + 1 < (int)speech.length() && isdigit(speech[i + 1])) {
                hour = hour * 10 + (speech[i + 1] - '0');
            }
            break;
        }
    }
    return hour;
}

static int parseMinuteFromSpeech(const String& speech) {
    int colonIdx = speech.indexOf(':');
    if (colonIdx > 0 && colonIdx + 2 < (int)speech.length()) {
        int min = (speech[colonIdx + 1] - '0') * 10;
        if (isdigit(speech[colonIdx + 2])) {
            min += (speech[colonIdx + 2] - '0');
        }
        return min;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------

void handleCommand(const char* label, const String& rawSpeech) {
    Serial.printf("Command: %s\n", label);

    if (strcmp(label, "time") == 0) {
        // V4-6: Speak the actual current time via TTS (falls back to static MP3 if unavailable)
        String t = getFormattedTime();  // e.g. "7:30 AM"
        showText("The Time Is", t.c_str(), NULL);
        String key = "time_" + t;
        key.replace(":", ""); key.replace(" ", ""); key.toLowerCase();  // e.g. "time_730am"
        tts_speak("The time is " + t, key);

    } else if (strcmp(label, "date") == 0) {
        // V4-6: Speak the actual current date via TTS
        String d = getFormattedDate();  // e.g. "18 May 2026"
        showText("Today's Date", d.c_str(), NULL);
        String key = "date_" + d;
        key.replace(" ", ""); key.toLowerCase();  // e.g. "date_18may2026"
        tts_speak("Today is " + d, key);

    } else if (strcmp(label, "time_and_date") == 0) {
        // V4-6: Speak combined time and date via TTS
        String t = getFormattedTime();
        String d = getFormattedDate();
        showText(d.c_str(), t.c_str(), NULL);
        String key = "combined_" + d + t;
        key.replace(" ", ""); key.replace(":", ""); key.toLowerCase();
        tts_speak("Today is " + d + ", and the time is " + t, key);

    } else if (strcmp(label, "weather") == 0) {
        if (!wifiConnected) {
            dfPlayer.play(100);
            return;
        }
        drawFace(THINKING);
        WeatherData wd = fetchWeather();
        if (wd.error) {
            dfPlayer.play(98);
            return;
        }
        char line2[30];
        sprintf(line2, "Feels like %d C", (int)wd.feelsLike);
        showText(wd.condition.c_str(), line2, wd.suggestion.c_str());
        if      (wd.condition == "Sunny")  dfPlayer.play(4);
        else if (wd.condition == "Rainy")  dfPlayer.play(5);
        else if (wd.condition == "Cloudy") dfPlayer.play(6);
        else                               dfPlayer.play(8);
        drawFace(SPEAKING);
        // Umbrella takes priority; warm-clothes plays only if umbrella not needed
        if      (wd.playUmbrella)    dfPlayer.play(40);
        else if (wd.playWarmClothes) dfPlayer.play(41);

    } else if (strcmp(label, "set_alarm") == 0) {
        // voice_loop() always passes empty rawSpeech — time parsing cannot work.
        // Redirect to the web portal, same as the "reminder" command.
        // TODO: record a dedicated "use the portal to set alarms" MP3 (track 7 reused for now).
        dfPlayer.play(7);
        showText("Set alarm via", "Ronnie portal", NULL);
        drawFace(HAPPY);

    } else if (strcmp(label, "reminder") == 0) {
        // Reminder details are set via the web portal;
        // voice command just prompts the user to open it
        dfPlayer.play(7);
        showText("Add reminder", "via Ronnie", "web portal");
        drawFace(HAPPY);

    } else if (strcmp(label, "good_morning") == 0) {
        int h = getCurrentHour();
        if      (h < 8) dfPlayer.play(50);
        else if (h < 9) dfPlayer.play(51);
        else            dfPlayer.play(52);
        drawFace(HAPPY);
        // Schedule weather 3 s later so the greeting MP3 finishes first
        if (wifiConnected) {
            gmWeatherScheduledAt = millis() + 3000;
        }

    } else if (strcmp(label, "battery") == 0) {
        battery_respond_to_query();

    } else {
        dfPlayer.play(99);
        drawFace(IDLE);
    }
}
