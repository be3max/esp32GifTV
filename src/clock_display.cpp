#include "clock_display.h"
#include "config_manager.h"

ClockDisplay clockDisplay;

void ClockDisplay::begin(TFT_eSPI *display, const String &ntpServer, int8_t tzHours) {
    _tft = display;
    long offsetSec = (long)tzHours * 3600;
    _ntp = new NTPClient(_udp, ntpServer.c_str(), offsetSec, 60000);
    _ntp->begin();
    _ntp->update();
    _tft->fillScreen(TFT_BLACK);
}

void ClockDisplay::update() {
    _ntp->update();
    String t = formatTime();
    if (t != _lastTimeStr) {
        _lastTimeStr = t;
        draw();
    }
}

void ClockDisplay::draw() {
    if (!_tft || !_ntp) return;

    _tft->fillScreen(TFT_BLACK);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_CYAN, TFT_BLACK);

    // Large time
    _tft->setTextSize(4);
    _tft->drawString(formatTime(), 120, 100);

    // Smaller date
    _tft->setTextSize(2);
    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString(formatDate(), 120, 160);
}

String ClockDisplay::formatTime() {
    if (!_ntp) return "--:--:--";
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
        _ntp->getHours(), _ntp->getMinutes(), _ntp->getSeconds());
    return String(buf);
}

String ClockDisplay::formatDate() {
    // NTPClient doesn't provide date — derive from epoch
    time_t epoch = _ntp->getEpochTime();
    struct tm *t = localtime(&epoch);
    char buf[20];
    // ISO format: YYYY-MM-DD
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return String(buf);
}
