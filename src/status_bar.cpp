#include "status_bar.h"
#include "weather.h"
#include "config_manager.h"
#include <ctime>

StatusBar statusBar;

void StatusBar::begin(TFT_eSPI *display) {
    _tft = display;
}

void StatusBar::draw(bool showClock, bool showWeather) {
    if (!_tft) return;

    // Draw bottom bar background
    _tft->fillRect(0, STATUS_BAR_Y, TFT_WIDTH, STATUS_BAR_HEIGHT, TFT_BLACK);

    // Text center y position within the bar (vertical center)
    int centerY = STATUS_BAR_Y + (STATUS_BAR_HEIGHT / 2);

    _tft->setTextFont(2);
    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    // --- LEFT SIDE: Weather (if enabled and valid) ---
    if (showWeather && weather.data.valid) {
        char tempStr[32];
        snprintf(tempStr, sizeof(tempStr), "t%.0f v%.0f ^%.0f",
                 weather.data.tempC, weather.data.tempMin, weather.data.tempMax);
        _tft->setTextDatum(ML_DATUM);
        // Draw twice (offset by 2px) for bold effect
        _tft->drawString(tempStr, 2, centerY);
        _tft->drawString(tempStr, 4, centerY);
    }

    // --- RIGHT SIDE: Date and Time (if clock enabled) ---
    if (showClock) {
        String rightText = "";
        time_t now = time(NULL);
        if (now != 0) {
            struct tm *tm_info = localtime(&now);
            char dateStr[12];
            char timeStr[9];
            strftime(dateStr, sizeof(dateStr), "%d-%m-%y", tm_info);
            strftime(timeStr, sizeof(timeStr), "%H:%M", tm_info);
            rightText = String(dateStr) + " " + String(timeStr);
        }
        if (rightText.length() > 0) {
            _tft->setTextDatum(MR_DATUM);
            // Draw twice (offset by 2px) for bold effect
            _tft->drawString(rightText, TFT_WIDTH - 2, centerY);
            _tft->drawString(rightText, TFT_WIDTH - 4, centerY);
        }
    }
}
