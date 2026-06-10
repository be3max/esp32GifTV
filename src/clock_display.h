#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// MS-DOS terminal style clock: "C:\> TIME" prompt, 7-segment amber digits
// (Font7), date line, blinking block cursor. Redraws only changed characters
// each second — no full-screen clears after the initial paint.
class ClockDisplay {
public:
    void begin(TFT_eSPI *display, const String &ntpServer, int8_t tzHours);

    // Call in loop — updates NTP and redraws changed digits on second change.
    void update();

    // Full repaint (chrome + time + date). Call after the popup menu closes
    // or on mode entry — incremental updates resume afterwards.
    void forceRedraw();

private:
    TFT_eSPI   *_tft = nullptr;
    WiFiUDP     _udp;
    NTPClient  *_ntp = nullptr;

    String   _lastTimeStr;
    String   _lastDateStr;
    int16_t  _charX[8]   = {0};  // x position of each HH:MM:SS character
    bool     _cursorOn   = false;

    static constexpr int16_t TIME_Y   = 64;   // top of Font7 digits
    static constexpr int16_t DATE_Y   = 166;  // top of Font4 date
    static constexpr int16_t PROMPT_Y = 10;
    static constexpr int16_t CURSOR_Y = 210;

    void drawStatic();                 // prompt + labels, drawn once
    void drawTime(const String &t);    // per-character diff redraw
    String formatTime();
    String formatDate();
};

extern ClockDisplay clockDisplay;
