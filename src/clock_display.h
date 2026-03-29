#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

class ClockDisplay {
public:
    void begin(TFT_eSPI *display, const String &ntpServer, int8_t tzHours);

    // Call in loop — updates NTP and redraws if the second has changed.
    void update();

    // Force a full redraw.
    void draw();

private:
    TFT_eSPI   *_tft = nullptr;
    WiFiUDP     _udp;
    NTPClient  *_ntp = nullptr;

    String _lastTimeStr;

    String formatTime();
    String formatDate();
};

extern ClockDisplay clockDisplay;
