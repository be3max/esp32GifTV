#include "status_bar.h"
#include "weather.h"
#include "config_manager.h"
#include <ctime>

// MS-DOS CGA-style palette
static constexpr uint16_t COL_DARK_YELLOW = 0x7BE0;  // CGA dark yellow  (TFT_OLIVE)
static constexpr uint16_t COL_DARK_BLUE   = 0x000F;  // CGA dark blue    (TFT_NAVY)
static constexpr uint16_t COL_DARK_RED    = 0x7800;  // CGA dark red     (TFT_MAROON)
static constexpr uint16_t COL_DARK_GRAY   = 0x39C7;  // CGA dark grey

StatusBar statusBar;

void StatusBar::begin(TFT_eSPI *display) {
    _tft = display;
}

void StatusBar::draw(bool showClock, bool showWeather) {
    if (!_tft) return;

    _tft->fillRect(0, STATUS_BAR_Y, TFT_WIDTH, STATUS_BAR_HEIGHT, TFT_BLACK);

    const int centerY = STATUS_BAR_Y + (STATUS_BAR_HEIGHT / 2);
    constexpr int LINE_H = 3;
    constexpr int LINE_Y = STATUS_BAR_Y + STATUS_BAR_HEIGHT - LINE_H;

    _tft->setTextFont(2);
    _tft->setTextSize(1);
    _tft->setTextDatum(ML_DATUM);

    // ── LEFT: Weather badges with bottom underlines ──────────────────────────
    if (showWeather && weather.data.valid) {
        int x = 0;

        // t` (current temp) — white text, yellow underline
        char tStr[10];
        snprintf(tStr, sizeof(tStr), " t`%.0f ", weather.data.tempC);
        _tft->setTextColor(TFT_WHITE, TFT_BLACK);
        _tft->drawString(tStr, x, centerY);
        int w = _tft->textWidth(tStr);
        _tft->fillRect(x, LINE_Y, w, LINE_H, COL_DARK_YELLOW);
        x += w;

        // v (min temp) — white text, blue underline
        char vStr[10];
        snprintf(vStr, sizeof(vStr), " v%.0f ", weather.data.tempMin);
        _tft->setTextColor(TFT_WHITE, TFT_BLACK);
        _tft->drawString(vStr, x, centerY);
        w = _tft->textWidth(vStr);
        _tft->fillRect(x, LINE_Y, w, LINE_H, COL_DARK_BLUE);
        x += w;

        // ^ (max temp) — white text, red underline
        char mStr[10];
        snprintf(mStr, sizeof(mStr), " ^%.0f ", weather.data.tempMax);
        _tft->setTextColor(TFT_WHITE, TFT_BLACK);
        _tft->drawString(mStr, x, centerY);
        w = _tft->textWidth(mStr);
        _tft->fillRect(x, LINE_Y, w, LINE_H, COL_DARK_RED);
    }

    // ── RIGHT: Date + Time with underlines ───────────────────────────────────
    if (showClock) {
        time_t now = time(NULL);
        if (now != 0) {
            struct tm *tm_info = localtime(&now);
            char dateStr[16];
            char timeStr[12];
            strftime(dateStr, sizeof(dateStr), " %d/%m/%y ", tm_info);
            strftime(timeStr, sizeof(timeStr), " %H:%M ", tm_info);

            const int dateW = _tft->textWidth(dateStr);
            const int timeW = _tft->textWidth(timeStr);

            // Date — light grey text, grey underline
            _tft->setTextDatum(ML_DATUM);
            _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            _tft->drawString(dateStr, TFT_WIDTH - dateW - timeW, centerY);
            _tft->fillRect(TFT_WIDTH - dateW - timeW, LINE_Y, dateW, LINE_H, COL_DARK_GRAY);

            // Time — white text, no underline
            _tft->setTextColor(TFT_WHITE, TFT_BLACK);
            _tft->drawString(timeStr, TFT_WIDTH - timeW, centerY);
        }
    }
}
