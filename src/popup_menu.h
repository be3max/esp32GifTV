#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <qrcode.h>

// Retro TUI-style popup menu.
// Show with show(now). Navigate with tap (single press). Confirm with 2s hold.
// Auto-dismisses after 7 s of no touch.
class PopupMenu {
public:
    enum class Action { NONE = 0, CLEAR_CACHE, RESTART, INFO, CLOSE };

    void begin(TFT_eSPI *tft);

    // Open the menu. Call once when the user's long-press fires.
    void show(uint32_t now);

    // Enter scrollable info sub-panel. Call from main.cpp when Action::INFO fires.
    void showInfo(const char *ip, int rssi);

    bool isVisible() const { return _visible; }

    // Call every loop iteration while the menu is open.
    // pressed = current debounced button state (btnPressed from main.cpp).
    // Returns Action when the user confirms a selection; NONE otherwise.
    Action tick(bool pressed, uint32_t now);

private:
    void draw(uint32_t now);
    void drawInfo(uint32_t now);

    TFT_eSPI    *_tft = nullptr;
    TFT_eSprite *_spr = nullptr;
    bool      _visible      = false;
    uint8_t   _selected     = 0;
    uint32_t  _lastTouchMs  = 0;
    uint32_t  _pressStartMs = 0;
    bool      _lastPressed  = false;
    bool      _needsRedraw  = true;
    uint32_t  _lastDrawMs   = 0;

    bool      _showingInfo  = false;
    uint8_t   _infoScroll   = 0;

    static constexpr uint32_t AUTO_DISMISS_MS  = 7000;
    static constexpr uint32_t HOLD_SELECT_MS   = 2000;
    static constexpr uint32_t INFO_HOLD_MS     = 3000;
    static constexpr uint8_t  ITEM_COUNT       = 4;
    static constexpr uint8_t  INFO_LINE_COUNT  = 10;
    static constexpr uint8_t  INFO_LINE_LEN    = 25;
    static constexpr uint8_t  SCROLLBAR_W      = 8;
    static constexpr uint8_t  QR_MODULE_PX     = 3;    // pixels per QR module
    static constexpr uint8_t  QR_BUFFER_SIZE   = 150;  // fits QR version 2–4

    // Layout — centred on 240×240 display
    static constexpr uint8_t  MX      = 10;
    static constexpr uint8_t  MY      = 62;
    static constexpr uint8_t  MW      = 220;
    static constexpr uint8_t  MH      = 140;
    static constexpr uint8_t  TITLE_H = 20;
    static constexpr uint8_t  ITEM_H  = 28;

    // Info panel: 7 lines visible (content height = MH-26 = 114, 114/16 = 7)
    static constexpr uint8_t  INFO_VISIBLE   = (MH - 26) / 16;
    static constexpr uint8_t  QR_PAGE_SCROLL = INFO_LINE_COUNT - INFO_VISIBLE + 1; // = 4

    char    _infoLines[INFO_LINE_COUNT][INFO_LINE_LEN];
    QRCode  _qrCode;
    uint8_t _qrBuffer[QR_BUFFER_SIZE];
};

extern PopupMenu popupMenu;
