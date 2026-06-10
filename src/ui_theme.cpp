#include "ui_theme.h"

void uiDrawDosFrame(TFT_eSPI *d, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t titleH, const char *title, uint16_t contentBg) {
    // Dark fill under the chrome
    d->fillRect(x + 1, y + 1, w - 2, h - 2, UI_DIALOG_BG);
    // Double-line border: white outer + light-grey inset
    d->drawRect(x,     y,     w,     h,     UI_BORDER_OUT);
    d->drawRect(x + 2, y + 2, w - 4, h - 4, UI_BORDER_IN);

    if (titleH > 0 && title != nullptr) {
        // Title bar with separator
        d->fillRect(x + 1, y + 1, w - 2, titleH, UI_TITLE_BG);
        d->drawFastHLine(x + 1, y + 1 + titleH, w - 2, UI_BORDER_OUT);
        d->setTextFont(2);
        d->setTextSize(1);
        d->setTextDatum(MC_DATUM);
        d->setTextColor(TFT_WHITE, UI_TITLE_BG);
        d->drawString(title, x + w / 2, y + 1 + titleH / 2);
        // Content area below separator
        d->fillRect(x + 1, y + 1 + titleH + 1, w - 2, h - titleH - 3, contentBg);
    } else {
        // Frameless: content fills everything inside the double border
        d->fillRect(x + 3, y + 3, w - 6, h - 6, contentBg);
    }
}

void uiDrawDosButton(TFT_eSPI *d, int16_t x, int16_t y, int16_t w, int16_t h,
                     const char *label) {
    d->fillRect(x, y, w, h, TFT_LIGHTGREY);
    d->drawRect(x, y, w, h, TFT_WHITE);
    d->setTextFont(2);
    d->setTextSize(1);
    d->setTextDatum(MC_DATUM);
    d->setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    d->drawString(label, x + w / 2, y + h / 2);
}

void uiCrtCollapse(TFT_eSPI *tft) {
    constexpr int STEPS   = 12;
    constexpr int STEP_MS = 18;
    const int half = TFT_HEIGHT / 2;

    // Bands sweep from both edges toward the centre
    for (int i = 0; i < STEPS; i++) {
        int y0 = (i * half) / STEPS;
        int y1 = ((i + 1) * half) / STEPS;
        tft->fillRect(0, y0, TFT_WIDTH, y1 - y0, TFT_BLACK);
        tft->fillRect(0, TFT_HEIGHT - y1, TFT_WIDTH, y1 - y0, TFT_BLACK);
        delay(STEP_MS);
    }

    // Bright centre line, then collapse horizontally to a dot
    tft->fillRect(0, half - 2, TFT_WIDTH, 4, TFT_WHITE);
    delay(60);
    constexpr int HSTEPS = 4;
    for (int i = 1; i <= HSTEPS; i++) {
        int inset = (i * (TFT_WIDTH / 2)) / HSTEPS;
        tft->fillRect(0, half - 2, inset, 4, TFT_BLACK);
        tft->fillRect(TFT_WIDTH - inset, half - 2, inset, 4, TFT_BLACK);
        delay(15);
    }
    // Phosphor dot blip
    tft->fillCircle(TFT_WIDTH / 2, half, 3, TFT_WHITE);
    delay(40);
    tft->fillCircle(TFT_WIDTH / 2, half, 3, TFT_BLACK);
}
