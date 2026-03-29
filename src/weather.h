#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

struct WeatherData {
    String   description;   // e.g. "light rain"
    float    tempC;
    float    tempMin;       // today's low (from main.temp_min)
    float    tempMax;       // today's high (from main.temp_max)
    float    humidity;
    String   icon;          // OWM icon code, e.g. "02d"
    bool     valid = false;
};

class Weather {
public:
    void begin(TFT_eSPI *display);

    // Fetch from OpenWeatherMap. Returns true on success.
    bool fetch(const String &apiKey, const String &city);

    // Draw current weather on the TFT.
    void draw();

    WeatherData data;

private:
    TFT_eSPI *_tft = nullptr;
};

extern Weather weather;
