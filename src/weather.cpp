#include "weather.h"
#include "config_manager.h"
#include "ui_theme.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

Weather weather;

// OpenWeatherMap free API endpoint — 5-day forecast (3-hour intervals)
// We'll extract today's min/max from the forecast data
static const char *OWM_URL =
    "http://api.openweathermap.org/data/2.5/forecast?q=%s&appid=%s&units=metric";

void Weather::begin(TFT_eSPI *display) {
    _tft = display;
}

bool Weather::fetch(const String &apiKey, const String &city) {
    if (apiKey.isEmpty() || city.isEmpty()) return false;

    char url[256];
    snprintf(url, sizeof(url), OWM_URL, city.c_str(), apiKey.c_str());

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[WX] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        Serial.println("[WX] JSON parse failed");
        return false;
    }

    // Forecast API returns a list of 3-hourly forecasts
    // Extract current (first item), description, and min/max from today's forecasts
    JsonArray list = doc["list"];
    if (list.size() == 0) {
        Serial.println("[WX] Forecast list is empty");
        return false;
    }

    // Use first forecast item for current conditions
    float currTemp = list[0]["main"]["temp"] | 0.0f;
    data.description = list[0]["weather"][0]["description"] | "unknown";
    data.icon        = list[0]["weather"][0]["icon"]        | "";
    data.humidity    = list[0]["main"]["humidity"]          | 0.0f;

    // Calculate min/max from all forecast items for today (use all available forecasts)
    float minTemp = currTemp;
    float maxTemp = currTemp;
    for (JsonObject item : list) {
        float temp = item["main"]["temp"] | 0.0f;
        if (temp < minTemp) minTemp = temp;
        if (temp > maxTemp) maxTemp = temp;
    }

    data.tempC       = currTemp;
    data.tempMin     = minTemp;
    data.tempMax     = maxTemp;
    data.valid       = true;

    Serial.printf("[WX] %s curr=%.1f min=%.1f max=%.1f %s\n",
        city.c_str(), data.tempC, data.tempMin, data.tempMax, data.description.c_str());
    return true;
}

void Weather::drawPanel() {
    _tft->fillScreen(TFT_BLACK);
    uiDrawDosFrame(_tft, PX, PY, PW, PH, PTITLE_H, "[ WEATHER.EXE ]");
    _panelDrawn = true;
}

// Primitive-drawn mini weather icon (32x32 cell) from OWM icon code prefix.
void Weather::drawIcon(int16_t x, int16_t y) {
    _tft->fillRect(x, y, 32, 32, UI_NAVY);
    const char *ic = data.icon.c_str();
    const int16_t cx = x + 16, cy = y + 16;

    if (strncmp(ic, "01", 2) == 0) {
        // Sun: circle + 8 tick rays
        _tft->fillCircle(cx, cy, 7, TFT_YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = i * 0.7854f;  // 45 deg steps
            _tft->drawLine(cx + (int)(10 * cosf(a)), cy + (int)(10 * sinf(a)),
                           cx + (int)(14 * cosf(a)), cy + (int)(14 * sinf(a)), TFT_YELLOW);
        }
    } else if (strncmp(ic, "09", 2) == 0 || strncmp(ic, "10", 2) == 0 ||
               strncmp(ic, "11", 2) == 0) {
        // Rain/storm: cloud + slashes (lightning stroke for 11)
        _tft->fillCircle(cx - 6, cy - 6, 6, TFT_LIGHTGREY);
        _tft->fillCircle(cx + 4, cy - 8, 7, TFT_LIGHTGREY);
        _tft->fillRect(cx - 10, cy - 6, 22, 6, TFT_LIGHTGREY);
        if (ic[0] == '1' && ic[1] == '1') {
            _tft->drawLine(cx, cy + 2, cx - 4, cy + 9, TFT_YELLOW);
            _tft->drawLine(cx - 4, cy + 9, cx + 1, cy + 14, TFT_YELLOW);
        } else {
            for (int i = -1; i <= 1; i++)
                _tft->drawLine(cx + i * 8, cy + 4, cx + i * 8 - 3, cy + 12, TFT_CYAN);
        }
    } else if (strncmp(ic, "13", 2) == 0) {
        // Snow: asterisk
        _tft->drawLine(cx - 8, cy, cx + 8, cy, TFT_WHITE);
        _tft->drawLine(cx, cy - 8, cx, cy + 8, TFT_WHITE);
        _tft->drawLine(cx - 6, cy - 6, cx + 6, cy + 6, TFT_WHITE);
        _tft->drawLine(cx - 6, cy + 6, cx + 6, cy - 6, TFT_WHITE);
    } else if (strncmp(ic, "50", 2) == 0) {
        // Mist: horizontal lines
        for (int i = 0; i < 4; i++)
            _tft->drawFastHLine(x + 4, y + 8 + i * 6, 24, TFT_LIGHTGREY);
    } else if (ic[0] != '\0') {
        // Clouds (02/03/04)
        _tft->fillCircle(cx - 6, cy - 2, 6, TFT_LIGHTGREY);
        _tft->fillCircle(cx + 4, cy - 4, 7, TFT_LIGHTGREY);
        _tft->fillRect(cx - 10, cy - 2, 22, 6, TFT_LIGHTGREY);
    }
}

