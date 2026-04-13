#include "popup_menu.h"

PopupMenu popupMenu;

static const char *ITEM_LABELS[3] = { "Clear Cache", "Restart", "Close Menu" };

void PopupMenu::begin(TFT_eSPI *tft) {
    _tft = tft;
}

void PopupMenu::show(uint32_t now) {
    _visible      = true;
    _selected     = 0;
    _lastTouchMs  = now;
    _pressStartMs = 0;
    _lastPressed  = false;
    _needsRedraw  = true;
}

void PopupMenu::draw(uint32_t now) {
    if (!_tft) return;
    _lastDrawMs = now;
    _needsRedraw = false;

    // Outer frame with double-line border (MS-DOS style)
    _tft->fillRect(MX, MY, MW, MH, TFT_NAVY);
    _tft->drawRect(MX, MY, MW, MH, TFT_WHITE);
    _tft->drawRect(MX + 2, MY + 2, MW - 4, MH - 4, TFT_LIGHTGREY);

    // Title bar
    _tft->fillRect(MX + 1, MY + 1, MW - 2, TITLE_H, TFT_DARKGREY);
    _tft->drawFastHLine(MX + 1, MY + TITLE_H + 1, MW - 2, TFT_WHITE);

    // Countdown bar (left side of title bar)
    uint32_t elapsed   = now - _lastTouchMs;
    uint32_t remaining = (elapsed < AUTO_DISMISS_MS) ? (AUTO_DISMISS_MS - elapsed) : 0;
    uint16_t fillW     = (uint16_t)(50UL * remaining / AUTO_DISMISS_MS);

    _tft->fillRect(MX + 5, MY + 5, 50, 10, TFT_BLACK);      // Dark trough
    if (fillW > 0) {
        _tft->fillRect(MX + 5, MY + 5, fillW, 10, TFT_WHITE);  // White fill
    }

    // Centred title
    _tft->setTextFont(2);
    _tft->setTextSize(1);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tft->drawString("[ MENU ]", MX + MW / 2, MY + 1 + TITLE_H / 2);

    // Determine hold progress in dots
    uint32_t holdMs = (_pressStartMs > 0 && _lastPressed)
                      ? (now - _pressStartMs) : 0;
    uint8_t holdDots = (holdMs >= 1500) ? 3
                     : (holdMs >= 1000) ? 2
                     : (holdMs >= 500)  ? 1 : 0;

    // Draw items
    for (uint8_t i = 0; i < ITEM_COUNT; i++) {
        const uint8_t iy = MY + 1 + TITLE_H + 2 + i * ITEM_H;
        const bool sel = (i == _selected);
        const bool confirming = sel && (_pressStartMs > 0) && _lastPressed && (holdMs > 0);

        // Colors: selected text is yellow, confirming inverts (yellow bg, dark text)
        uint16_t bgColor   = confirming ? TFT_YELLOW : TFT_NAVY;
        uint16_t textColor = confirming ? TFT_NAVY   : (sel ? TFT_YELLOW : TFT_LIGHTGREY);

        _tft->fillRect(MX + 3, iy, MW - 6, ITEM_H - 2, bgColor);

        // Item label with arrow and hold-progress dots
        char buf[40];
        if (confirming) {
            const char *dots = (holdDots == 3) ? "..." : (holdDots == 2) ? ".." : (holdDots == 1) ? "." : "";
            snprintf(buf, sizeof(buf), " > %s %s", ITEM_LABELS[i], dots);
        } else if (sel) {
            snprintf(buf, sizeof(buf), " > %s", ITEM_LABELS[i]);
        } else {
            snprintf(buf, sizeof(buf), "   %s", ITEM_LABELS[i]);
        }

        _tft->setTextDatum(ML_DATUM);
        _tft->setTextColor(textColor, bgColor);
        _tft->drawString(buf, MX + 6, iy + ITEM_H / 2);
    }
}

PopupMenu::Action PopupMenu::tick(bool pressed, uint32_t now) {
    if (!_visible) return Action::NONE;

    // Rising edge — button pressed
    if (pressed && !_lastPressed) {
        _pressStartMs = now;
        _lastTouchMs  = now;
        _needsRedraw  = true;
    }

    // While pressed — periodic redraw to update hold-progress dots every 400ms
    if (pressed) {
        _lastTouchMs = now;
        if ((now - _lastDrawMs) >= 400) {
            _needsRedraw = true;
        }
    }

    // Falling edge — button released
    Action result = Action::NONE;
    if (!pressed && _lastPressed) {
        uint32_t holdMs = now - _pressStartMs;

        if (holdMs >= HOLD_SELECT_MS) {
            // User held for 2+ seconds → confirm selection
            _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);  // Erase menu before closing
            _visible = false;
            switch (_selected) {
                case 0: result = Action::CLEAR_CACHE; break;
                case 1: result = Action::RESTART; break;
                default: result = Action::CLOSE; break;
            }
        } else {
            // Short tap → navigate to next item (cycle)
            _selected = (_selected + 1) % ITEM_COUNT;
            _pressStartMs = 0;
            _needsRedraw  = true;
        }
    }

    // Auto-dismiss after 7 seconds of no touch
    if (_visible && (now - _lastTouchMs) >= AUTO_DISMISS_MS) {
        _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);  // Erase menu before closing
        _visible = false;
        result = Action::CLOSE;
    }

    // Periodic redraw for smooth countdown bar animation (every 500ms)
    if (_visible && !pressed && (now - _lastDrawMs) >= 500) {
        _needsRedraw = true;
    }

    // Redraw if state changed
    if (_visible && _needsRedraw) {
        draw(now);
    }

    _lastPressed = pressed;
    return result;
}
