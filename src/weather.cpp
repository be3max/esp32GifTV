#include "weather.h"
#include "config_manager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

Weather weather;

// OpenWeatherMap free API endpoint
static const char *OWM_URL =
    "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=metric";

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
    if (deserializeJson(doc, body)) return false;

    data.description = doc["weather"][0]["description"] | "unknown";
    data.icon        = doc["weather"][0]["icon"]        | "";
    data.tempC       = doc["main"]["temp"]              | 0.0f;
    data.tempMin     = doc["main"]["temp_min"]          | 0.0f;
    data.tempMax     = doc["main"]["temp_max"]          | 0.0f;
    data.humidity    = doc["main"]["humidity"]          | 0.0f;
    data.valid       = true;

    Serial.printf("[WX] %s %.1f°C %s\n",
        city.c_str(), data.tempC, data.description.c_str());
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
