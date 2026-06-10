#include "clock_display.h"
#include "config_manager.h"
#include "ui_theme.h"

ClockDisplay clockDisplay;

void ClockDisplay::begin(TFT_eSPI *display, const String &ntpServer, int8_t tzHours) {
    _tft = display;
    long offsetSec = (long)tzHours * 3600;
    if (_ntp) { delete _ntp; }
    _ntp = new NTPClient(_udp, ntpServer.c_str(), offsetSec, 60000);
    _ntp->begin();
    _ntp->update();
    forceRedraw();
}

void ClockDisplay::drawStatic() {
    _tft->setTextFont(2);
    _tft->setTextSize(1);
    _tft->setTextDatum(TL_DATUM);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("C:\\> TIME", 3, PROMPT_Y);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("Current date is", 3, DATE_Y - 22);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("C:\\>", 3, CURSOR_Y);

    // Precompute Font7 character x positions for HH:MM:SS, centred
    const int16_t digitW = _tft->textWidth("8", 7);
    const int16_t colonW = _tft->textWidth(":", 7);
    const int16_t totalW = 6 * digitW + 2 * colonW;
    int16_t x = (TFT_WIDTH - totalW) / 2;
    if (x < 0) x = 0;
    const char *pattern = "88:88:88";
    for (uint8_t i = 0; i < 8; i++) {
        _charX[i] = x;
        x += (pattern[i] == ':') ? colonW : digitW;
    }
}

void ClockDisplay::forceRedraw() {
    if (!_tft) return;
    _tft->fillScreen(TFT_BLACK);
    drawStatic();
    _lastTimeStr = "";
    _lastDateStr = "";
    _cursorOn    = false;
    update();
}

void ClockDisplay::update() {
    if (!_tft || !_ntp) return;
    _ntp->update();

    String t = formatTime();
    if (t == _lastTimeStr) return;  // same second — nothing to do

    drawTime(t);
    _lastTimeStr = t;

    String d = formatDate();
    if (d != _lastDateStr) {
        _lastDateStr = d;
        _tft->setTextFont(4);
        _tft->setTextSize(1);
        _tft->setTextDatum(TL_DATUM);
        _tft->setTextColor(TFT_WHITE, TFT_BLACK);
        // Pad with trailing spaces so a shorter string erases the old one
        _tft->drawString(d + "  ", 3, DATE_Y);
    }

    // Blinking block cursor after the bottom prompt — toggles each second
    _cursorOn = !_cursorOn;
    _tft->setTextFont(2);
    const int16_t cx = 3 + _tft->textWidth("C:\\> ");
    _tft->fillRect(cx, CURSOR_Y, 9, 15, _cursorOn ? TFT_GREEN : TFT_BLACK);
}

void ClockDisplay::drawTime(const String &t) {
    if (t.length() != 8) return;
    const bool evenSec = ((_ntp->getSeconds()) & 1) == 0;

    _tft->setTextFont(7);
    _tft->setTextSize(1);
    _tft->setTextDatum(TL_DATUM);

    const bool first = (_lastTimeStr.length() != 8);
    for (uint8_t i = 0; i < 8; i++) {
        const char c = t[i];
        if (c == ':') {
            // Colons always repaint — they blink with the seconds
            _tft->setTextColor(evenSec ? UI_AMBER : UI_AMBER_DIM, TFT_BLACK);
            _tft->drawString(":", _charX[i], TIME_Y);
            continue;
        }
        if (!first && c == _lastTimeStr[i]) continue;  // unchanged digit
        _tft->setTextColor(UI_AMBER, TFT_BLACK);
        char buf[2] = { c, '\0' };
        _tft->drawString(buf, _charX[i], TIME_Y);
    }
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
