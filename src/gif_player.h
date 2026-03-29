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
    void    startTransition();                  // initialize and arm the dissolve transition

    static void gifDrawCallback(GIFDRAW *pDraw);
};

extern GifPlayer gifPlayer;
