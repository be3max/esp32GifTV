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

// MS-DOS panel-style weather screen: WEATHER.EXE dialog frame with city,
// temperature, min/max, description, humidity and a primitive-drawn icon.
// Panel chrome is drawn once; values repaint only their own field areas.
class Weather {
public:
    void begin(TFT_eSPI *display);

    // Fetch from OpenWeatherMap. Returns true on success.
    bool fetch(const String &apiKey, const String &city);

    // Draw panel chrome + current values (or "Fetching..." while !data.valid).
    // Call on mode entry / after menu close.
    void draw();

    // Repaint value fields only (panel must already be drawn).
    void drawValues();

    WeatherData data;

private:
    TFT_eSPI *_tft = nullptr;
    bool      _panelDrawn = false;

    // Panel geometry
    static constexpr int16_t PX = 10, PY = 20, PW = 220, PH = 190, PTITLE_H = 20;

    void drawPanel();
    void drawIcon(int16_t x, int16_t y);
};

extern Weather weather;
