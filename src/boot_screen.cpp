#include "boot_screen.h"
#include "ui_theme.h"
#include <cmath>

BootScreen bootScreen;

// ── Internal helpers ──────────────────────────────────────────────────────────

void BootScreen::setFont() {
    _tft->setTextFont(2);   // 16px tall — readable retro look
    _tft->setTextSize(1);
    _tft->setTextDatum(TL_DATUM);
}

static const char *tagStr(BootScreen::Tag tag) {
    switch (tag) {
        case BootScreen::Tag::OK:   return "[ OK ]";
        case BootScreen::Tag::FAIL: return "[FAIL]";
        case BootScreen::Tag::WAIT: return "[WAIT]";
        default:                    return "";
    }
}

static uint16_t tagColor(BootScreen::Tag tag) {
    switch (tag) {
        case BootScreen::Tag::OK:   return TFT_WHITE;
        case BootScreen::Tag::FAIL: return TFT_RED;
        case BootScreen::Tag::WAIT: return TFT_YELLOW;
        default:                    return TFT_WHITE;
    }
}

void BootScreen::drawTag(uint16_t x, uint16_t y, Tag tag) {
    if (tag == Tag::NONE) return;
    setFont();
    _tft->setTextColor(tagColor(tag), TFT_BLACK);
    _tft->setCursor(x, y);
    _tft->print(tagStr(tag));
}

// Draws the shared MS-DOS dialog chrome (background, borders, title bar).
// Caller draws its own content in the blue area below the separator.
void BootScreen::drawDialogFrame(uint8_t bx, uint8_t by, uint8_t bw, uint8_t bh,
                                 uint8_t titleH, const char *title) {
    uiDrawDosFrame(_tft, bx, by, bw, bh, titleH, title);
}

// ── Public API ────────────────────────────────────────────────────────────────

void BootScreen::begin(TFT_eSPI *tft) {
    _tft       = tft;
    _scrollRow = 0;
    _lastTagX  = 0;
    _lastTagY  = 0;
    _lastTagW  = 0;
    _tft->fillScreen(TFT_BLACK);
}

// Helper: draw a filled 5-pointed star.
// cx,cy = center; R = outer radius; r = inner radius; color = fill color
// Points computed analytically (top point at 270°, every 72°).
static void drawStar(TFT_eSPI *tft, int cx, int cy, int R, int r, uint16_t color) {
    // Precomputed sin/cos for angles 270,306,342,18,54,90,126,162,198,234 degrees
    // (outer at 270+k*72, inner at 270+36+k*72, k=0..4)
    static const float SX[10] = {
         0.000f,  0.588f,  0.951f,  0.951f,  0.588f,  // outer: cos offsets (from 270°)
         0.000f, -0.588f, -0.951f, -0.951f, -0.588f   // not used here — computed below
    };
    // Precomputed: outer points at (270+k*72)°, inner at (306+k*72)°
    // sin(angle) = y-component, cos(angle) = x-component
    // angles in degrees: outer 270,342,54,126,198; inner 306,18,90,162,234
    const float outerA[5] = {270, 342, 54, 126, 198};
    const float innerA[5] = {306,  18, 90, 162, 234};
    int px[10], py[10];
    for (int i = 0; i < 5; i++) {
        float oa = outerA[i] * 3.14159f / 180.0f;
        float ia = innerA[i] * 3.14159f / 180.0f;
        px[i*2]   = cx + (int)(R * cosf(oa));
        py[i*2]   = cy + (int)(R * sinf(oa));
        px[i*2+1] = cx + (int)(r * cosf(ia));
        py[i*2+1] = cy + (int)(r * sinf(ia));
    }
    // 5 spike triangles
    for (int i = 0; i < 5; i++) {
        int next = (i*2+2) % 10;
        tft->fillTriangle(px[i*2], py[i*2], px[i*2+1], py[i*2+1], px[next], py[next], color);
    }
    // Inner pentagon fill
    tft->fillTriangle(px[1], py[1], px[3], py[3], px[5], py[5], color);
    tft->fillTriangle(px[1], py[1], px[5], py[5], px[7], py[7], color);
    tft->fillTriangle(px[1], py[1], px[7], py[7], px[9], py[9], color);
}

void BootScreen::drawHeader() {
    _tft->fillRect(0, 0, TFT_WIDTH, HEADER_H, TFT_BLACK);

    // ── Left: Multi-color BIOS title ──────────────────────────────────────────
    setFont();
    _tft->setCursor(MARGIN_X, 2);

    // "GS" in cyan
    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->print("GS");

    // " BIOS v" in white
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->print(" BIOS v");

    // Firmware version in white
    _tft->print(FIRMWARE_VERSION_STR);

    // ── Right: Energy Star Logo (yellow circle with green star) ──────────────
    const int circleX = 210, circleY = 25, circleR = 20;

    // Yellow circle outline (2px thick)
    _tft->drawCircle(circleX, circleY, circleR, TFT_YELLOW);
    _tft->drawCircle(circleX, circleY, circleR - 1, TFT_YELLOW);

    // "energy" text in yellow inside circle, lower-center
    _tft->setTextFont(1);
    _tft->setTextSize(1);
    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->setCursor(197, 30);
    _tft->print("energy");

    // 5-pointed star in upper-right of circle (green)
    // Star center (220, 10), outer R=8, inner r=3
    drawStar(_tft, 220, 10, 8, 3, TFT_GREEN);

    // Shooting comet tail (dots going upper-left from star)
    _tft->fillCircle(210, 15, 2, TFT_GREEN);
    _tft->fillCircle(202, 20, 1, TFT_GREEN);

    // "EPA POLLUTION PREVENTER" below circle in green (abbreviated to fit)
    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->setCursor(164, 48);
    _tft->print("EPA POLL");
}

