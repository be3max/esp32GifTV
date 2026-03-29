# esp32GifScreenSaver

A custom firmware for the **NMMiner NM-TV-154** (ESP32-based 1.54" TFT miner display) that turns the device into a configurable screensaver with GIF playback, clock, and weather modes — configurable entirely via a browser-based web portal.

---

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-D0WD-V3, 240 MHz, 4 MB flash, no PSRAM |
| Display | ST7789 1.54" 240×240 TFT over HSPI |
| USB-Serial | CH340 on COM7 |
| Touch pad | GPIO32 (T9 capacitive) |

### Pin mapping

| Function | GPIO | Notes |
|----------|------|-------|
| TFT_MOSI | 13 | HSPI data |
| TFT_SCLK | 14 | HSPI clock |
| TFT_CS | 15 | Chip select |
| TFT_DC | 2 | Data/Command |
| TFT_RST | — | Not connected |
| TFT_BL | 19 | **Active LOW** |
| TFT_PWR | 21 | **Active LOW** — must pull LOW before `tft.begin()` |

---

## Features

### Display modes
- **GIF** — Streams animated GIFs from the internet (or a user-supplied URL list), renders with smooth scaling to fill the screen. Optional dissolve transition and CRT scanline effect.
- **GIF + Status bar** — Same as GIF, with a 20px bottom bar showing current time, date, and weather.
- **Clock** — Full-screen digital clock synced via NTP.
- **Weather** — Full-screen current weather from OpenWeatherMap.

### GIF playback
- Downloads GIFs via HTTP(S) and streams directly to LittleFS (`/tmp.gif`) to avoid large heap allocations
- Optional local cache: stores up to N KB of GIFs on the LittleFS partition, with configurable lifetime
- Configurable refresh interval (default: 30 s)
- Supports a remote URL list file (one URL per line) with reservoir sampling
- Proportional scaling with destination-driven row filling to eliminate horizontal gaps
- Dissolve transition: 30×30 grid of 8px blocks revealed in random order using hardware RNG

### Web portal
- Runs on port 80; scan for `GifScreen-Setup` AP on first boot, then open `http://<device-ip>/`
- Configure all settings without reflashing: WiFi, display mode, GIF URLs, timezone, weather API key, cache settings, visual effects
- REST endpoints: `/api/config` (GET/POST), `/api/status`, `/api/restart`, `/api/reset`

### Boot screen
- Retro Award BIOS / MS-DOS aesthetic on the TFT during startup
- Scrolling POST-style lines with colored status tags: `[ OK ]` (green), `[FAIL]` (red), `[WAIT]` (yellow → flips to OK after each blocking call)
- MS-DOS dialog box for the "Connected!" and "Cache cleared" screens

---

## Build & Flash

Requires [PlatformIO](https://platformio.org/). The full CLI path on Windows is `C:\Users\user\.platformio\penv\Scripts\pio.exe`.

```bash
# Build and flash firmware
pio run -t upload

# Flash web portal files (data/ folder → LittleFS)
pio run -t uploadfs

# Clean build artifacts (required after changing build_flags)
pio run -t clean

# Serial monitor
pio device monitor -b 115200
```

> **Note:** Flash `uploadfs` first when both firmware and filesystem change together.
> Always run `clean` before `upload` after editing `build_flags` in `platformio.ini`.

---

## Architecture

```
main.cpp          — setup() boot sequence, loop() state machine, touch button FSM
config_manager    — JSON config persistence on LittleFS (/config.json)
gif_player        — HTTP download, LittleFS streaming, AnimatedGIF decode, dissolve fx
boot_screen       — Retro BIOS/DOS boot UI renderer (zero heap allocation)
web_portal        — ESPAsyncWebServer, REST API, serves data/index.html
clock_display     — NTPClient-backed full-screen digital clock
weather           — OpenWeatherMap fetch + TFT render
status_bar        — 20px bottom bar with time and weather summary
```

### Memory strategy
RAM is tight (~200 KB usable, no PSRAM):
1. Bluetooth controller memory released first (`esp_bt_controller_mem_release`) — recovers ~80 KB contiguous
2. GIF frame buffer (66 KB) allocated immediately after, before WiFi fragments the heap
3. GIF downloads capped at 100 KB (`MAX_GIF_BYTES`)
4. LittleFS cache holds GIFs across reboots to avoid re-downloading

### Flash layout (`partitions.csv`)

| Partition | Size |
|-----------|------|
| OTA slot 0 | 1.5 MB |
| OTA slot 1 | 1.5 MB |
| LittleFS | 960 KB |

Firmware currently occupies ~77% of the 1.5 MB app slot.

---

## Configuration

All settings are stored in `/config.json` on LittleFS and editable via the web portal.

| Setting | Default | Description |
|---------|---------|-------------|
| `display_mode` | GIF + status | 0=GIF only, 1=GIF+bar, 2=Clock, 3=Weather |
| `gif_refresh_seconds` | 30 | How often to fetch a new GIF |
| `gif_urls[]` | — | Up to 10 direct GIF URLs |
| `gif_list_url` | — | URL to a plain-text file of GIF URLs (one per line) |
| `gif_cache_enabled` | false | Cache downloaded GIFs to LittleFS |
| `gif_cache_max_kb` | 500 | Max cache size in KB |
| `gif_cache_lifetime_min` | 60 | Cache expiry in minutes |
| `gif_dissolve_enabled` | true | Dissolve block transition between GIFs |
| `gif_crt_enabled` | false | CRT scanline overlay (darkens odd rows) |
| `timezone` | UTC0 | POSIX TZ string e.g. `EST5EDT,M3.2.0,M11.1.0` |
| `weather_api_key` | — | OpenWeatherMap API key |
| `weather_city` | Toronto | City name for weather lookup |

---

## Touch Button

The capacitive touch pad (GPIO32) supports three gestures:

| Gesture | Action |
|---------|--------|
| Single tap | Skip to next GIF immediately |
| Double tap | Clear GIF cache and reload |
| Hold (1 s) | Restart device |

A red bar flashes at the top of the screen while the pad is pressed.

---

## Dependencies

Managed by PlatformIO (`platformio.ini`):

| Library | Purpose |
|---------|---------|
| `bodmer/TFT_eSPI` | ST7789 display driver |
| `bitbank2/AnimatedGIF` | GIF decoder |
| `bblanchon/ArduinoJson` | Config JSON parse/serialize |
| `mathieucarbou/ESPAsyncWebServer` | Non-blocking web server |
| `tzapu/WiFiManager` | First-boot WiFi provisioning AP |
| `arduino-libraries/NTPClient` | NTP time sync for clock mode |
