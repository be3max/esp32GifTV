#include "weather.h"
#include "config_manager.h"
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

void Weather::draw() {
    if (!_tft || !data.valid) return;

    _tft->fillScreen(TFT_BLACK);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);

    _tft->setTextSize(3);
    _tft->drawString(configMgr.getConfig().weather_city, 120, 60);

    _tft->setTextSize(4);
    char tempStr[12];
    snprintf(tempStr, sizeof(tempStr), "%.1f C", data.tempC);
    _tft->drawString(tempStr, 120, 120);

    _tft->setTextSize(2);
    _tft->drawString(data.description, 120, 170);

    char humStr[20];
    snprintf(humStr, sizeof(humStr), "Humidity: %.0f%%", data.humidity);
    _tft->drawString(humStr, 120, 200);
}