void Weather::drawValues() {
    if (!_tft || !_panelDrawn) return;

    const int16_t cx = PX + PW / 2;
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(1);

    if (!data.valid) {
        _tft->setTextFont(2);
        _tft->setTextColor(TFT_LIGHTGREY, UI_NAVY);
        _tft->fillRect(PX + 3, 92, PW - 6, 24, UI_NAVY);
        _tft->drawString("Fetching weather data...", cx, 104);
        return;
    }

    char buf[32];

    // City (field stops short of the icon cell on the right)
    _tft->setTextFont(4);
    _tft->fillRect(PX + 3, 48, PW - 46, 28, UI_NAVY);
    _tft->setTextColor(TFT_YELLOW, UI_NAVY);
    _tft->drawString(configMgr.getConfig().weather_city, cx - 18, 62);

    drawIcon(PX + PW - 40, 46);

    // Temperature (Font4 has no degree glyph — apostrophe reads as °)
    snprintf(buf, sizeof(buf), "%.1f'C", data.tempC);
    _tft->setTextFont(4);
    _tft->fillRect(PX + 3, 84, PW - 6, 30, UI_NAVY);
    _tft->setTextColor(TFT_WHITE, UI_NAVY);
    _tft->drawString(buf, cx, 99);

    // Min/max — same v/^ convention as the status bar
    _tft->setTextFont(2);
    _tft->fillRect(PX + 3, 120, PW - 6, 20, UI_NAVY);
    snprintf(buf, sizeof(buf), "v%.1f", data.tempMin);
    const int16_t minW = _tft->textWidth(buf);
    _tft->setTextColor(TFT_CYAN, UI_NAVY);
    _tft->drawString(buf, cx - minW / 2 - 8, 130);
    snprintf(buf, sizeof(buf), "^%.1f", data.tempMax);
    _tft->setTextColor(UI_AMBER, UI_NAVY);
    _tft->drawString(buf, cx + minW / 2 + 8, 130);

    // Description
    _tft->fillRect(PX + 3, 146, PW - 6, 20, UI_NAVY);
    _tft->setTextColor(TFT_CYAN, UI_NAVY);
    _tft->drawString(data.description, cx, 156);

    // Humidity
    snprintf(buf, sizeof(buf), "Humidity: %.0f%%", data.humidity);
    _tft->fillRect(PX + 3, 172, PW - 6, 20, UI_NAVY);
    _tft->setTextColor(TFT_LIGHTGREY, UI_NAVY);
    _tft->drawString(buf, cx, 182);
}

void Weather::draw() {
    if (!_tft) return;
    drawPanel();
    drawValues();
}
