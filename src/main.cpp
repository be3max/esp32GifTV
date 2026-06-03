// NOTE: WiFiManager.h must precede web_portal.h (ESPAsyncWebServer) to avoid
// HTTP method enum collisions between the two libraries' transitive includes.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include "esp_bt.h"

#include "config_manager.h"
#include "gif_player.h"
#include "web_portal.h"
#include "weather.h"
#include "clock_display.h"
#include "status_bar.h"
#include "boot_screen.h"
#include "popup_menu.h"

// ── Hardware (NM-TV-154) ─────────────────────────────────────────────────────
#define TFT_POWER_PIN  21   // Active LOW — P-FET gate powering the display
#define PIN_BACKLIGHT  19   // Active LOW

// ── Touch button (TC pad = GPIO32 / T9, native capacitive touch) ─────────────
// GPIO32 reads ~83 when touched, ~107 when idle
constexpr uint8_t  PIN_TOUCH          = 32;
constexpr uint8_t  TOUCH_THRESHOLD    = 95;   // touchRead() < this = touched
constexpr uint32_t BTN_DEBOUNCE_MS    = 20;   // ignore transitions shorter than this
constexpr uint32_t BTN_HOLD_MS        = 1000; // press duration for HOLD event
constexpr uint32_t BTN_DTAP_MS        = 400;  // max gap between two taps for DOUBLE_TAP
constexpr bool     ENABLE_TOUCH_DEBUG = false; // set true to print touchRead() values

TFT_eSPI tft = TFT_eSPI();

// ── State machine ─────────────────────────────────────────────────────────────
enum class AppMode { GIF, CLOCK, WEATHER };

static AppMode   currentMode    = AppMode::GIF;
static uint32_t  weatherFetchMs = 0;
static uint32_t  statusBarDrawMs = 0;

// ── Button FSM ────────────────────────────────────────────────────────────────
enum class BtnEvent { NONE, TAP, DOUBLE_TAP, HOLD };
enum class BtnState { IDLE, PRESSED, HELD, TAP_WAIT, SECOND_PRESS };

static BtnState  btnState      = BtnState::IDLE;
static uint32_t  btnPressMs    = 0;
static uint32_t  btnReleaseMs  = 0;
static bool      btnLastRaw    = false;
static uint32_t  btnDebounceMs = 0;
static bool      btnStable     = false;
static bool      btnPressed    = false;  // True while finger is on touch pad (for visual feedback)

