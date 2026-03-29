#include "config_manager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

ConfigManager configMgr;

// ── Defaults ─────────────────────────────────────────────────────────────────
void ConfigManager::applyDefaults() {
    memset(&_cfg, 0, sizeof(_cfg));
    _cfg.gif_refresh_seconds = 30;
    _cfg.display_mode        = DISPLAY_MODE_GIF_STATUS;
    strlcpy(_cfg.timezone,     "UTC0",    sizeof(_cfg.timezone));
    strlcpy(_cfg.weather_city, "Toronto", sizeof(_cfg.weather_city));
    _cfg.weather_lat     = 0.0f;
    _cfg.weather_lon     = 0.0f;
    _cfg.gif_url_count   = 0;
    _cfg.gif_list_url[0] = '\0';
    _cfg.gif_use_list_url = false;
    _cfg.gif_cache_enabled      = false;
    _cfg.gif_cache_max_kb       = 500;
    _cfg.gif_cache_lifetime_min = 60;
    _cfg.gif_dissolve_enabled   = true;
    _cfg.gif_crt_enabled        = false;
}

// ── Public API ───────────────────────────────────────────────────────────────
bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {   // true = format on first failure
        Serial.println("[CFG] LittleFS mount failed");
        return false;
    }
    return load();
}

bool ConfigManager::load() {
    applyDefaults();

    if (!LittleFS.exists(CONFIG_PATH)) {
        Serial.println("[CFG] No config file — writing defaults");
        return save();
    }

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        Serial.println("[CFG] Failed to open config file");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[CFG] JSON parse error: %s — resetting\n", err.c_str());
        return save();   // overwrite corrupt file with defaults
    }

    // Scalar fields — defaults already applied, so | operator provides fallback
    strlcpy(_cfg.wifi_ssid,       doc["wifi_ssid"]       | "", sizeof(_cfg.wifi_ssid));
    strlcpy(_cfg.wifi_pass,       doc["wifi_pass"]       | "", sizeof(_cfg.wifi_pass));
    _cfg.gif_refresh_seconds    = doc["gif_refresh_seconds"] | 30;

    // Migration from old show_clock/show_weather booleans to new display_mode
    if (doc["display_mode"].is<int>()) {
        _cfg.display_mode = doc["display_mode"].as<uint8_t>();
    } else {
        bool old_clock   = doc["show_clock"]   | false;
        bool old_weather = doc["show_weather"] | false;
        if      (old_clock)   _cfg.display_mode = DISPLAY_MODE_CLOCK;
        else if (old_weather) _cfg.display_mode = DISPLAY_MODE_WEATHER;
        else                  _cfg.display_mode = DISPLAY_MODE_GIF_STATUS;
    }
    strlcpy(_cfg.timezone,        doc["timezone"]          | "UTC0",    sizeof(_cfg.timezone));
    strlcpy(_cfg.weather_api_key, doc["weather_api_key"]   | "",        sizeof(_cfg.weather_api_key));
    strlcpy(_cfg.weather_city,    doc["weather_city"]      | "Toronto", sizeof(_cfg.weather_city));
    _cfg.weather_lat            = doc["weather_lat"]       | 0.0f;
    _cfg.weather_lon            = doc["weather_lon"]       | 0.0f;

    // GIF URL array
    _cfg.gif_url_count = 0;
    JsonArray urls = doc["gif_urls"].as<JsonArray>();
    for (JsonVariant v : urls) {
        if (_cfg.gif_url_count >= MAX_GIF_URLS) break;
        strlcpy(_cfg.gif_urls[_cfg.gif_url_count],
                v.as<const char *>() ? v.as<const char *>() : "",
                sizeof(_cfg.gif_urls[0]));
        _cfg.gif_url_count++;
    }

    // GIF URL list file mode
    strlcpy(_cfg.gif_list_url,
            doc["gif_list_url"] | "",
            sizeof(_cfg.gif_list_url));
    _cfg.gif_use_list_url = doc["gif_use_list_url"] | false;

    // GIF cache settings
    _cfg.gif_cache_enabled      = doc["gif_cache_enabled"]      | false;
    _cfg.gif_cache_max_kb       = doc["gif_cache_max_kb"]       | 500;
    _cfg.gif_cache_lifetime_min = doc["gif_cache_lifetime_min"] | 60;

    // GIF visual effects
    _cfg.gif_dissolve_enabled   = doc["gif_dissolve_enabled"] | true;
    _cfg.gif_crt_enabled        = doc["gif_crt_enabled"]      | false;

    Serial.println("[CFG] Config loaded");
    return true;
}

bool ConfigManager::save() {
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        Serial.println("[CFG] Failed to open config for writing");
        return false;
    }

    JsonDocument doc;
    doc["wifi_ssid"]            = _cfg.wifi_ssid;
    doc["wifi_pass"]            = _cfg.wifi_pass;
    doc["gif_refresh_seconds"]  = _cfg.gif_refresh_seconds;
    doc["display_mode"]         = _cfg.display_mode;
    doc["timezone"]             = _cfg.timezone;
    doc["weather_api_key"]      = _cfg.weather_api_key;
    doc["weather_city"]         = _cfg.weather_city;
    doc["weather_lat"]          = _cfg.weather_lat;
    doc["weather_lon"]          = _cfg.weather_lon;

    JsonArray urls = doc["gif_urls"].to<JsonArray>();
    for (uint8_t i = 0; i < _cfg.gif_url_count; i++) {
        urls.add(_cfg.gif_urls[i]);
    }

    // GIF URL list file mode
    doc["gif_list_url"]     = _cfg.gif_list_url;
    doc["gif_use_list_url"] = _cfg.gif_use_list_url;

    // GIF cache settings
    doc["gif_cache_enabled"]      = _cfg.gif_cache_enabled;
    doc["gif_cache_max_kb"]       = _cfg.gif_cache_max_kb;
    doc["gif_cache_lifetime_min"] = _cfg.gif_cache_lifetime_min;

    // GIF visual effects
    doc["gif_dissolve_enabled"]   = _cfg.gif_dissolve_enabled;
    doc["gif_crt_enabled"]        = _cfg.gif_crt_enabled;

    serializeJson(doc, f);
    f.close();
    Serial.println("[CFG] Config saved");
    return true;
}

DeviceConfig &ConfigManager::getConfig() {
    return _cfg;
}

const DeviceConfig &ConfigManager::getConfig() const {
    return _cfg;
}

void ConfigManager::reset() {
    applyDefaults();
    save();
    Serial.println("[CFG] Factory reset");
}
