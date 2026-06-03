#include "popup_menu.h"

PopupMenu popupMenu;

static const char *ITEM_LABELS[4] = { "Information", "Clear Cache", "Restart", "Close Menu" };

void PopupMenu::begin(TFT_eSPI *tft) {
    _tft = tft;
    _spr = new TFT_eSprite(tft);
    if (_spr) _spr->createSprite(MW, MH);  // Allocate once; kept for lifetime of app
}

void PopupMenu::show(uint32_t now) {
    _visible      = true;
    _selected     = 0;
    _lastTouchMs  = now;
    _pressStartMs = 0;
    _lastPressed  = false;
    _needsRedraw  = true;
    _showingInfo  = false;
}

void PopupMenu::showInfo(const char *ip, int rssi) {
    snprintf(_infoLines[0], INFO_LINE_LEN, "Settings portal:");
    snprintf(_infoLines[1], INFO_LINE_LEN, "http://%s/", ip);
    snprintf(_infoLines[2], INFO_LINE_LEN, " ");
    snprintf(_infoLines[3], INFO_LINE_LEN, "How to connect:");
    snprintf(_infoLines[4], INFO_LINE_LEN, "1. Same WiFi net");
    snprintf(_infoLines[5], INFO_LINE_LEN, "2. Open browser");
    snprintf(_infoLines[6], INFO_LINE_LEN, "3. Enter URL above");
    snprintf(_infoLines[7], INFO_LINE_LEN, " ");
    snprintf(_infoLines[8], INFO_LINE_LEN, "Signal: %d dBm", rssi);
    snprintf(_infoLines[9], INFO_LINE_LEN, "Hold 3s > close");
    qrcode_initText(&_qrCode, _qrBuffer, 2, ECC_LOW, _infoLines[1]);
    _infoScroll   = 0;
    _showingInfo  = true;
    _pressStartMs = 0;
    _lastPressed  = false;
    _needsRedraw  = true;
}

