#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

class OTAManager {
public:
    void begin(TFT_eSPI *tft);
    void tick(uint32_t now);   // call every loop; fires 15-min auto-check
    void checkNow();           // force immediate check (from menu or boot)

private:
    bool fetchLatestVersion(char *outTag, size_t tagLen);
    void performUpdate(const char *tag);
    void drawFrame();
    void drawProgress(const char *status, int pct, int written, int total);
    void drawResult(bool ok, const char *msg);

    TFT_eSPI   *_tft         = nullptr;
    uint32_t    _lastCheckMs = 0;

    static constexpr uint32_t CHECK_INTERVAL_MS = 15UL * 60UL * 1000UL;

    // Layout constants (240×240 display)
    static constexpr int BAR_X = 16;
    static constexpr int BAR_Y = 98;
    static constexpr int BAR_W = 208;
    static constexpr int BAR_H = 20;
};

extern OTAManager otaManager;