static AppMode pickMode(const DeviceConfig &c) {
    switch (c.display_mode) {
        case DISPLAY_MODE_CLOCK:   return AppMode::CLOCK;
        case DISPLAY_MODE_WEATHER: return AppMode::WEATHER;
        default:                   return AppMode::GIF;  // GIF_ONLY and GIF_STATUS
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static void drawTouchIndicator(bool show) {
    // Draw/clear 16-pixel red bar at top when touch is pressed
    if (show) {
        tft.fillRect(0, 0, tft.width(), 16, TFT_RED);
    } else {
        tft.fillRect(0, 0, tft.width(), 16, TFT_BLACK);
    }
}

static int8_t parseTimezoneOffset(const char *tzStr) {
    // Parse "UTC-5", "UTC+2", "UTC0" etc. and return the numeric offset in hours
    // Format: "UTC" followed by optional sign and number
    if (!tzStr || strlen(tzStr) < 3) return 0;
    int offset = atoi(tzStr + 3);  // Skip "UTC" and parse the number
    return (int8_t)offset;
}

static const char* buildPosixTz(int8_t offsetHours, char *buf, size_t bufSize) {
    // Build POSIX TZ string with DST rules for North American zones
    // All NA DST zones use M3.2.0 (2nd Sun Mar) to M11.1.0 (1st Sun Nov)
    struct {
        int8_t offset;
        const char *posix;
    } NA_ZONES[] = {
        { -5, "EST5EDT,M3.2.0,M11.1.0" },  // Eastern
        { -6, "CST6CDT,M3.2.0,M11.1.0" },  // Central
        { -7, "MST7MDT,M3.2.0,M11.1.0" },  // Mountain
        { -8, "PST8PDT,M3.2.0,M11.1.0" },  // Pacific
    };
    for (size_t i = 0; i < sizeof(NA_ZONES) / sizeof(NA_ZONES[0]); i++) {
        if (NA_ZONES[i].offset == offsetHours) {
            strlcpy(buf, NA_ZONES[i].posix, bufSize);
            return buf;
        }
    }
    // Fall back: no DST for non-NA offsets
    snprintf(buf, bufSize, "UTC%d", -offsetHours);
    return buf;
}

// ── Touch Button FSM ──────────────────────────────────────────────────────────
static BtnEvent checkButton() {
    uint16_t touchVal = touchRead(PIN_TOUCH);
    bool raw = (touchVal < TOUCH_THRESHOLD);
    uint32_t now = millis();

    // Debug: optionally print touch readings every 1 second for calibration
    if (ENABLE_TOUCH_DEBUG) {
        static uint32_t lastDebugMs = 0;
        if (now - lastDebugMs > 1000) {
            Serial.printf("[TOUCH] GPIO32 = %d %s\n", touchVal, raw ? "[PRESSED]" : "");
            lastDebugMs = now;
        }
    }

    // Debounce: only accept state after stable for BTN_DEBOUNCE_MS
    if (raw != btnLastRaw) { btnLastRaw = raw; btnDebounceMs = now; }
    bool pressed = btnStable;
    if ((now - btnDebounceMs) >= BTN_DEBOUNCE_MS) { pressed = raw; btnStable = raw; }

    // Track pressed state change for visual feedback (skip if menu is visible)
    if (pressed != btnPressed) {
        btnPressed = pressed;
        if (!popupMenu.isVisible()) {
            drawTouchIndicator(pressed);
        }
    }

    switch (btnState) {
        case BtnState::IDLE:
            if (pressed) { btnState = BtnState::PRESSED; btnPressMs = now; }
            break;

        case BtnState::PRESSED:
            if (!pressed) {
                // Released before hold threshold — first tap done
                btnState = BtnState::TAP_WAIT;
                btnReleaseMs = now;
            } else if ((now - btnPressMs) >= BTN_HOLD_MS) {
                btnState = BtnState::HELD;
            }
            break;

        case BtnState::HELD:
            // Fire HOLD on release (avoids repeated events)
            if (!pressed) { btnState = BtnState::IDLE; return BtnEvent::HOLD; }
            break;

        case BtnState::TAP_WAIT:
            if (pressed) {
                // Second press started
                btnState = BtnState::SECOND_PRESS; btnPressMs = now;
            } else if ((now - btnReleaseMs) >= BTN_DTAP_MS) {
                // Window expired — confirm single TAP
                btnState = BtnState::IDLE; return BtnEvent::TAP;
            }
            break;

        case BtnState::SECOND_PRESS:
            if (!pressed) {
                btnState = BtnState::IDLE; return BtnEvent::DOUBLE_TAP;
            } else if ((now - btnPressMs) >= BTN_HOLD_MS) {
                // Second press held too long — treat as HOLD
                btnState = BtnState::HELD;
            }
            break;
    }
    return BtnEvent::NONE;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] esp32GifScreenSaver");

    // Release BT controller memory — not used in this project.
    // Reclaims ~60-100 KB of contiguous heap before WiFi fragments it,
    // enabling the GIF frame buffer to be allocated successfully.
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    // Power ON the display (active LOW)
    pinMode(TFT_POWER_PIN, OUTPUT);
    digitalWrite(TFT_POWER_PIN, LOW);
    delay(100);

    // Display init
    tft.begin();
    tft.setSwapBytes(true);
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Boot screen takes over the display from here
    bootScreen.begin(&tft);
    bootScreen.drawHeader();
    bootScreen.postLine("ESP32-D0WD-V3 @ 240MHz", BootScreen::Tag::OK);
    bootScreen.postLine("BT controller mem released", BootScreen::Tag::OK);
    bootScreen.postLine("Display ST7789 1.54\" 240x240", BootScreen::Tag::OK);

    // Allocate the GIF combined buffer as early as possible — before LittleFS
    // and WiFi fragment the heap — to ensure a large contiguous block is available.
    bootScreen.postLine("Allocating GIF frame buffer", BootScreen::Tag::WAIT);
    gifPlayer.begin(&tft);
    popupMenu.begin(&tft);  // Allocate menu sprite before WiFi fragments heap
    bootScreen.updateLastTag(BootScreen::Tag::OK);

    bootScreen.postLine("Clearing boot cache", BootScreen::Tag::WAIT);
    gifPlayer.clearCacheOnBoot();
    bootScreen.updateLastTag(BootScreen::Tag::OK);

    // Config (LittleFS)
    bootScreen.postLine("Mounting LittleFS", BootScreen::Tag::WAIT);
    if (!configMgr.begin()) {
        bootScreen.updateLastTag(BootScreen::Tag::FAIL);
        bootScreen.postLine("LittleFS failed — continuing", BootScreen::Tag::FAIL);
        delay(3000);
    } else {
        bootScreen.updateLastTag(BootScreen::Tag::OK);
        bootScreen.postLine("Loading /config.json", BootScreen::Tag::OK);
    }

    // Initialize NTP with timezone in one call (always, even in GIF mode, for status bar)
    // Note: configTzTime() atomically sets both NTP server and POSIX TZ env var
    // For North American zones, applies automatic DST rules (e.g., EST5EDT,M3.2.0,M11.1.0)
    const DeviceConfig &cfg = configMgr.getConfig();
    int8_t tzOffset = parseTimezoneOffset(cfg.timezone);
    char posixTz[32];
    buildPosixTz(tzOffset, posixTz, sizeof(posixTz));
    configTzTime(posixTz, "pool.ntp.org");
    bootScreen.postLine("NTP + Timezone configured", BootScreen::Tag::OK);

    // WiFi
    bootScreen.postLine("WiFi: connecting...", BootScreen::Tag::WAIT);
    WiFiManager wm;
    wm.setConfigPortalTimeout(120);
    if (!wm.autoConnect("GifScreen-Setup")) {
        Serial.println("[WiFi] connect failed, restarting");
        bootScreen.updateLastTag(BootScreen::Tag::FAIL);
        bootScreen.postLine("WiFi failed — restarting", BootScreen::Tag::FAIL);
        delay(3000);
        ESP.restart();
    }
    bootScreen.updateLastTag(BootScreen::Tag::OK);
    Serial.printf("[WiFi] connected, IP: %s\n", WiFi.localIP().toString().c_str());

    // Seed random number generator for better GIF URL selection randomness
    randomSeed(time(NULL) ^ WiFi.RSSI() ^ millis());

    // Sub-systems (gifPlayer already begun above)
    weather.begin(&tft);
    statusBar.begin(&tft);
    webPortal.begin();

    currentMode = pickMode(cfg);
    gifPlayer.setRefreshInterval(cfg.gif_refresh_seconds);
    gifPlayer.setClipHeight(cfg.display_mode == DISPLAY_MODE_GIF_ONLY ? TFT_HEIGHT : STATUS_BAR_Y);
    if (currentMode == AppMode::CLOCK) {
        int8_t tzOffset = parseTimezoneOffset(cfg.timezone);
        clockDisplay.begin(&tft, "pool.ntp.org", tzOffset);
    }

    {
        const char *modeStr = (currentMode == AppMode::CLOCK)   ? "CLOCK"
                            : (currentMode == AppMode::WEATHER) ? "WEATHER"
                                                                 : "GIF";
        char modeLine[32];
        snprintf(modeLine, sizeof(modeLine), "Mode: %s", modeStr);
        bootScreen.postLine("Subsystems initialized", BootScreen::Tag::OK);
        bootScreen.postLine(modeLine, BootScreen::Tag::OK);
    }

    // Connected! dialog (MS-DOS QBasic style), then clear for main loop
    bootScreen.showConnectedDialog(
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        3000
    );
    bootScreen.done();

    Serial.println("[BOOT] setup complete");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    webPortal.update();

    if (webPortal.pendingRestart) {
        delay(500);
        ESP.restart();
    }

    // ── Button handling ───────────────────────────────────────────────────────
    BtnEvent btnEvt = checkButton();

    // Menu mode — bypass all other button handling
    if (popupMenu.isVisible()) {
        PopupMenu::Action menuAct = popupMenu.tick(btnPressed, millis());

        if (menuAct != PopupMenu::Action::NONE) {
            // Consume any in-flight button state so release doesn't re-trigger HOLD
            btnState = BtnState::IDLE;
        }

        if (menuAct == PopupMenu::Action::CLEAR_CACHE) {
            Serial.println("[MENU] Clear Cache");
            gifPlayer.stop();
            gifPlayer.clearCacheOnBoot();
            gifPlayer.clearListCache();
            bootScreen.showDialog("[ Cache Manager ]", "Cache cleared", 2000);
            tft.fillScreen(TFT_BLACK);
            gifPlayer.fetchAndPlay();
        } else if (menuAct == PopupMenu::Action::RESTART) {
            Serial.println("[MENU] Restart");
            ESP.restart();
        } else if (menuAct == PopupMenu::Action::INFO) {
            Serial.println("[MENU] Info");
            popupMenu.showInfo(WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        // Action::CLOSE or NONE → menu self-dismisses or continues
    } else {
        // Normal button handling (when menu not visible)
        if (btnEvt == BtnEvent::HOLD) {
            Serial.println("[BTN] HOLD → show menu");
            popupMenu.show(millis());
        } else if (currentMode == AppMode::GIF && btnEvt == BtnEvent::TAP) {
            Serial.println("[BTN] TAP → next GIF");
            gifPlayer.fetchAndPlay();
        }
    }

    const DeviceConfig &cfg = configMgr.getConfig();

    // React to config changes from web portal (skip if menu is visible)
    if (!popupMenu.isVisible()) {
        AppMode cfgMode = pickMode(cfg);
        if (cfgMode != currentMode) {
            if (currentMode == AppMode::GIF) gifPlayer.stop();
            currentMode = cfgMode;
            tft.fillScreen(TFT_BLACK);
            gifPlayer.setClipHeight(cfg.display_mode == DISPLAY_MODE_GIF_ONLY ? TFT_HEIGHT : STATUS_BAR_Y);
            if (currentMode == AppMode::CLOCK) {
                int8_t tzOffset = parseTimezoneOffset(cfg.timezone);
                clockDisplay.begin(&tft, "pool.ntp.org", tzOffset);
            }
        }
    }

    // Skip mode processing while menu is visible (GIF is paused)
    if (!popupMenu.isVisible()) {
        switch (currentMode) {

            case AppMode::GIF: {
                uint32_t now = millis();

                // Show loading animation while first GIF is loading
                if (!gifPlayer.isPlaying()) {
                    bootScreen.tickLoadingAnim(now);
                }

                gifPlayer.setRefreshInterval(cfg.gif_refresh_seconds);
                gifPlayer.tick();

            // Fetch weather periodically if enabled (for status bar in GIF_STATUS mode)
            if (cfg.display_mode == DISPLAY_MODE_GIF_STATUS && cfg.weather_api_key[0] != '\0') {
                if (!weather.data.valid || (now - weatherFetchMs > 10UL * 60000UL)) {
                    weather.fetch(cfg.weather_api_key, cfg.weather_city);
                    weatherFetchMs = now;
                }
            }

            // Draw status bar periodically if in GIF_STATUS mode
            if (cfg.display_mode == DISPLAY_MODE_GIF_STATUS && (now - statusBarDrawMs > 60000)) {
                statusBar.draw(true, cfg.weather_api_key[0] != '\0');
                statusBarDrawMs = now;
            }
            break;
            }

            case AppMode::CLOCK:
                clockDisplay.update();
                delay(100);
                break;

            case AppMode::WEATHER: {
                uint32_t now = millis();
                if (!weather.data.valid || (now - weatherFetchMs > 10UL * 60000UL)) {
                    weather.fetch(cfg.weather_api_key, cfg.weather_city);
                    weatherFetchMs = now;
                }
                weather.draw();
                delay(30000);
                break;
            }
        }
    }
}
