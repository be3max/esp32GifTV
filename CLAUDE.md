# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

PlatformIO CLI (pio may need full path `C:\Users\user\.platformio\penv\Scripts\pio.exe`):

```bash
pio run -t upload      # Build and flash firmware to ESP32 on COM7
pio run -t uploadfs    # Flash data/ folder to LittleFS partition
pio run -t clean       # Clean build artifacts (needed after changing build_flags)
pio device monitor -b 115200  # Serial monitor
```

**Important:** After changing `build_flags` in platformio.ini (e.g. TFT pin defines), always run `clean` before `upload` — stale TFT_eSPI object files won't pick up new defines. Flash `uploadfs` first when both firmware and filesystem change.

## Hardware: NM-TV-154 (NMMiner TV-Miner v1.0)

- **MCU:** ESP32-D0WD-V3, 240 MHz, 4 MB flash, no PSRAM
- **Display:** ST7789 1.54" 240×240 TFT over HSPI
- **USB-Serial:** CH340 on COM7

### Critical pin configuration

| Function | GPIO | Notes |
|----------|------|-------|
| TFT_MOSI | 13 | HSPI data |
| TFT_SCLK | 14 | HSPI clock |
| TFT_CS | 15 | HSPI chip select |
| TFT_DC | 2 | Data/Command |
| TFT_RST | -1 | Not connected |
| TFT_BL | 19 | **Active LOW** |
| Power | 21 | **Active LOW** — must `digitalWrite(21, LOW)` before `tft.begin()` |

Display init sequence: power pin LOW → `tft.begin()` → `tft.setSwapBytes(true)` → `tft.setRotation(0)`.

Source: https://www.nmminer.com/2026/03/02/how-to-develop-nm-tv-custom-firmware/

## Architecture

Three display modes selected via web portal, driven by a state machine in `main.cpp`:

- **GIF mode** — `gif_player` downloads GIF via HTTP into heap (max 100 KB, no PSRAM), decodes with AnimatedGIF library, renders scanlines to TFT via SPI callback. Cycles through URL list.
- **Clock mode** — `clock_display` syncs via NTPClient, redraws on second change.
- **Weather mode** — `weather` fetches OpenWeatherMap API every 10 min, renders to TFT.

`config_manager` persists all settings as JSON in LittleFS (`/config.json`). `web_portal` runs ESPAsyncWebServer on port 80 serving a single-page app from LittleFS (`data/index.html`) and REST endpoints at `/api/config`, `/api/status`, `/api/restart`.

WiFiManager handles provisioning — creates AP "GifScreen-Setup" if no stored credentials.

## Flash Layout (partitions.csv)

Two 1.5 MB OTA app slots + 960 KB LittleFS. Firmware currently uses ~76% of app slot (~1.2 MB). GIF buffer (heap-allocated) competes with stack and TFT buffers in ~200 KB usable RAM.

## Key Constraints

- **No PSRAM** — GIF downloads capped at 100 KB (`MAX_GIF_BYTES` in gif_player.cpp). Keep heap allocations minimal.
- **GPIO 2 is a boot strapping pin** — works fine as TFT_DC after boot, but avoid pulling it in ways that affect boot mode.
- **Backlight and power are both active LOW** — setting either HIGH turns them OFF.
- **`tft.setSwapBytes(true)`** is required for correct color rendering on this panel.
