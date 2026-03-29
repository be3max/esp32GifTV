#include "web_portal.h"
#include "config_manager.h"
#include "gif_player.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

WebPortal webPortal;

void WebPortal::begin() {
    setupRoutes();
    _server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void WebPortal::stop() {
    _server.end();
}

void WebPortal::update() {
    // No polling needed for AsyncWebServer.
}

void WebPortal::setupRoutes() {
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *req) {
        handleGetConfig(req);
    });

    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [this](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            handlePostConfig(req, data, len);
        }
    );

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
        handleGetStatus(req);
    });

    _server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true}");
        pendingRestart = true;
    });

    _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
        configMgr.reset();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not found");
    });
}

void WebPortal::handleGetConfig(AsyncWebServerRequest *req) {
    const DeviceConfig &c = configMgr.getConfig();
    JsonDocument doc;

    doc["wifi_ssid"]            = c.wifi_ssid;
    doc["gif_refresh_seconds"]  = c.gif_refresh_seconds;
    doc["display_mode"]         = c.display_mode;
    doc["timezone"]             = c.timezone;
    doc["weather_api_key"]      = c.weather_api_key;
    doc["weather_city"]         = c.weather_city;
    doc["weather_lat"]          = c.weather_lat;
    doc["weather_lon"]          = c.weather_lon;

    JsonArray urls = doc["gif_urls"].to<JsonArray>();
    for (uint8_t i = 0; i < c.gif_url_count; i++) {
        urls.add(c.gif_urls[i]);
    }

    doc["gif_list_url"]     = c.gif_list_url;
    doc["gif_use_list_url"] = c.gif_use_list_url;

    doc["gif_cache_enabled"]      = c.gif_cache_enabled;
    doc["gif_cache_max_kb"]       = c.gif_cache_max_kb;
    doc["gif_cache_lifetime_min"] = c.gif_cache_lifetime_min;

    doc["gif_dissolve_enabled"]   = c.gif_dissolve_enabled;
    doc["gif_crt_enabled"]        = c.gif_crt_enabled;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void WebPortal::handlePostConfig(AsyncWebServerRequest *req, uint8_t *data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        req->send(400, "application/json", "{\"error\":\"invalid JSON\"}");
        return;
    }

    DeviceConfig &c = configMgr.getConfig();

    if (doc["wifi_ssid"].is<const char *>())       strlcpy(c.wifi_ssid,       doc["wifi_ssid"],       sizeof(c.wifi_ssid));
    if (doc["wifi_pass"].is<const char *>())       strlcpy(c.wifi_pass,       doc["wifi_pass"],       sizeof(c.wifi_pass));
    if (doc["gif_refresh_seconds"].is<int>())      c.gif_refresh_seconds    = doc["gif_refresh_seconds"];
    if (doc["display_mode"].is<int>())             c.display_mode           = (uint8_t)doc["display_mode"].as<int>();
    if (doc["timezone"].is<const char *>())        strlcpy(c.timezone,        doc["timezone"],        sizeof(c.timezone));
    if (doc["weather_api_key"].is<const char *>()) strlcpy(c.weather_api_key, doc["weather_api_key"], sizeof(c.weather_api_key));
    if (doc["weather_city"].is<const char *>())    strlcpy(c.weather_city,    doc["weather_city"],    sizeof(c.weather_city));
    if (doc["weather_lat"].is<float>())            c.weather_lat            = doc["weather_lat"];
    if (doc["weather_lon"].is<float>())            c.weather_lon            = doc["weather_lon"];

    if (doc["gif_urls"].is<JsonArray>()) {
        c.gif_url_count = 0;
        for (JsonVariant v : doc["gif_urls"].as<JsonArray>()) {
            if (c.gif_url_count >= MAX_GIF_URLS) break;
            strlcpy(c.gif_urls[c.gif_url_count],
                    v.as<const char *>() ? v.as<const char *>() : "",
                    sizeof(c.gif_urls[0]));
            c.gif_url_count++;
        }
    }

    if (doc["gif_list_url"].is<const char *>())
        strlcpy(c.gif_list_url, doc["gif_list_url"], sizeof(c.gif_list_url));
    if (doc["gif_use_list_url"].is<bool>())
        c.gif_use_list_url = doc["gif_use_list_url"];

    if (doc["gif_cache_enabled"].is<bool>())
        c.gif_cache_enabled = doc["gif_cache_enabled"];
    if (doc["gif_cache_max_kb"].is<int>())
        c.gif_cache_max_kb = doc["gif_cache_max_kb"];
    if (doc["gif_cache_lifetime_min"].is<int>())
        c.gif_cache_lifetime_min = doc["gif_cache_lifetime_min"];

    if (doc["gif_dissolve_enabled"].is<bool>())
        c.gif_dissolve_enabled = doc["gif_dissolve_enabled"];
    if (doc["gif_crt_enabled"].is<bool>())
        c.gif_crt_enabled = doc["gif_crt_enabled"];

    configMgr.save();
    req->send(200, "application/json", "{\"ok\":true}");
}

void WebPortal::handleGetStatus(AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["freeHeap"]    = ESP.getFreeHeap();
    doc["uptime"]      = millis() / 1000;
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["cache_count"] = gifPlayer.getCacheCount();
    doc["cache_bytes"] = gifPlayer.getCacheBytes();

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}