// ── Info panel draw ────────────────────────────────────────────────────────────
void PopupMenu::drawInfo(uint32_t now) {
    if (!_tft) return;
    _lastDrawMs  = now;
    _needsRedraw = false;

    bool useSprite = _spr && _spr->created();
    TFT_eSPI *d   = useSprite ? static_cast<TFT_eSPI *>(_spr) : _tft;
    const int16_t ox = useSprite ? 0 : MX;
    const int16_t oy = useSprite ? 0 : MY;

    // Outer frame (MS-DOS double-line border)
    d->fillRect(ox, oy, MW, MH, TFT_NAVY);
    d->drawRect(ox, oy, MW, MH, TFT_WHITE);
    d->drawRect(ox + 2, oy + 2, MW - 4, MH - 4, TFT_LIGHTGREY);

    // Title bar
    d->fillRect(ox + 1, oy + 1, MW - 2, TITLE_H, TFT_DARKGREY);
    d->drawFastHLine(ox + 1, oy + TITLE_H + 1, MW - 2, TFT_WHITE);

    // Countdown bar (same as main menu — depletes over 7s, returns to menu on expire)
    uint32_t elapsed   = now - _lastTouchMs;
    uint32_t remaining = (elapsed < AUTO_DISMISS_MS) ? (AUTO_DISMISS_MS - elapsed) : 0;
    uint16_t fillW     = (uint16_t)(50UL * remaining / AUTO_DISMISS_MS);
    d->fillRect(ox + 5, oy + 5, 50, 10, TFT_BLACK);
    if (fillW > 0)
        d->fillRect(ox + 5, oy + 5, fillW, 10, TFT_WHITE);

    d->setTextFont(2);
    d->setTextSize(1);
    d->setTextDatum(MC_DATUM);
    d->setTextColor(TFT_WHITE, TFT_DARKGREY);
    d->drawString("[ Information ]", ox + MW / 2, oy + 1 + TITLE_H / 2);

    // Content area geometry
    const int16_t contentTop = oy + 1 + TITLE_H + 2;
    const int16_t contentH   = MH - (1 + TITLE_H + 2) - 3;  // 114px

    if (_infoScroll != QR_PAGE_SCROLL) {
        // ── Text + scrollbar ──────────────────────────────────────────────────
        const int16_t sbX = ox + MW - SCROLLBAR_W - 2;

        // Scrollbar track (MS-DOS style: dark grey with light-grey left border)
        d->fillRect(sbX, contentTop, SCROLLBAR_W, contentH, TFT_DARKGREY);
        d->drawFastVLine(sbX, contentTop, contentH, TFT_LIGHTGREY);

        // Scrollbar thumb — range covers text pages only (not QR page)
        const uint8_t maxScroll = (INFO_LINE_COUNT > INFO_VISIBLE) ? INFO_LINE_COUNT - INFO_VISIBLE : 0;
        if (maxScroll > 0) {
            const int16_t thumbH = max((int16_t)8, (int16_t)(INFO_VISIBLE * contentH / INFO_LINE_COUNT));
            const int16_t thumbY = contentTop + (int16_t)(_infoScroll * (contentH - thumbH) / maxScroll);
            d->fillRect(sbX + 1, thumbY, SCROLLBAR_W - 1, thumbH, TFT_WHITE);
        } else {
            d->fillRect(sbX + 1, contentTop, SCROLLBAR_W - 1, contentH, TFT_WHITE);
        }

        // Text lines
        d->setTextDatum(ML_DATUM);
        d->setTextFont(2);
        d->setTextSize(1);
        for (uint8_t i = 0; i < INFO_VISIBLE; i++) {
            uint8_t lineIdx = _infoScroll + i;
            if (lineIdx >= INFO_LINE_COUNT) break;
            const int16_t lineY = contentTop + i * 16 + 8;
            uint16_t color = (lineIdx == 1) ? TFT_YELLOW
                           : (lineIdx == 9) ? TFT_LIGHTGREY
                           : TFT_WHITE;
            d->setTextColor(color, TFT_NAVY);
            d->drawString(_infoLines[lineIdx], ox + 5, lineY);
        }
    } else {
        // ── QR page ───────────────────────────────────────────────────────────
        d->setTextDatum(ML_DATUM);
        d->setTextFont(2);
        d->setTextSize(1);
        d->setTextColor(TFT_WHITE, TFT_NAVY);
        d->drawString("Scan to open:", ox + 5, contentTop + 8);

        const uint8_t qrSize = _qrCode.size;          // 25 for version 2
        const int16_t qrW    = qrSize * QR_MODULE_PX; // 75px
        const int16_t qrX    = ox + (MW - qrW) / 2;  // centred horizontally
        const int16_t qrY    = contentTop + 20;        // below label

        // White quiet-zone background (2 modules each side)
        const int16_t margin = 2 * QR_MODULE_PX;
        d->fillRect(qrX - margin, qrY - margin,
                    qrW + 2 * margin, qrW + 2 * margin, TFT_WHITE);

        // Dark modules
        for (uint8_t row = 0; row < qrSize; row++) {
            for (uint8_t col = 0; col < qrSize; col++) {
                if (qrcode_getModule(&_qrCode, col, row)) {
                    d->fillRect(qrX + col * QR_MODULE_PX,
                                qrY + row * QR_MODULE_PX,
                                QR_MODULE_PX, QR_MODULE_PX, TFT_BLACK);
                }
            }
        }
    }

    if (useSprite)
        _spr->pushSprite(MX, MY);
}

