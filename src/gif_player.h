#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <AnimatedGIF.h>

class GifPlayer {
public:
    void begin(TFT_eSPI *tft);
    void fetchAndPlay();                       // fetch new GIF and start playback
    void tick();                               // call in loop(); advances frames + refresh timer
    void setRefreshInterval(uint16_t seconds);
    void setClipHeight(int h);                 // set GIF render clip height (224 for status bar, 240 for full)
    void stop();                               // stop playback (call when leaving GIF mode)
    bool isPlaying() const { return _playing; } // true once first GIF frame is rendering
    uint8_t  getCacheCount();                  // returns current number of cached GIFs
    uint32_t getCacheBytes();                  // returns total bytes in cache
    void clearCacheOnBoot();                   // clear all cached GIFs on ESP startup
    void clearListCache();                     // reset URL list so next fetch re-samples from remote file

private:
    TFT_eSPI    *_tft           = nullptr;
    AnimatedGIF  _gif;

    uint8_t     *_buf           = nullptr; // non-null when GIF is loaded into RAM
    size_t       _bufLen        = 0;

    bool         _hasGif        = false;
    bool         _playing       = false;

    uint16_t     _refreshSec    = 30;
    uint32_t     _lastFetchMs   = 0;
    uint32_t     _retryAfterMs  = 0;
    uint8_t      _gifIndex      = 0;
    char         _currentGifPath[40];        // path to currently playing GIF (/tmp.gif or /gif_N.gif)

    static TFT_eSPI  *_tftPtr;
    static uint16_t   _lineBuf[240];   // scanline buffer for palette conversion
    static uint8_t   *_frameBuf;       // 8-bit canvas managed by AnimatedGIF

    // Tracks the previous frame's bounds and disposal method so we can
    // clear areas that disposal-2 frames leave behind (DrawNewPixels only
    // clears within the NEW frame's bounding box, not the OLD frame's).
    static int     _prevFrameX;
    static int     _prevFrameY;
    static int     _prevFrameW;
    static int     _prevFrameH;
    static uint8_t _prevDisposal;

    bool    fetchRandomGifUrl(String &outUrl);
    bool    streamGifToFlash(const String &url);
    bool    fetchGifList(const String &url);  // download & parse URL list file

    bool    cacheMetaLoad();                   // load cache metadata from /cache_meta.json
    void    cacheMetaSave();                   // save cache metadata to /cache_meta.json
    void    cacheClear();                      // delete all cached GIFs and metadata
    bool    cacheCopyFromTmp();                // copy /tmp.gif to next cache slot
    bool    cacheIsExpired();                  // check if cache lifetime exceeded
    bool    cacheIsFull();                     // check if cache size limit reached
    String  cacheRandomPath(int8_t excludeIdx = -1); // return random cached GIF path (avoid repeat)

    void    openAndPlayFile(const char *path); // open GIF file and play it
    void    startTransition();                  // initialize and arm the glitch transition

    // ── Connection-problem notice (was "no-internet toast") ───────────────────
    // Reason shown on the notice so the user knows *why* playback stalled:
    //   NO_WIFI  — not associated with an access point
    //   SERVER   — WiFi up, but the GIF source/API did not respond
    //   DOWNLOAD — WiFi up, transfer started but failed/incomplete
    enum class ToastReason : uint8_t { NO_WIFI, SERVER, DOWNLOAD };
    ToastReason _toastReason  = ToastReason::NO_WIFI;
    uint32_t  _toastShownMs  = 0;              // millis() when toast was last drawn
    bool      _toastVisible  = false;          // true while retry-pending notice is shown
    void      drawToast();                     // render/refresh the notice panel (uses _toastReason)
    void      clearToast();                    // erase notice area with fillRect
    bool      playFromCacheFallback();         // on network failure, play a cached GIF if any exist

    // ── Oversized / undecodable GIF banner ────────────────────────────────────
    // Full-screen notice ("GIF too large", live countdown) shown when a GIF is
    // skipped. Plays for ~10 s, then tick() advances to the next GIF.
    bool      _failBanner    = false;          // true while the skip banner is shown
    char      _failName[32]  = {0};            // truncated filename
    char      _failMsg[24]   = {0};            // reason line
    int       _failBytes     = 0;              // reported byte size (0 = hide)
    uint32_t  _failShownMs   = 0;              // millis() when countdown was last drawn
    void      drawFailBanner(bool full);       // full=true repaints everything, else countdown only
    void      flagFailedGif(const String &url, int bytes, const char *reason); // blacklist + stage banner

    static void gifDrawCallback(GIFDRAW *pDraw);
};

extern GifPlayer gifPlayer;
