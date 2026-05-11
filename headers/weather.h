#pragma once
#include <Arduino.h>
#include "config.h"   // AppConfig struct + extern appConfig

struct WeatherData {
  float  feelsLike     = 0.0f;
  String condition;              // "Sunny" | "Rainy" | "Cloudy" | "Mild Cloudy"
  String suggestion;             // short OLED line3 string
  bool   playUmbrella    = false;
  bool   playWarmClothes = false;
  bool   error           = false;
};

WeatherData fetchWeather();