void BootScreen::postLine(const char *label, Tag tag) {
    const uint16_t y = SCROLL_TOP + (_scrollRow % MAX_LINES) * LINE_H;

    _tft->fillRect(0, y, TFT_WIDTH, LINE_H - 1, TFT_BLACK);

    setFont();
    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->setCursor(MARGIN_X, y);
    _tft->print(label);

    // Tag position is computed at runtime — font 2 is variable-width
    const uint16_t tagW = (tag != Tag::NONE) ? _tft->textWidth(tagStr(tag)) : 0;
    const uint16_t tagX = TFT_WIDTH - tagW - MARGIN_X;

    _lastTagX = tagX;
    _lastTagY = y;
    _lastTagW = tagW;

    drawTag(tagX, y, tag);
    _scrollRow++;

    // Slow boot animation — retro BIOS effect
    delay(POST_LINE_DELAY_MS);
}

void BootScreen::updateLastTag(Tag tag) {
    setFont();
    const uint16_t newW = (tag != Tag::NONE) ? _tft->textWidth(tagStr(tag)) : 0;
    const uint16_t newX = TFT_WIDTH - newW - MARGIN_X;

    // Erase the union of old and new tag rects to prevent ghost pixels
    const uint16_t eraseX = min(newX, _lastTagX);
    const uint16_t eraseW = max(newX + newW, _lastTagX + _lastTagW) - eraseX;
    _tft->fillRect(eraseX, _lastTagY, eraseW, LINE_H - 1, TFT_BLACK);

    _lastTagX = newX;
    _lastTagW = newW;
    drawTag(newX, _lastTagY, tag);
}

void BootScreen::showConnectedDialog(const char *ipStr, int rssi, uint32_t durationMs) {
    _tft->fillScreen(TFT_NAVY);

    constexpr uint8_t bx = 10, by = 40, bw = 220, bh = 160, titleH = 20;
    drawDialogFrame(bx, by, bw, bh, titleH, "[ Connected! ]");

    const uint8_t cy = by + 1 + titleH + 4;   // top of content area

    setFont();
    _tft->setTextDatum(MC_DATUM);

    _tft->setTextColor(TFT_YELLOW, TFT_NAVY);
    _tft->drawString(ipStr, bx + bw / 2, cy + 14);

    char rssiStr[20];
    snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d dBm", rssi);
    _tft->setTextColor(TFT_CYAN, TFT_NAVY);
    _tft->drawString(rssiStr, bx + bw / 2, cy + 34);

    _tft->setTextColor(TFT_WHITE, TFT_NAVY);
    _tft->drawString("Open browser to configure", bx + bw / 2, cy + 56);

    constexpr uint8_t btnW = 70, btnH = 18;
    const uint8_t btnX = bx + (bw - btnW) / 2;
    const uint8_t btnY = by + bh - btnH - 8;
    uiDrawDosButton(_tft, btnX, btnY, btnW, btnH, "[ OK ]");

    delay(durationMs);
}

void BootScreen::showDialog(const char *title, const char *body, uint32_t durationMs) {
    _tft->fillScreen(TFT_NAVY);

    constexpr uint8_t bx = 10, by = 60, bw = 220, bh = 120, titleH = 20;
    drawDialogFrame(bx, by, bw, bh, titleH, title);

    const uint8_t cy = by + 1 + titleH + 4;

    setFont();
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_GREEN, TFT_NAVY);
    _tft->drawString(body, bx + bw / 2, cy + (bh - titleH) / 2 - 8);

    constexpr uint8_t btnW = 70, btnH = 18;
    const uint8_t btnX = bx + (bw - btnW) / 2;
    const uint8_t btnY = by + bh - btnH - 6;
    uiDrawDosButton(_tft, btnX, btnY, btnW, btnH, "[ OK ]");

    delay(durationMs);
}

void BootScreen::done() {
    _tft->fillScreen(TFT_BLACK);
    setFont();
    _animStartMs = 0;  // reset animation state
}

void BootScreen::tickLoadingAnim(uint32_t now) {
    if (!_tft) return;

    // Initialize animation on first call
    if (_animStartMs == 0) {
        _animStartMs = now;
        _tft->fillScreen(TFT_BLACK);

        // Draw prompt line once
        setFont();
        _tft->setTextColor(TFT_GREEN, TFT_BLACK);
        _tft->setCursor(3, 30);
        _tft->print("> C:\\load.exe");

        _cursorX = 3 + _tft->textWidth("> C:\\load.exe");
        _cursorOn = true;
    }

    // Blink cursor every 400ms
    uint32_t elapsed = now - _animStartMs;
    bool cursorOnNow = ((elapsed / 400) % 2) == 0;

    if (cursorOnNow != _cursorOn) {
        _cursorOn = cursorOnNow;

        // Draw or erase cursor block (█ = 0xDB in DOS encoding, but use a simple block)
        if (_cursorOn) {
            _tft->fillRect(_cursorX, 30, 8, 16, TFT_GREEN);
        } else {
            _tft->fillRect(_cursorX, 30, 8, 16, TFT_BLACK);
        }
    }
}
