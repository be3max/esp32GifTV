#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Retro Award BIOS / MS-DOS style boot screen renderer.
// Zero heap allocation — all state fits in member variables (~11 bytes).
class BootScreen {
public:
    enum class Tag : uint8_t { NONE = 0, OK, FAIL, WAIT };

    // Call after tft.begin(). Fills screen black, resets scroll position.
    void begin(TFT_eSPI *tft);

    // Draw the fixed BIOS header bar at the top of the screen.
    void drawHeader();

    // Append one POST-style line. Label is left-aligned; tag is right-aligned.
    // If MAX_LINES is reached, new lines overwrite from the top (wrap).
    void postLine(const char *label, Tag tag = Tag::NONE);

    // Overwrite only the tag area of the most-recently posted line.
    // Use to flip WAIT -> OK or FAIL after a blocking call resolves.
    void updateLastTag(Tag tag);

    // Show an MS-DOS QBasic-style dialog with IP and RSSI, then block for durationMs.
    void showConnectedDialog(const char *ipStr, int rssi, uint32_t durationMs);

    // Show a general-purpose MS-DOS dialog with a title and one body line.
    void showDialog(const char *title, const char *body, uint32_t durationMs);

    // Clear display to black. Call at end of boot to hand off to main loop.
    void done();

    // Draw console-style loading animation (> C:\load.exe with blinking cursor).
    // Call in loop() while GIF is loading. Automatically stops once animation is dismissed.
    void tickLoadingAnim(uint32_t now);

private:
    TFT_eSPI  *_tft       = nullptr;
    uint8_t    _scrollRow = 0;   // next POST line index (0-based)
    uint16_t   _lastTagX  = 0;   // pixel x of most-recently drawn tag
    uint16_t   _lastTagY  = 0;   // pixel y of most-recently drawn tag

    uint16_t _lastTagW = 0;    // pixel width of most-recently drawn tag

    // Loading animation state
    uint32_t   _animStartMs = 0;  // millis() when animation started (0 = not started)
    uint16_t   _cursorX     = 0;  // x position of blinking cursor
    bool       _cursorOn    = true;

    void drawTag(uint16_t x, uint16_t y, Tag tag);
    void setFont();             // always call before drawing text

    // Draws the shared MS-DOS dialog chrome: background fill, double border,
    // dark-grey title bar with centred title text, separator line, blue content area.
    // Content area begins at pixel y = by + titleH + 2; caller draws on top of it.
    void drawDialogFrame(uint8_t bx, uint8_t by, uint8_t bw, uint8_t bh,
                         uint8_t titleH, const char *title);

    static constexpr uint8_t  HEADER_H   = 52;   // header bar height in px
    static constexpr uint8_t  SCROLL_TOP = 55;   // y of first POST line
    static constexpr uint8_t  LINE_H     = 17;   // px per POST line (font2 16px + 1px gap)
    static constexpr uint8_t  MAX_LINES  = 11;   // lines before wrapping
    static constexpr uint8_t  MARGIN_X   = 3;
    static constexpr uint8_t  POST_LINE_DELAY_MS = 35;  // typewriter pacing per POST line
};

extern BootScreen bootScreen;
