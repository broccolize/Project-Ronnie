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

void handleCommand(const char* label, const String& /*rawSpeech*/) {
    Serial.printf("Command: %s\n", label);

    if (strcmp(label, "time") == 0) {
        String t = getFormattedTime();
        showText("The Time Is", t.c_str(), NULL);
        // Include timezone offset in key so cached audio is never wrong after a
        // timezone change
        char key[32];
        snprintf(key, sizeof(key), "time_%ld_%s",
                 appConfig.timezoneOffset,
                 t.c_str());
        // Sanitise key: remove chars that are invalid in SD file names
        String k = String(key);
        k.replace(":", ""); k.replace(" ", ""); k.toLowerCase();
        tts_speak("The time is " + t, k);

    } else if (strcmp(label, "date") == 0) {
        String d = getFormattedDate();
        showText("Today's Date", d.c_str(), NULL);
        char key[32];
        snprintf(key, sizeof(key), "date_%ld_%s",
                 appConfig.timezoneOffset,
                 d.c_str());
        String k = String(key);
        k.replace(" ", ""); k.toLowerCase();
        tts_speak("Today is " + d, k);

    } else if (strcmp(label, "time_and_date") == 0) {
        String t = getFormattedTime();
        String d = getFormattedDate();
        showText(d.c_str(), t.c_str(), NULL);
        char key[48];
        snprintf(key, sizeof(key), "combined_%ld_%s%s",
                 appConfig.timezoneOffset,
                 d.c_str(), t.c_str());
        String k = String(key);
        k.replace(" ", ""); k.replace(":", ""); k.toLowerCase();
        tts_speak("Today is " + d + ", and the time is " + t, k);

    } else if (strcmp(label, "weather") == 0) {
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

    } else if (strcmp(label, "set_alarm") == 0) {
        dfPlayer.play(7);
        showText("Set alarm via", "Ronnie portal", NULL);
        drawFace(HAPPY);

    } else if (strcmp(label, "reminder") == 0) {
        dfPlayer.play(7);
        showText("Add reminder", "via Ronnie", "web portal");
        drawFace(HAPPY);

    } else if (strcmp(label, "good_morning") == 0) {
        int h = getCurrentHour();
        if      (h < 8) dfPlayer.play(50);
        else if (h < 9) dfPlayer.play(51);
        else            dfPlayer.play(52);
        drawFace(HAPPY);
        if (wifiConnected) gmWeatherScheduledAt = millis() + 3000;

    } else if (strcmp(label, "battery") == 0) {
        battery_respond_to_query();

    } else if (strcmp(label, "timer") == 0) {
        // Duration can't be parsed from voice alone — redirect to portal
        dfPlayer.play(7);
        showText("Set timer via", "Ronnie portal", NULL);
        drawFace(HAPPY);

    } else if (strcmp(label, "stopwatch") == 0) {
        if (stopwatch_running()) {
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

    } else {
        dfPlayer.play(99);
        drawFace(IDLE);
    }
}
