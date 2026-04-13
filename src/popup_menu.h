#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Retro TUI-style popup menu.
// Show with show(now). Navigate with tap (single press). Confirm with 2s hold.
// Auto-dismisses after 7 s of no touch.
class PopupMenu {
public:
    enum class Action { NONE = 0, CLEAR_CACHE, RESTART, CLOSE };

    void begin(TFT_eSPI *tft);

    // Open the menu. Call once when the user's long-press fires.
    void show(uint32_t now);

    bool isVisible() const { return _visible; }

    // Call every loop iteration while the menu is open.
    // pressed = current debounced button state (btnPressed from main.cpp).
    // Returns Action when the user confirms a selection; NONE otherwise.
    Action tick(bool pressed, uint32_t now);

private:
    void draw(uint32_t now);

    TFT_eSPI *_tft         = nullptr;
    bool      _visible     = false;
    uint8_t   _selected    = 0;
    uint32_t  _lastTouchMs  = 0;
    uint32_t  _pressStartMs = 0;
    bool      _lastPressed  = false;
    bool      _needsRedraw  = true;
    uint32_t  _lastDrawMs   = 0;

    static constexpr uint32_t AUTO_DISMISS_MS = 7000;
    static constexpr uint32_t HOLD_SELECT_MS  = 2000;
    static constexpr uint8_t  ITEM_COUNT      = 3;

    // Layout — centred on 240×240 display
    static constexpr uint8_t  MX      = 10;
    static constexpr uint8_t  MY      = 62;
    static constexpr uint8_t  MW      = 220;
    static constexpr uint8_t  MH      = 116;
    static constexpr uint8_t  TITLE_H = 20;
    static constexpr uint8_t  ITEM_H  = 28;
};

extern PopupMenu popupMenu;
