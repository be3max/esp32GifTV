#pragma once
#include <Arduino.h>

constexpr char    CONFIG_PATH[] = "/config.json";
constexpr uint8_t MAX_GIF_URLS  = 10;

enum DisplayMode : uint8_t {
    DISPLAY_MODE_GIF_ONLY   = 0,
    DISPLAY_MODE_GIF_STATUS = 1,
    DISPLAY_MODE_CLOCK      = 2,
    DISPLAY_MODE_WEATHER    = 3,
};

struct DeviceConfig {
    char     wifi_ssid[64];
    char     wifi_pass[64];
    uint16_t gif_refresh_seconds;   // default: 30
    uint8_t  display_mode;          // default: DISPLAY_MODE_GIF_STATUS
    char     timezone[48];          // POSIX TZ string e.g. "EST5EDT,M3.2.0,M11.1.0"
    char     weather_api_key[64];   // OpenWeatherMap key
    char     weather_city[64];      // e.g. "Toronto"
    float    weather_lat;
    float    weather_lon;

    // GIF URL list (kept alongside core config)
    char     gif_urls[MAX_GIF_URLS][256];
    uint8_t  gif_url_count;

    // GIF URL list file mode
    char     gif_list_url[256];    // URL to a text file with GIF URLs, one per line
    bool     gif_use_list_url;     // true = use gif_list_url, false = use gif_urls[]

    // GIF cache settings
    bool     gif_cache_enabled;       // default: false
    uint16_t gif_cache_max_kb;        // default: 500 (KB); safe max ~700 for 960 KB partition
    uint16_t gif_cache_lifetime_min;  // default: 60 (minutes)

    // GIF visual effects
    bool     gif_dissolve_enabled;    // default: true
    bool     gif_crt_enabled;         // default: false
};

class ConfigManager {
public:
    bool begin();                          // mount LittleFS
    bool load();                           // parse /config.json → _cfg, create defaults if missing
    bool save();                           // serialise _cfg → /config.json
    DeviceConfig       &getConfig();       // mutable reference
    const DeviceConfig &getConfig() const; // const reference
    void reset();                          // factory-reset: defaults + save

private:
    DeviceConfig _cfg{};

    void applyDefaults();
};

extern ConfigManager configMgr;
