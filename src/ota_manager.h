#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

class OTAManager {
public:
    // Outcome of checkNow() so the caller can decide what to repaint.
    // UPDATING never actually returns — the device restarts on a successful flash.
    enum class Result { UP_TO_DATE, FAILED, BACK, UPDATING };

    void   begin(TFT_eSPI *tft);
    void   tick(uint32_t now);            // call every loop; fires 15-min auto-check
    // interactive=true shows a Back/Install confirm screen (manual menu);
    // interactive=false installs immediately (hands-free 15-min auto-check).
    Result checkNow(bool interactive = true);

private:
    bool fetchLatestVersion(char *outTag, size_t tagLen);
    void performUpdate(const char *tag);
    void drawFrame();
    void drawProgress(const char *status, int pct, int written, int total);
    void drawResult(bool ok, const char *msg);
    void drawConfirm(const char *current, const char *latest, uint8_t sel);
    int  waitForChoice(const char *current, const char *latest); // 0=Back 1=Install -1=timeout

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
