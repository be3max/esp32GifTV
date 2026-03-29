#include "boot_screen.h"

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
        case BootScreen::Tag::OK:   return TFT_GREEN;
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
    // Dark content fill
    _tft->fillRect(bx + 1, by + 1, bw - 2, bh - 2, COLOR_DIALOG_BG);
    // Outer white border + inner light-grey inset (double-line effect)
    _tft->drawRect(bx,     by,     bw,     bh,     TFT_WHITE);
    _tft->drawRect(bx + 2, by + 2, bw - 4, bh - 4, TFT_LIGHTGREY);
    // Title bar
    _tft->fillRect(bx + 1, by + 1, bw - 2, titleH, TFT_DARKGREY);
    _tft->drawFastHLine(bx + 1, by + 1 + titleH, bw - 2, TFT_WHITE);
    // Title text centred in title bar
    setFont();
    _tft->setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString(title, bx + bw / 2, by + 1 + titleH / 2);
    // Blue content area
    uint8_t contentY = by + 1 + titleH + 1;
    _tft->fillRect(bx + 1, contentY, bw - 2, bh - titleH - 3, TFT_BLUE);
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

void BootScreen::drawHeader() {
    _tft->fillRect(0, 0, TFT_WIDTH, HEADER_H, TFT_NAVY);

    setFont();
    _tft->setTextColor(TFT_YELLOW, TFT_NAVY);

    // Left title
    _tft->setCursor(MARGIN_X, 2);
    _tft->print("GifScreen BIOS v1.0");

    // Right label — measured dynamically to stay right-aligned with any font
    const char *rightLabel = "Award 2026";
    _tft->setCursor(TFT_WIDTH - _tft->textWidth(rightLabel) - MARGIN_X, 2);
    _tft->print(rightLabel);

    // Separator
    _tft->drawFastHLine(0, HEADER_H,     TFT_WIDTH, TFT_CYAN);
    _tft->drawFastHLine(0, HEADER_H + 1, TFT_WIDTH, TFT_DARKGREY);
}

void BootScreen::postLine(const char *label, Tag tag) {
    const uint16_t y = SCROLL_TOP + (_scrollRow % MAX_LINES) * LINE_H;

    _tft->fillRect(0, y, TFT_WIDTH, LINE_H - 1, TFT_BLACK);

    setFont();
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
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
    _tft->fillScreen(TFT_BLUE);

    constexpr uint8_t bx = 10, by = 40, bw = 220, bh = 160, titleH = 20;
    drawDialogFrame(bx, by, bw, bh, titleH, "[ Connected! ]");

    const uint8_t cy = by + 1 + titleH + 4;   // top of content area

    setFont();
    _tft->setTextDatum(MC_DATUM);

    _tft->setTextColor(TFT_YELLOW, TFT_BLUE);
    _tft->drawString(ipStr, bx + bw / 2, cy + 14);

    char rssiStr[20];
    snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d dBm", rssi);
    _tft->setTextColor(TFT_CYAN, TFT_BLUE);
    _tft->drawString(rssiStr, bx + bw / 2, cy + 34);

    _tft->setTextColor(TFT_WHITE, TFT_BLUE);
    _tft->drawString("Open browser to configure", bx + bw / 2, cy + 56);

    constexpr uint8_t btnW = 70, btnH = 18;
    const uint8_t btnX = bx + (bw - btnW) / 2;
    const uint8_t btnY = by + bh - btnH - 8;
    _tft->fillRect(btnX, btnY, btnW, btnH, TFT_LIGHTGREY);
    _tft->drawRect(btnX, btnY, btnW, btnH, TFT_WHITE);
    _tft->setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    _tft->drawString("[ OK ]", btnX + btnW / 2, btnY + btnH / 2);

    delay(durationMs);
}

void BootScreen::showDialog(const char *title, const char *body, uint32_t durationMs) {
    _tft->fillScreen(TFT_BLUE);

    constexpr uint8_t bx = 10, by = 60, bw = 220, bh = 120, titleH = 20;
    drawDialogFrame(bx, by, bw, bh, titleH, title);

    const uint8_t cy = by + 1 + titleH + 4;

    setFont();
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_GREEN, TFT_BLUE);
    _tft->drawString(body, bx + bw / 2, cy + (bh - titleH) / 2 - 8);

    constexpr uint8_t btnW = 70, btnH = 18;
    const uint8_t btnX = bx + (bw - btnW) / 2;
    const uint8_t btnY = by + bh - btnH - 6;
    _tft->fillRect(btnX, btnY, btnW, btnH, TFT_LIGHTGREY);
    _tft->drawRect(btnX, btnY, btnW, btnH, TFT_WHITE);
    _tft->setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    _tft->drawString("[ OK ]", btnX + btnW / 2, btnY + btnH / 2);

    delay(durationMs);
}

void BootScreen::done() {
    _tft->fillScreen(TFT_BLACK);
    setFont();
}