// ── Regular menu draw ──────────────────────────────────────────────────────────
void PopupMenu::draw(uint32_t now) {
    if (!_tft) return;
    _lastDrawMs  = now;
    _needsRedraw = false;

    bool useSprite = _spr && _spr->created();
    TFT_eSPI *d   = useSprite ? static_cast<TFT_eSPI *>(_spr) : _tft;
    const int16_t ox = useSprite ? 0 : MX;
    const int16_t oy = useSprite ? 0 : MY;

    // Outer frame with double-line border (MS-DOS style)
    d->fillRect(ox, oy, MW, MH, TFT_NAVY);
    d->drawRect(ox, oy, MW, MH, TFT_WHITE);
    d->drawRect(ox + 2, oy + 2, MW - 4, MH - 4, TFT_LIGHTGREY);

    // Title bar
    d->fillRect(ox + 1, oy + 1, MW - 2, TITLE_H, TFT_DARKGREY);
    d->drawFastHLine(ox + 1, oy + TITLE_H + 1, MW - 2, TFT_WHITE);

    // Countdown bar (left side of title bar)
    uint32_t elapsed   = now - _lastTouchMs;
    uint32_t remaining = (elapsed < AUTO_DISMISS_MS) ? (AUTO_DISMISS_MS - elapsed) : 0;
    uint16_t fillW     = (uint16_t)(50UL * remaining / AUTO_DISMISS_MS);

    d->fillRect(ox + 5, oy + 5, 50, 10, TFT_BLACK);
    if (fillW > 0)
        d->fillRect(ox + 5, oy + 5, fillW, 10, TFT_WHITE);

    // Centred title
    d->setTextFont(2);
    d->setTextSize(1);
    d->setTextDatum(MC_DATUM);
    d->setTextColor(TFT_WHITE, TFT_DARKGREY);
    d->drawString("[ MENU ]", ox + MW / 2, oy + 1 + TITLE_H / 2);

    // Determine hold progress in dots
    uint32_t holdMs  = (_pressStartMs > 0 && _lastPressed) ? (now - _pressStartMs) : 0;
    uint8_t holdDots = (holdMs >= 1500) ? 3 : (holdMs >= 1000) ? 2 : (holdMs >= 500) ? 1 : 0;

    // Draw items
    for (uint8_t i = 0; i < ITEM_COUNT; i++) {
        const int16_t iy      = oy + 1 + TITLE_H + 2 + i * ITEM_H;
        const bool sel        = (i == _selected);
        const bool confirming = sel && (_pressStartMs > 0) && _lastPressed && (holdMs > 0);

        uint16_t bgColor   = confirming ? TFT_YELLOW : TFT_NAVY;
        uint16_t textColor = confirming ? TFT_NAVY   : (sel ? TFT_YELLOW : TFT_LIGHTGREY);

        d->fillRect(ox + 3, iy, MW - 6, ITEM_H - 2, bgColor);

        char buf[40];
        if (confirming) {
            const char *dots = (holdDots == 3) ? "..." : (holdDots == 2) ? ".." : (holdDots == 1) ? "." : "";
            snprintf(buf, sizeof(buf), " > %s %s", ITEM_LABELS[i], dots);
        } else if (sel) {
            snprintf(buf, sizeof(buf), " > %s", ITEM_LABELS[i]);
        } else {
            snprintf(buf, sizeof(buf), "   %s", ITEM_LABELS[i]);
        }

        d->setTextDatum(ML_DATUM);
        d->setTextColor(textColor, bgColor);
        d->drawString(buf, ox + 6, iy + ITEM_H / 2);
    }

    if (useSprite)
        _spr->pushSprite(MX, MY);
}

// ── Tick ───────────────────────────────────────────────────────────────────────
PopupMenu::Action PopupMenu::tick(bool pressed, uint32_t now) {
    if (!_visible) return Action::NONE;

    // ── Info sub-panel mode ────────────────────────────────────────────────────
    if (_showingInfo) {
        if (pressed && !_lastPressed) {
            _pressStartMs = now;
            _lastTouchMs  = now;
        }
        if (pressed) _lastTouchMs = now;

        if (!pressed && _lastPressed) {
            uint32_t heldMs = now - _pressStartMs;
            if (heldMs >= INFO_HOLD_MS) {
                // 3s hold → close everything
                _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);
                _showingInfo = false;
                _visible     = false;
                _lastPressed = pressed;
                return Action::CLOSE;
            } else {
                // Short tap → scroll one line, wrap at end
                _infoScroll  = (_infoScroll < QR_PAGE_SCROLL) ? _infoScroll + 1 : 0;
                _needsRedraw = true;
            }
        }

        // Auto-dismiss info panel → return to main menu (not full close)
        if ((now - _lastTouchMs) >= AUTO_DISMISS_MS) {
            _showingInfo = false;
            _lastTouchMs = now;  // reset main menu 7s timer
            _needsRedraw = true;
        }

        // Periodic redraw for smooth countdown animation
        if ((now - _lastDrawMs) >= 500) _needsRedraw = true;

        _lastPressed = pressed;
        if (_needsRedraw) {
            if (_showingInfo) drawInfo(now); else draw(now);
        }
        return Action::NONE;
    }

    // ── Normal menu mode ───────────────────────────────────────────────────────

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
            // 2s hold → confirm selection
            switch (_selected) {
                case 0:
                    // Information: stay visible, main.cpp calls showInfo()
                    result = Action::INFO;
                    break;
                case 1:
                    _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);
                    _visible = false;
                    result = Action::CLEAR_CACHE;
                    break;
                case 2:
                    _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);
                    _visible = false;
                    result = Action::RESTART;
                    break;
                default:
                    _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);
                    _visible = false;
                    result = Action::CLOSE;
                    break;
            }
        } else {
            // Short tap → navigate to next item (wraps)
            _selected = (_selected + 1) % ITEM_COUNT;
            _pressStartMs = 0;
            _needsRedraw  = true;
        }
    }

    // Auto-dismiss after 7 seconds of no touch
    if (_visible && (now - _lastTouchMs) >= AUTO_DISMISS_MS) {
        _tft->fillRect(MX, MY, MW, MH, TFT_BLACK);
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
