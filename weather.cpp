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
    client.setInsecure();
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

    DynamicJsonDocument doc(1536);
    deserializeJson(doc, http.getStream());
    http.end();

    wd.feelsLike     = doc["main"]["feels_like"];
    String main      = doc["weather"][0]["main"].as<String>();
    float  rainAmount = 0;

    if (doc.containsKey("rain")) {
        rainAmount = doc["rain"]["1h"] | doc["rain"]["3h"] | 0.0f;
    }
    int cloudCover = doc["clouds"]["all"] | 0;

    if (main == "Rain" || main == "Drizzle" || main == "Thunderstorm") {
        wd.condition = "Rainy";
    } else if (main == "Clear") {
        wd.condition = "Sunny";
    } else if (main == "Clouds" && cloudCover > 60) {
        wd.condition = "Cloudy";
    } else {
        wd.condition = "Mild Cloudy";
    }

    wd.playUmbrella    = (rainAmount > 0.1 || main == "Rain" || main == "Drizzle");
    wd.playWarmClothes = (wd.feelsLike <= 16.0);

    if      (wd.playUmbrella)         wd.suggestion = "Bring an umbrella!";
    else if (wd.playWarmClothes)      wd.suggestion = "Dress warmly today";
    else if (wd.condition == "Sunny") wd.suggestion = "Stay hydrated!";
    else                              wd.suggestion = "Have a good day!";

    return wd;
}
