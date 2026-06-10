#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Shared MS-DOS retro UI theme: palette, dialog chrome, transitions.
// Stateless — zero heap. All helpers take TFT_eSPI* so they draw on the
// real display or a TFT_eSprite (which derives from TFT_eSPI).

// ── CGA / DOS palette (RGB565) ───────────────────────────────────────────────
constexpr uint16_t UI_NAVY           = TFT_NAVY;      // dialog content
constexpr uint16_t UI_DIALOG_BG      = 0x2124;        // dark navy-grey under chrome
constexpr uint16_t UI_AMBER          = 0xFD20;        // amber phosphor (clock digits)
constexpr uint16_t UI_AMBER_DIM      = 0x8A60;        // dimmed amber (blink off state)
constexpr uint16_t UI_CGA_DARKYELLOW = 0x7BE0;
constexpr uint16_t UI_CGA_DARKBLUE   = 0x000F;
constexpr uint16_t UI_CGA_DARKRED    = 0x7800;
constexpr uint16_t UI_CGA_DARKGRAY   = 0x39C7;
constexpr uint16_t UI_TITLE_BG       = TFT_DARKGREY;
constexpr uint16_t UI_BORDER_OUT     = TFT_WHITE;
constexpr uint16_t UI_BORDER_IN      = TFT_LIGHTGREY;

// Draws MS-DOS dialog chrome: content fill, double-line border (white outer,
// light-grey inset), dark-grey title bar with centred Font2 title, separator.
// titleH = 0 or title == nullptr → frameless variant (border + fill only).
// Content area begins at y + 1 + titleH + 1; caller draws on top of it.
void uiDrawDosFrame(TFT_eSPI *d, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t titleH, const char *title, uint16_t contentBg = UI_NAVY);

// Grey 3D-ish DOS button with centred Font2 label, e.g. "[ OK ]".
void uiDrawDosButton(TFT_eSPI *d, int16_t x, int16_t y, int16_t w, int16_t h,
                     const char *label);

// CRT power-off transition: black bands sweep from top and bottom edges toward
// the centre, a white horizontal line flashes, collapses to a dot, then black.
// Blocking, ~350 ms total. Screen is fully black afterwards — drop-in
// replacement for tft.fillScreen(TFT_BLACK) at mode switches.
void uiCrtCollapse(TFT_eSPI *tft);
