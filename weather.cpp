#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "weather.h"

extern AppConfig appConfig;

WeatherData fetchWeather() {
    WeatherData wd;
    if (appConfig.owmKey.isEmpty()) {
        wd.error = true;
        return wd;
    }

    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin OWM cert fingerprint for production
    HTTPClient http;

    String url = "https://api.openweathermap.org/data/2.5/weather?q="
                 + appConfig.city
                 + "&appid=" + appConfig.owmKey
                 + "&units=metric";

    http.begin(client, url);
    http.setTimeout(5000);
    http.setConnectTimeout(5000);

    int code = http.GET();
    if (code != 200) {
        http.end();
        wd.error = true;
        return wd;
    }

    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    http.end();

    wd.feelsLike  = doc["main"]["feels_like"] | 0.0f;
    String main   = doc["weather"][0]["main"].as<String>();
    int cloudCover = doc["clouds"]["all"] | 0;

    float rainAmount = 0.0f;
    if (doc["rain"].is<JsonObject>()) {
        // "1h" takes priority; fall back to "3h" if only the 3-hour bucket is present
        if (!doc["rain"]["1h"].isNull())      rainAmount = doc["rain"]["1h"].as<float>();
        else if (!doc["rain"]["3h"].isNull()) rainAmount = doc["rain"]["3h"].as<float>();
    }

    if (main == "Rain" || main == "Drizzle" || main == "Thunderstorm") {
        wd.condition = "Rainy";
    } else if (main == "Clear") {
        wd.condition = "Sunny";
    } else if (main == "Clouds" && cloudCover > 60) {
        wd.condition = "Cloudy";
    } else {
        wd.condition = "Mild Cloudy";
    }

    wd.playUmbrella    = (rainAmount > 0.1f || main == "Rain" || main == "Drizzle");
    wd.playWarmClothes = (wd.feelsLike <= 16.0f);

    if      (wd.playUmbrella)         wd.suggestion = "Bring an umbrella!";
    else if (wd.playWarmClothes)      wd.suggestion = "Dress warmly today";
    else if (wd.condition == "Sunny") wd.suggestion = "Stay hydrated!";
    else                              wd.suggestion = "Have a good day!";

    return wd;
}
