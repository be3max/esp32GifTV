#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Status bar dimensions — draws at bottom of screen
constexpr uint8_t STATUS_BAR_HEIGHT = 20;
constexpr int     STATUS_BAR_Y      = TFT_HEIGHT - STATUS_BAR_HEIGHT;  // 240 - 20 = 220

class StatusBar {
public:
    void begin(TFT_eSPI *display);
    void draw(bool showClock, bool showWeather);

private:
    TFT_eSPI *_tft = nullptr;
};

extern StatusBar statusBar;
