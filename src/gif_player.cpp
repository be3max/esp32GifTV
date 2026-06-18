#include "gif_player.h"
#include "config_manager.h"
#include "status_bar.h"
#include "ui_theme.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ctime>

GifPlayer  gifPlayer;
TFT_eSPI  *GifPlayer::_tftPtr    = nullptr;
uint16_t   GifPlayer::_lineBuf[240] = {0};
uint8_t   *GifPlayer::_frameBuf   = nullptr;
int        GifPlayer::_prevFrameX  = 0;
int        GifPlayer::_prevFrameY  = 0;
int        GifPlayer::_prevFrameW  = 0;
int        GifPlayer::_prevFrameH  = 0;
uint8_t    GifPlayer::_prevDisposal = 0;

// Canvas dimensions for scaling calculations (set when GIF is opened)
static int s_canvasW = 0;
static int s_canvasH = 0;

// GIF render clip height (224 = status bar, 240 = full screen)
static int s_gifClipH = STATUS_BAR_Y;

// Previous line for vertical smoothing (store RGB colors, not indices)
static uint16_t s_prevLineRGB[240] = {0};
static bool s_hasPrevLine = false;

// ── Block dissolve transition state ────────────────────────────────────────
// 8×8 pixel blocks: 30 columns × 30 rows = 900 blocks total.
// Blocks are shuffled randomly then revealed with ease-in quadratic timing —
// a few random squares appear first, then more and more blocks flood in faster.
static constexpr uint16_t DISSOLVE_BLOCKS     = 900;    // 30×30
static constexpr uint8_t  DISSOLVE_BLOCK_SIZE = 8;      // pixels per block edge
static constexpr uint8_t  DISSOLVE_COLS       = 30;     // TFT_WIDTH  / DISSOLVE_BLOCK_SIZE
static constexpr uint8_t  DISSOLVE_ROWS       = 30;     // TFT_HEIGHT / DISSOLVE_BLOCK_SIZE
static constexpr uint32_t DISSOLVE_DURATION_MS = 2000;  // total transition time (ms)

static uint16_t s_blockOrder[DISSOLVE_BLOCKS];          // shuffled block indices (1800 B)
static uint8_t  s_blockRevealed[113] = {0};             // bitmask: 1 = block revealed (113 B)
static bool     s_inTransition    = false;              // transition currently active
static uint16_t s_transitionStep  = 0;                  // blocks revealed so far
static uint32_t s_transitionStart = 0;                  // millis() when transition began
static bool     s_crtEnabled      = false;              // cached CRT setting for callback

static inline void blockSetRevealed(uint16_t idx) {
    s_blockRevealed[idx >> 3] |= (1u << (idx & 7));
}
static inline bool blockIsRevealed(uint16_t idx) {
    return (s_blockRevealed[idx >> 3] >> (idx & 7)) & 1u;
}

// GIF URL list file cache — stores downloaded URLs from a remote text file
static constexpr uint8_t LIST_BUF_SIZE = 8;   // reservoir size — 8 × 256 = 2 KB
static char    s_listUrls[LIST_BUF_SIZE][256];  // reservoir-sampled subset of remote list
static uint8_t s_listUrlCount = 0;
static uint32_t s_listFetchedMs = 0;
static const uint32_t LIST_CACHE_MS = 5UL * 60000UL;  // 5 minutes

// GIF local cache — stores downloaded GIFs to LittleFS
static const char *CACHE_META_PATH = "/cache_meta.json";
static const char *CACHE_FILE_PREFIX = "/gif_";

struct CacheMeta {
    uint32_t created_epoch;  // Unix timestamp when cache was first created
    uint8_t  count;          // number of cached files currently
    uint32_t bytes;          // total bytes in cache
};
static CacheMeta s_cacheMeta = {0, 0, 0};

// ── Session blacklist of bad URLs ──────────────────────────────────────────────
// URLs that were too big to fit flash or failed to decode are remembered (by
// hash) for the rest of the session so we never re-download them. RAM-only —
// cleared on reboot, which is fine: a transient server hiccup gets a fresh try.
static constexpr uint8_t BLACKLIST_MAX = 16;
static uint32_t s_blacklist[BLACKLIST_MAX] = {0};
static uint8_t  s_blacklistCount = 0;
static char     s_currentUrl[256] = {0};   // URL backing the current /tmp.gif

static uint32_t urlHash(const char *s) {
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) ^ (uint8_t)*s++;   // djb2
    return h;
}
static bool urlBlacklisted(const char *url) {
    if (!url || !*url) return false;
    uint32_t h = urlHash(url);
    for (uint8_t i = 0; i < s_blacklistCount; i++)
        if (s_blacklist[i] == h) return true;
    return false;
}
static void urlBlacklist(const char *url) {
    if (!url || !*url || urlBlacklisted(url)) return;
    uint32_t h = urlHash(url);
    if (s_blacklistCount < BLACKLIST_MAX)
        s_blacklist[s_blacklistCount++] = h;       // ring-evict oldest once full
    else
        s_blacklist[h % BLACKLIST_MAX] = h;
    Serial.printf("[GIF] Blacklisted %s\n", url);
}

// ── Frame buffer helpers ──────────────────────────────────────────────────────
// GIF data is streamed to LittleFS (/tmp.gif) so no large heap block is needed
// during the download phase.  After the HTTP connection closes and TLS buffers
// are freed (~90 KB reclaimed), a 48 KB frame buffer is allocated for proper
// transparency compositing via AnimatedGIF's DrawNewPixels mode.

static constexpr size_t FB_SIZE       = 66 * 1024;   // 67,584 B — safely above 256×256 (65,536)
// Hard ceiling on download size. GIFs stream to flash and play in file-callback
// mode (no full-file heap load), so the limit is LittleFS room, not RAM — well
// above the old 100 KB cap. Oversized canvases simply skip the frame buffer and
// fall back to raw stretch rendering.
static constexpr size_t MAX_GIF_BYTES = 500 * 1024;
// GIFs larger than this are played but NOT copied into the on-flash cache — a
// single fat file would otherwise crowd out many small ones (cache cap 500 KB).
static constexpr size_t CACHE_MAX_GIF_BYTES = 120 * 1024;
// Flash headroom kept free when streaming a download to /tmp.gif.
static constexpr size_t FLASH_MARGIN_BYTES  = 16 * 1024;

static const char *GIF_TMP_PATH   = "/tmp.gif";

// gifAlloc/gifFree — called by AnimatedGIF's allocFrameBuf/freeFrameBuf.
// Allocate exactly what the library requests, capped at FB_SIZE to avoid
// attempting huge allocations for oversized canvases.
static void *gifAlloc(uint32_t size) {
    if (size == 0 || size > (uint32_t)FB_SIZE) return nullptr;
    return calloc(1, size);   // calloc zeroes; AnimatedGIF expects a clean canvas
}
static void gifFree(void *p) { free(p); }

// ── LittleFS file callbacks for AnimatedGIF ───────────────────────────────────

static void *gifFileOpen(const char *path, int32_t *pSize) {
    fs::File *f = new (std::nothrow) fs::File(LittleFS.open(path, "r"));
    if (!f)  { return nullptr; }
    if (!*f) { delete f; return nullptr; }
    *pSize = (int32_t)f->size();
    return (void *)f;
}
static void gifFileClose(void *handle) {
    if (!handle) return;
    fs::File *f = (fs::File *)handle;
    f->close();
    delete f;
}
// AnimatedGIF requires read/seek callbacks to maintain pFile->iPos — the
// decoder derives every frame's start offset from it. Without this, frame 2+
// parse from the wrong position and the GIF freezes on its first frame.
static int32_t gifFileRead(GIFFILE *pFile, uint8_t *buf, int32_t len) {
    fs::File *f = (fs::File *)pFile->fHandle;
    int32_t n = (int32_t)f->read(buf, (size_t)len);
    pFile->iPos = (int32_t)f->position();
    return n;
}
static int32_t gifFileSeek(GIFFILE *pFile, int32_t pos) {
    if (pos < 0) pos = 0;
    else if (pos >= pFile->iSize) pos = pFile->iSize - 1;  // match library's seekFile clamp
    fs::File *f = (fs::File *)pFile->fHandle;
    f->seek((uint32_t)pos);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

// ── Cache helper methods ──────────────────────────────────────────────────────

bool GifPlayer::cacheMetaLoad() {
    fs::File f = LittleFS.open(CACHE_META_PATH, "r");
    if (!f) {
        s_cacheMeta = {0, 0, 0};
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        s_cacheMeta = {0, 0, 0};
        return false;
    }

    s_cacheMeta.created_epoch = doc["created_epoch"] | 0;
    s_cacheMeta.count = doc["count"] | 0;
    s_cacheMeta.bytes = doc["bytes"] | 0;
    return true;
}

void GifPlayer::cacheMetaSave() {
    fs::File f = LittleFS.open(CACHE_META_PATH, "w");
    if (!f) {
        Serial.println("[GIF] Cache meta save failed");
        return;
    }

    JsonDocument doc;
    doc["created_epoch"] = s_cacheMeta.created_epoch;
    doc["count"] = s_cacheMeta.count;
    doc["bytes"] = s_cacheMeta.bytes;
    serializeJson(doc, f);
    f.close();
}

void GifPlayer::cacheClear() {
    // Delete all cached GIF files
    for (uint8_t i = 0; i < s_cacheMeta.count; i++) {
        String path = String(CACHE_FILE_PREFIX) + String(i) + ".gif";
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
            Serial.printf("[GIF] Deleted %s\n", path.c_str());
        }
    }

    // Delete metadata file
    if (LittleFS.exists(CACHE_META_PATH)) {
        LittleFS.remove(CACHE_META_PATH);
    }

    s_cacheMeta = {0, 0, 0};
    Serial.println("[GIF] Cache cleared");
}

bool GifPlayer::cacheCopyFromTmp() {
    fs::File src = LittleFS.open(GIF_TMP_PATH, "r");
    if (!src) return false;

    uint32_t fileSize = src.size();

    // Don't cache fat GIFs — one would crowd out many small ones (and risks
    // overflowing the partition alongside the next /tmp.gif download).
    if (fileSize > CACHE_MAX_GIF_BYTES) {
        src.close();
        Serial.printf("[GIF] Not caching (too big: %u B)\n", fileSize);
        return false;
    }

    String dstPath = String(CACHE_FILE_PREFIX) + String(s_cacheMeta.count) + ".gif";

    fs::File dst = LittleFS.open(dstPath, "w");
    if (!dst) {
        src.close();
        return false;
    }

    uint8_t buf[512];
    size_t copied = 0;
    while (copied < fileSize) {
        size_t toRead = min((size_t)512, (size_t)(fileSize - copied));
        size_t n = src.read(buf, toRead);
        if (n == 0) break;
        dst.write(buf, n);
        copied += n;
    }

    src.close();
    dst.close();

    if (copied == fileSize) {
        s_cacheMeta.count++;
        s_cacheMeta.bytes += fileSize;
        cacheMetaSave();
        return true;
    } else {
        LittleFS.remove(dstPath);
        return false;
    }
}

bool GifPlayer::cacheIsExpired() {
    if (s_cacheMeta.created_epoch == 0) return false;

    time_t now = time(NULL);
    if (now == 0) return false;  // NTP not synced yet

    const DeviceConfig &cfg = configMgr.getConfig();
    uint32_t lifetime_sec = (uint32_t)cfg.gif_cache_lifetime_min * 60;
    return (now - (time_t)s_cacheMeta.created_epoch) > (time_t)lifetime_sec;
}

bool GifPlayer::cacheIsFull() {
    const DeviceConfig &cfg = configMgr.getConfig();
    uint32_t max_bytes = (uint32_t)cfg.gif_cache_max_kb * 1024;
    return s_cacheMeta.bytes >= max_bytes;
}

String GifPlayer::cacheRandomPath(int8_t excludeIdx) {
    if (s_cacheMeta.count == 0) return "";
    uint8_t idx;
    if (s_cacheMeta.count == 1 || excludeIdx < 0) {
        idx = random(0, s_cacheMeta.count);
    } else {
        do { idx = random(0, s_cacheMeta.count); } while (idx == (uint8_t)excludeIdx);
    }
    return String(CACHE_FILE_PREFIX) + String(idx) + ".gif";
}

// ── Public API ────────────────────────────────────────────────────────────────

void GifPlayer::begin(TFT_eSPI *tft) {
    _tft    = tft;
    _tftPtr = tft;
    _gif.begin(LITTLE_ENDIAN_PIXELS);
    Serial.printf("[GIF] begin (heap=%u)\n", ESP.getFreeHeap());
}

void GifPlayer::setRefreshInterval(uint16_t seconds) {
    _refreshSec = seconds;
}

void GifPlayer::setClipHeight(int h) {
    s_gifClipH = h;
}

void GifPlayer::tick() {
    uint32_t now = millis();

    // First fetch
    if (!_hasGif && _retryAfterMs == 0) {
        fetchAndPlay();
        return;
    }

    // Retry after failure
    if (_retryAfterMs != 0 && now >= _retryAfterMs) {
        _retryAfterMs = 0;
        fetchAndPlay();
        return;
    }

    // Live countdown refresh while a retry is pending. Runs regardless of
    // _playing — the skip banner / no-internet toast are shown with playback
    // stopped, so gating this behind _playing froze the countdown on screen.
    if (_retryAfterMs != 0 && now < _retryAfterMs) {
        if (_failBanner && (now - _failShownMs) >= 1000)        drawFailBanner(false);
        else if (_toastVisible && (now - _toastShownMs) >= 1000) drawToast();
    }

    // Periodic refresh
    if (_hasGif && _playing &&
        (now - _lastFetchMs >= (uint32_t)_refreshSec * 1000)) {
        fetchAndPlay();
        return;
    }

    // Advance one frame
    if (_playing) {
        static uint32_t s_frameCount = 0;  // NOLINT — intentionally persistent
        int result = _gif.playFrame(true, nullptr);

        // Advance block dissolve transition (time-based, ease-in quadratic, independent of GIF frame rate)
        if (s_inTransition) {
            uint32_t elapsed = millis() - s_transitionStart;

            // Ease-in quadratic: targetStep = DISSOLVE_BLOCKS * (t/duration)^2
            // Using fixed-point: t_frac = elapsed * 256 / duration (0..256)
            // targetStep = DISSOLVE_BLOCKS * t_frac^2 / 65536
            uint32_t t256 = min((uint32_t)256, (elapsed * 256) / DISSOLVE_DURATION_MS);
            uint16_t targetStep = (uint16_t)((uint32_t)DISSOLVE_BLOCKS * t256 * t256 >> 16);

            if (targetStep > s_transitionStep) {
                // Reveal blocks in shuffled order from s_blockOrder[s_transitionStep..targetStep)
                for (uint16_t i = s_transitionStep; i < targetStep; i++)
                    blockSetRevealed(s_blockOrder[i]);
                s_transitionStep = targetStep;
            }

            if (s_transitionStep >= DISSOLVE_BLOCKS) {
                s_inTransition = false;
                Serial.println("[GIF] Block dissolve transition complete");
            }
        }

        if (result > 0) {
            s_frameCount++;
            if (s_frameCount % 20 == 0)
                Serial.printf("[GIF] frame %u (heap=%u)\n", s_frameCount, ESP.getFreeHeap());
        } else {
            Serial.printf("[GIF] loop after %u frames\n", s_frameCount);
            s_frameCount = 0;
            _prevFrameX = _prevFrameY = _prevFrameW = _prevFrameH = 0;
            _prevDisposal = 0;

            if (_buf) {
                // In-memory mode: _gif.reset() works correctly (seeks within _buf).
                // Just clear the canvas so transparent pixels on frame 0 don't
                // composite against stale last-frame data.
                if (_frameBuf) {
                    int cw = _gif.getCanvasWidth();
                    int ch = _gif.getCanvasHeight();
                    memset(_frameBuf, 0, (size_t)cw * ch);
                }
                _gif.reset();
            } else {
                // File-callback mode: loop via explicit close + reopen. reset()
                // would also work now that the file callbacks maintain iPos, but
                // reopen keeps the frame buffer lifecycle simple.
                // Free the frame buffer via AnimatedGIF's own API first — calling
                // _gif.close() with a live frame buffer risks a double-free.
                if (_frameBuf) {
                    _gif.freeFrameBuf(gifFree);
                    _frameBuf = nullptr;
                }
                _gif.close();

                if (_gif.open(_currentGifPath, gifFileOpen, gifFileClose,
                              gifFileRead, gifFileSeek, gifDrawCallback)) {
                    int cw = _gif.getCanvasWidth();
                    int ch = _gif.getCanvasHeight();
                    s_canvasW = cw;  // Update for scaling in callback
                    s_canvasH = ch;
                    size_t canvasBytes = (size_t)cw * ch;
                    if (canvasBytes > 0 && canvasBytes <= FB_SIZE) {
                        if (_gif.allocFrameBuf(gifAlloc) == GIF_SUCCESS) {
                            _frameBuf = _gif.getFrameBuf();
                        }
                    }
                } else {
                    Serial.printf("[GIF] reopen failed, err=%d\n", _gif.getLastError());
                    _playing = false;
                    _hasGif  = false;
                }
            }
        }
    }
}

void GifPlayer::startTransition() {
    // 1. Initialize block order: 0..899
    for (uint16_t i = 0; i < DISSOLVE_BLOCKS; i++) s_blockOrder[i] = i;

    // 2. Fisher-Yates shuffle using hardware RNG
    for (uint16_t i = DISSOLVE_BLOCKS - 1; i > 0; i--) {
        uint16_t j = (uint16_t)(esp_random() % (uint32_t)(i + 1));
        uint16_t tmp   = s_blockOrder[i];
        s_blockOrder[i] = s_blockOrder[j];
        s_blockOrder[j] = tmp;
    }

    // 3. Clear bitmask — no blocks revealed yet
    memset(s_blockRevealed, 0, sizeof(s_blockRevealed));

    s_transitionStep = 0;
    s_inTransition   = true;
    s_transitionStart = millis();

    Serial.printf("[GIF] Block dissolve start (ease-in quadratic, heap=%u)\n", ESP.getFreeHeap());
}

void GifPlayer::openAndPlayFile(const char *path) {
    // Load GIF into RAM first if it fits, otherwise use file-callback mode.
    // This logic is shared between fresh downloads and cached files.
    {
        fs::File f = LittleFS.open(path, "r");
        uint32_t fileSize = f ? f.size() : 0;
        if (f) f.close();

        if (fileSize > 0) {
            _buf = (uint8_t *)malloc(fileSize);
            if (_buf) {
                fs::File gf = LittleFS.open(path, "r");
                if (gf) {
                    _bufLen = gf.read(_buf, fileSize);
                    gf.close();
                    Serial.printf("[GIF] RAM load OK %u B (heap=%u)\n",
                                  _bufLen, ESP.getFreeHeap());
                } else {
                    free(_buf); _buf = nullptr; _bufLen = 0;
                }
            } else {
                Serial.printf("[GIF] RAM load FAIL → file I/O (heap=%u maxAlloc=%u)\n",
                              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            }
        }
    }

    bool opened = (_buf && _bufLen > 0)
        ? _gif.open(_buf, _bufLen, gifDrawCallback)
        : _gif.open(path, gifFileOpen, gifFileClose,
                    gifFileRead, gifFileSeek, gifDrawCallback);

    if (opened) {
        _playing     = true;
        _hasGif      = true;
        _lastFetchMs = millis();

        // Clear any pending failure toast / skip banner
        clearToast();
        _failBanner = false;

        // Cache CRT setting for scanline callback
        s_crtEnabled = configMgr.getConfig().gif_crt_enabled;

        if (configMgr.getConfig().gif_dissolve_enabled) {
            startTransition();
        } else {
            _tft->fillScreen(TFT_BLACK);
        }
        memset(_lineBuf, 0, sizeof(_lineBuf));

        // Redraw status bar immediately — fillScreen wipes it, and the 60 s timer won't fire for a while
        {
            const DeviceConfig &sbCfg = configMgr.getConfig();
            if (sbCfg.display_mode == DISPLAY_MODE_GIF_STATUS) {
                statusBar.draw(true, sbCfg.weather_api_key[0] != '\0');
            }
        }
        _prevFrameX = _prevFrameY = _prevFrameW = _prevFrameH = 0;
        _prevDisposal = 0;

        // Allocate frame buffer sized exactly to the canvas — no over-allocation.
        int cw = _gif.getCanvasWidth();
        int ch = _gif.getCanvasHeight();
        s_canvasW = cw;
        s_canvasH = ch;
        size_t canvasBytes = (size_t)cw * ch;

        Serial.printf("[GIF] canvas %dx%d %u B (heap=%u)\n",
                      cw, ch, canvasBytes, ESP.getFreeHeap());

        if (_gif.allocFrameBuf(gifAlloc) == GIF_SUCCESS) {
            _frameBuf = _gif.getFrameBuf();
            Serial.println("[GIF] Frame buffer OK");
        } else {
            Serial.printf("[GIF] No frame buffer (heap=%u)\n", ESP.getFreeHeap());
        }

        Serial.printf("[GIF] Playing %dx%d (%s) from %s\n", cw, ch,
                      _buf ? "RAM" : "file", path);
    } else {
        if (_buf) { free(_buf); _buf = nullptr; _bufLen = 0; }
        s_inTransition = false;  // no callbacks will fire, clear stale state
        s_crtEnabled   = false;  // no callbacks will fire, clear stale state
        Serial.printf("[GIF] open failed, err=%d\n", _gif.getLastError());

        // Fresh downloads that won't decode: blacklist the URL, show the skip
        // banner, and let tick() advance after the countdown. (Cache files have
        // no URL to blacklist, so they just fall through to a plain retry.)
        if (strcmp(path, GIF_TMP_PATH) == 0 && s_currentUrl[0]) {
            flagFailedGif(String(s_currentUrl), 0, "Cannot decode");
            _retryAfterMs = millis() + 10000;
            drawFailBanner(true);
        }
    }
}

void GifPlayer::fetchAndPlay() {
    // Extract current cache index before overwriting _currentGifPath
    // (used to avoid replaying the same cached GIF)
    int8_t currentCacheIdx = -1;
    const char *cachePrefix = CACHE_FILE_PREFIX;
    if (strncmp(_currentGifPath, cachePrefix, strlen(cachePrefix)) == 0) {
        currentCacheIdx = (int8_t)atoi(_currentGifPath + strlen(cachePrefix));
    }

    stop();
    strlcpy(_currentGifPath, GIF_TMP_PATH, sizeof(_currentGifPath));

    const DeviceConfig &cfg = configMgr.getConfig();

    // ─── Cache logic ─────────────────────────────────────────────────────────
    if (cfg.gif_cache_enabled) {
        cacheMetaLoad();

        if (s_cacheMeta.count > 0 && cacheIsExpired()) {
            Serial.println("[GIF] Cache expired — clearing");
            cacheClear();
            // Fall through to download fresh GIF
        } else if (s_cacheMeta.count > 0 && cacheIsFull()) {
            // Cache full: ONLY play from cache, no more downloads
            String path = cacheRandomPath(currentCacheIdx);
            strlcpy(_currentGifPath, path.c_str(), sizeof(_currentGifPath));
            Serial.printf("[GIF] Cache full — playing %s\n", _currentGifPath);
            openAndPlayFile(_currentGifPath);
            return;
        } else if (s_cacheMeta.count > 0 && random(2) == 0) {
            // Cache filling: 50% chance — play random cached GIF instead of downloading
            String path = cacheRandomPath(currentCacheIdx);
            strlcpy(_currentGifPath, path.c_str(), sizeof(_currentGifPath));
            Serial.printf("[GIF] Cache hit (filling) — playing %s\n", _currentGifPath);
            openAndPlayFile(_currentGifPath);
            return;
        }
        // else: fall through and download a new GIF
    }

    // ─── Normal download path ────────────────────────────────────────────────
    // Reached only when no cached GIF served us above. If the link is down,
    // skip the network entirely: blocking on DNS/TLS against a dead AP freezes
    // tick() (and the toast countdown). Arm the retry and let the WiFi watchdog
    // re-associate — playback resumes automatically once internet returns.
    if (WiFi.status() != WL_CONNECTED) {
        if (playFromCacheFallback()) return;   // offline but cache available
        Serial.println("[GIF] No WiFi — retry in 10 s");
        _retryAfterMs  = millis() + 10000;
        _toastReason   = ToastReason::NO_WIFI;
        drawToast();
        return;
    }

    String gifUrl;

    // Determine which URL list to use
    const char (*urlList)[256] = cfg.gif_urls;
    uint8_t urlCount = cfg.gif_url_count;

    // If URL file mode is enabled, fetch and use that list
    if (cfg.gif_use_list_url && cfg.gif_list_url[0] != '\0') {
        // Refresh cache if empty or stale
        if (s_listUrlCount == 0 || millis() - s_listFetchedMs > LIST_CACHE_MS) {
            fetchGifList(String(cfg.gif_list_url));
        }
        if (s_listUrlCount > 0) {
            urlList = s_listUrls;
            urlCount = s_listUrlCount;
        }
    }

    // Select random URL from the active list, avoiding the last one if possible
    if (urlCount > 0) {
        uint8_t idx;
        if (urlCount > 1) {
            // Avoid repeating the same URL
            do { idx = random(0, urlCount); } while (idx == _gifIndex);
        } else {
            idx = 0;
        }
        _gifIndex = idx;
        gifUrl = urlList[idx];
        Serial.printf("[GIF] URL #%u (random, avoid repeat): %s\n", idx, gifUrl.c_str());
    } else if (cfg.gif_use_list_url && cfg.gif_list_url[0] != '\0') {
        // User configured a URL list but it failed to load (empty/HTTP error).
        // Do NOT fall back to the random cat API — that serves off-list, oversized
        // GIFs. Use cache if available, otherwise retry fetching the list.
        if (playFromCacheFallback()) return;
        Serial.println("[GIF] List unavailable — retry in 10 s");
        _retryAfterMs  = millis() + 10000;
        _toastReason   = ToastReason::SERVER;
        drawToast();
        return;
    } else {
        // No GIF list configured (and no built-in URLs). Tell the user to set one
        // in the web portal instead of pulling random off-list GIFs.
        if (playFromCacheFallback()) return;
        Serial.println("[GIF] No GIF list configured — retry in 10 s");
        _retryAfterMs  = millis() + 10000;
        _toastReason   = ToastReason::LIST_EMPTY;
        drawToast();
        return;
    }

    // Skip URLs blacklisted this session (oversized / undecodable). Try a few
    // alternates so one bad GIF in the list doesn't wedge playback on it.
    for (uint8_t attempt = 0; attempt < 8 && urlBlacklisted(gifUrl.c_str()); attempt++) {
        if (urlCount > 1) {
            uint8_t idx;
            do { idx = random(0, urlCount); } while (idx == _gifIndex);
            _gifIndex = idx;
            gifUrl = urlList[idx];
        } else {
            break;                                  // single/empty list, nothing to swap to
        }
    }
    if (urlBlacklisted(gifUrl.c_str())) {
        Serial.println("[GIF] Only blacklisted candidates — retry in 10 s");
        _retryAfterMs = millis() + 10000;
        return;
    }
    strlcpy(s_currentUrl, gifUrl.c_str(), sizeof(s_currentUrl));

    // Stream GIF to flash — HTTP + TLS operate with no large heap block held.
    if (!streamGifToFlash(gifUrl)) {
        // streamGifToFlash sets up the skip banner itself for too-large / no-space
        // failures; everything else (HTTP error, empty body) is a connection issue.
        if (!_failBanner && playFromCacheFallback()) return;  // transient fail — use cache
        Serial.println("[GIF] Download failed — retry in 10 s");
        _retryAfterMs  = millis() + 10000;
        _toastReason   = ToastReason::DOWNLOAD;
        if (_failBanner) drawFailBanner(true); else drawToast();
        return;
    }

    // HTTP is closed; TLS buffers (~90 KB) freed.
    // After successful download, cache it if enabled and not full
    if (cfg.gif_cache_enabled && !cacheIsFull()) {
        if (s_cacheMeta.count == 0) {
            s_cacheMeta.created_epoch = (uint32_t)time(NULL);  // stamp on first entry
        }
        if (cacheCopyFromTmp()) {
            Serial.printf("[GIF] Cached as gif_%u (%u KB total)\n",
                          s_cacheMeta.count - 1, s_cacheMeta.bytes / 1024);
        }
    }

    openAndPlayFile(GIF_TMP_PATH);
}

void GifPlayer::stop() {
    // Reset any in-progress transition so the new GIF gets a clean start
    s_inTransition   = false;
    s_transitionStep = 0;
    s_crtEnabled     = false;

    if (_playing) {
        if (_frameBuf) {
            _gif.freeFrameBuf(gifFree);
            _frameBuf = nullptr;
        }
        _gif.close();
        _playing = false;
    }
    if (_buf) { free(_buf); _buf = nullptr; _bufLen = 0; }
    _hasGif = false;
}

// ── Connection-problem notice (full panel, replaces the old tiny toast) ─────

static constexpr int TOAST_X = 16;
static constexpr int TOAST_Y = 49;
static constexpr int TOAST_W = 208;
static constexpr int TOAST_H = 152;
static constexpr int TOAST_TITLE_H = 20;

void GifPlayer::drawToast() {
    if (!_tft || _retryAfterMs == 0) return;

    uint32_t now      = millis();
    uint32_t secsLeft = (_retryAfterMs > now)
                        ? ((_retryAfterMs - now) + 999) / 1000
                        : 0;

    const int16_t cx = TOAST_X + TOAST_W / 2;

    // Reason-specific heading + detail line.
    const char *head, *detail;
    switch (_toastReason) {
        case ToastReason::NO_WIFI:
            head = "No WiFi";        detail = "Not joined to network"; break;
        case ToastReason::SERVER:
            head = "Server offline"; detail = "List not responding";    break;
        case ToastReason::LIST_EMPTY:
            head = "GIF list empty"; detail = "Set a list in settings";  break;
        default: /* DOWNLOAD */
            head = "Download failed"; detail = "Transfer interrupted";  break;
    }

    _tft->setTextFont(2);
    _tft->setTextSize(1);

    if (!_toastVisible) {
        // Full DOS-style dialog so the notice is readable, not lost on black.
        _tft->fillRect(TOAST_X, TOAST_Y, TOAST_W, TOAST_H, UI_NAVY);
        uiDrawDosFrame(_tft, TOAST_X, TOAST_Y, TOAST_W, TOAST_H,
                       TOAST_TITLE_H, " CONNECTION ", UI_NAVY);

        int16_t y = TOAST_Y + TOAST_TITLE_H + 8;

        // Heading row: warning glyph + reason
        _tft->setTextDatum(TC_DATUM);
        _tft->setTextColor(TFT_YELLOW, UI_NAVY);
        _tft->drawString(String("[!] ") + head, cx, y, 2);
        y += 22;

        // One-line explanation
        _tft->setTextColor(TFT_WHITE, UI_NAVY);
        _tft->drawString(detail, cx, y, 2);
        y += 22;

        // WiFi status detail
        char line[40];
        if (WiFi.status() == WL_CONNECTED) {
            snprintf(line, sizeof(line), "WiFi: on  %d dBm", WiFi.RSSI());
            _tft->setTextColor(TFT_GREEN, UI_NAVY);
        } else {
            snprintf(line, sizeof(line), "WiFi: offline");
            _tft->setTextColor(TFT_ORANGE, UI_NAVY);
        }
        _tft->drawString(line, cx, y, 2);
        y += 20;

        // Cache status — tells the user offline playback is/ isn't possible
        uint8_t  cc = getCacheCount();
        uint32_t cb = getCacheBytes();
        if (cc > 0)
            snprintf(line, sizeof(line), "Cached: %u gif / %u KB", cc, cb / 1024);
        else
            snprintf(line, sizeof(line), "Cached: none yet");
        _tft->setTextColor(cc > 0 ? TFT_CYAN : TFT_DARKGREY, UI_NAVY);
        _tft->drawString(line, cx, y, 2);
    }

    // Countdown row — only this strip repaints each second.
    int16_t cyRow = TOAST_Y + TOAST_H - 22;
    char buf[24];
    snprintf(buf, sizeof(buf), "Retry in %us", (unsigned)secsLeft);
    _tft->fillRect(TOAST_X + 4, cyRow - 2, TOAST_W - 8, 18, UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(UI_AMBER, UI_NAVY);
    _tft->drawString(buf, cx, cyRow, 2);

    _toastShownMs = now;
    _toastVisible = true;
}

void GifPlayer::clearToast() {
    if (!_tft || !_toastVisible) return;
    _tft->fillRect(TOAST_X, TOAST_Y, TOAST_W, TOAST_H, TFT_BLACK);
    _toastVisible = false;
}

// On a network failure, fall back to a cached GIF instead of stalling on the
// notice. Returns true if a cached GIF was started (caller should return).
bool GifPlayer::playFromCacheFallback() {
    const DeviceConfig &cfg = configMgr.getConfig();
    if (!cfg.gif_cache_enabled) return false;
    cacheMetaLoad();
    if (s_cacheMeta.count == 0) return false;

    String path = cacheRandomPath(-1);
    if (path.isEmpty()) return false;
    strlcpy(_currentGifPath, path.c_str(), sizeof(_currentGifPath));
    Serial.printf("[GIF] Network failed — fallback to cache %s\n", _currentGifPath);
    openAndPlayFile(_currentGifPath);
    return true;
}

// ── Oversized / undecodable GIF skip banner ─────────────────────────────────
// full=true repaints the whole notice (filename, reason, size, "skipping…");
// full=false repaints only the countdown cell, called once a second from tick()
// so the timer visibly ticks down instead of freezing.
void GifPlayer::drawFailBanner(bool full) {
    if (!_tft) return;

    uint32_t now      = millis();
    uint32_t secsLeft = (_retryAfterMs > now) ? ((_retryAfterMs - now) + 999) / 1000 : 0;
    const int cx = _tft->width() / 2;
    const int cy = _tft->height() / 2;

    if (full) {
        _tft->fillScreen(TFT_BLACK);
        _tft->setTextFont(1);
        _tft->setTextSize(1);
        _tft->setTextDatum(MC_DATUM);

        _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
        _tft->drawString(_failName, cx, cy - 45);
        _tft->drawString(_failMsg,  cx, cy - 25);

        _tft->setTextColor(TFT_WHITE, TFT_BLACK);
        if (_failBytes > 0) {
            _tft->drawString(String(_failBytes) + " B", cx, cy - 5);
            _tft->drawString("(max " + String(MAX_GIF_BYTES / 1024) + " KB)", cx, cy + 15);
        }
        _tft->setTextColor(TFT_DARKGREY, TFT_BLACK);
        _tft->drawString("skipping to next", cx, cy + 40);
    }

    // Live countdown cell — repaint just this strip each second.
    char buf[8];
    snprintf(buf, sizeof(buf), "%us", (unsigned)secsLeft);
    _tft->fillRect(cx - 30, cy + 58, 60, 16, TFT_BLACK);
    _tft->setTextFont(1);
    _tft->setTextSize(1);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_ORANGE, TFT_BLACK);
    _tft->drawString(buf, cx, cy + 65);

    _failShownMs = now;
    _failBanner  = true;
}

uint8_t GifPlayer::getCacheCount() {
    return s_cacheMeta.count;
}

uint32_t GifPlayer::getCacheBytes() {
    return s_cacheMeta.bytes;
}

void GifPlayer::clearCacheOnBoot() {
    cacheClear();
}

void GifPlayer::clearListCache() {
    s_listUrlCount  = 0;
    s_listFetchedMs = 0;
    Serial.println("[GIF] List cache cleared");
}

// ── Fetch GIF URL list from remote text file ──────────────────────────────────

bool GifPlayer::fetchGifList(const String &url) {
    // Auto-upgrade http:// to https://. GitHub/gist CDNs answer plain HTTP with a
    // 301 to HTTPS, which a non-TLS client cannot follow — the fetch would fail and
    // the list would look empty. Forcing TLS makes a pasted http:// URL just work.
    String fetchUrl = url;
    if (fetchUrl.startsWith("http://"))
        fetchUrl = "https://" + fetchUrl.substring(7);

    WiFiClientSecure sec;
    sec.setInsecure();

    HTTPClient http;
    if (fetchUrl.startsWith("https"))
        http.begin(sec, fetchUrl);
    else
        http.begin(fetchUrl);

    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[GIF] List HTTP %d  %s\n", code, fetchUrl.c_str());
        http.end();
        return false;
    }

    // Read the body directly from the stream. getString() can return empty after
    // a redirect or chunked transfer over TLS (Content-Length unknown), which made
    // a perfectly good list look empty. Read until the server closes or goes idle.
    int contentLen = http.getSize();   // -1 when chunked / unknown
    WiFiClient *stream = http.getStreamPtr();
    String body;
    body.reserve(contentLen > 0 ? (size_t)contentLen + 16 : 8192);

    uint32_t lastData = millis();
    uint32_t hardDeadline = millis() + 20000;   // absolute cap
    char rb[257];
    while (http.connected() && (contentLen < 0 || (int)body.length() < contentLen)
           && millis() < hardDeadline) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes((uint8_t *)rb, min(avail, 256));
            if (n > 0) { rb[n] = '\0'; body += rb; lastData = millis(); }
        } else {
            // Stop only after a long idle — short gaps between TLS records must not
            // truncate the body (which would leave the last URL chopped off).
            if (millis() - lastData > 8000) break;
            delay(2);
        }
        yield();
    }
    http.end();

    Serial.printf("[GIF] List HTTP 200  len=%d  read=%u bytes\n",
                  contentLen, (unsigned)body.length());

    if (body.length() == 0) {
        Serial.println("[GIF] List file is empty");
        return false;
    }

    // Reservoir sampling: one-pass random selection of LIST_BUF_SIZE URLs.
    // Uniform probability regardless of list length — no full list stored.
    s_listUrlCount = 0;
    uint32_t totalSeen = 0;
    int startPos = 0;
    while (startPos < (int)body.length()) {
        int endPos = body.indexOf('\n', startPos);
        if (endPos == -1) endPos = body.length();

        String line = body.substring(startPos, endPos);
        // Strip carriage return if present
        if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
        // Trim whitespace
        line.trim();

        // Skip empty lines and comments
        if (line.length() > 0 && line[0] != '#') {
            totalSeen++;
            if (totalSeen <= LIST_BUF_SIZE) {
                // Fill the reservoir first
                strlcpy(s_listUrls[totalSeen - 1], line.c_str(), sizeof(s_listUrls[0]));
                s_listUrlCount = (uint8_t)totalSeen;
            } else {
                // Replace a random slot with probability LIST_BUF_SIZE / totalSeen
                uint32_t j = random(0, totalSeen);
                if (j < LIST_BUF_SIZE) {
                    strlcpy(s_listUrls[j], line.c_str(), sizeof(s_listUrls[0]));
                }
            }
        }

        startPos = endPos + 1;
    }

    s_listFetchedMs = millis();
    Serial.printf("[GIF] List fetched %u URLs from remote (heap=%u)\n", s_listUrlCount, ESP.getFreeHeap());
    return s_listUrlCount > 0;
}

// ── Stream GIF download to LittleFS ──────────────────────────────────────────

// Stage the skip-banner fields for a GIF we are giving up on, blacklist it, and
// let the caller arm the retry timer + draw the banner.
void GifPlayer::flagFailedGif(const String &url, int bytes, const char *reason) {
    int lastSlash = url.lastIndexOf('/');
    String filename = (lastSlash >= 0) ? url.substring(lastSlash + 1) : url;
    if (filename.length() > 30) filename = filename.substring(0, 27) + "...";
    strlcpy(_failName, filename.c_str(), sizeof(_failName));
    strlcpy(_failMsg,  reason,           sizeof(_failMsg));
    _failBytes  = bytes;
    _failBanner = true;
    urlBlacklist(url.c_str());
}

bool GifPlayer::streamGifToFlash(const String &url) {
    _failBanner = false;   // cleared unless this call decides the GIF is unusable

    // Auto-upgrade http:// to https:// (CDNs 301 to TLS, non-TLS can't follow).
    String dlUrl = url;
    if (dlUrl.startsWith("http://"))
        dlUrl = "https://" + dlUrl.substring(7);

    // TLS connect to CDNs (giphy/github) can fail with -1 when the heap is
    // fragmented (handshake needs a ~40 KB contiguous block). It is transient, so
    // retry a couple of times before giving up.
    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;

    int code = 0;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (dlUrl.startsWith("https")) http.begin(sec, dlUrl);
        else                           http.begin(dlUrl);
        http.setTimeout(15000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        code = http.GET();
        if (code == HTTP_CODE_OK) break;

        Serial.printf("[GIF] DL HTTP %d (try %u, maxAlloc=%u)\n",
                      code, attempt + 1, ESP.getMaxAllocHeap());
        http.end();
        if (code >= 0) break;          // real HTTP error (404 etc) — don't retry
        delay(400);                    // transient connect error — back off and retry
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[GIF] DL failed %d  %s\n", code, url.c_str());
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen > (int)MAX_GIF_BYTES) {
        Serial.printf("[GIF] Too large: %d B\n", contentLen);
        http.end();
        flagFailedGif(url, contentLen, "GIF too large");
        return false;
    }

    // Guard LittleFS room — a known content length that won't fit (e.g. cache is
    // nearly full) is skipped just like an oversized GIF rather than half-writing.
    if (contentLen > 0) {
        size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
        if ((size_t)contentLen + FLASH_MARGIN_BYTES > freeB) {
            Serial.printf("[GIF] No flash room: need %d B, free %u B\n", contentLen, freeB);
            http.end();
            flagFailedGif(url, contentLen, "No flash space");
            return false;
        }
    }

    fs::File f = LittleFS.open(GIF_TMP_PATH, "w");
    if (!f) {
        Serial.println("[GIF] LittleFS open for write failed");
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t chunk[256];
    size_t got = 0;
    uint32_t deadline = millis() + 15000;

    while (http.connected() && millis() < deadline) {
        size_t avail = stream->available();
        if (avail) {
            size_t n = min(avail, sizeof(chunk));
            size_t r = stream->readBytes(chunk, n);
            f.write(chunk, r);
            got += r;
            if (contentLen > 0 && got >= (size_t)contentLen) break;
            deadline = millis() + 5000;
        } else {
            delay(1);
        }
    }

    f.close();
    http.end();

    if (got == 0) {
        LittleFS.remove(GIF_TMP_PATH);
        Serial.println("[GIF] Stream got 0 bytes");
        return false;
    }

    Serial.printf("[GIF] Streamed %u B to flash (heap=%u)\n", got, ESP.getFreeHeap());
    return true;
}

// ── Block dissolve transition helper ───────────────────────────────────────
// When transition is active, push only scanline segments belonging to revealed
// blocks; unrevealed blocks leave old TFT content intact.
static void pushScanlineWithTransition(TFT_eSPI *tft, int dy, uint16_t *scaledBuf) {
    // CRT scanline mask: replace odd rows with black to simulate phosphor gaps
    if (s_crtEnabled && (dy & 1)) {
        tft->drawFastHLine(0, dy, TFT_WIDTH, 0x0000);
        return;
    }

    if (!s_inTransition) {
        // Fast path: no transition — push entire scanline atomically
        tft->pushImage(0, dy, TFT_WIDTH, 1, scaledBuf);
        return;
    }

    // Block row for this screen y
    int blockRow = dy / DISSOLVE_BLOCK_SIZE;
    if (blockRow >= DISSOLVE_ROWS) blockRow = DISSOLVE_ROWS - 1;

    // Walk across the 30 column blocks, pushing contiguous revealed segments
    // as single pushImage calls (minimises SPI transaction overhead)
    int segStart = -1;  // start of a currently-open revealed segment, or -1 if none

    for (int col = 0; col < DISSOLVE_COLS; col++) {
        uint16_t blockIdx = (uint16_t)(blockRow * DISSOLVE_COLS + col);
        bool revealed = blockIsRevealed(blockIdx);

        if (revealed && segStart == -1) {
            // Start a new segment
            segStart = col * DISSOLVE_BLOCK_SIZE;
        } else if (!revealed && segStart != -1) {
            // End segment before this block
            int segEnd = col * DISSOLVE_BLOCK_SIZE;  // exclusive
            tft->pushImage(segStart, dy, segEnd - segStart, 1, &scaledBuf[segStart]);
            segStart = -1;
        }
    }

    // Flush final segment if it extends to the right edge
    if (segStart != -1) {
        tft->pushImage(segStart, dy, TFT_WIDTH - segStart, 1, &scaledBuf[segStart]);
    }
}

// ── AnimatedGIF draw callback — uses pushImage for atomic SPI ─────────────────

void GifPlayer::gifDrawCallback(GIFDRAW *pDraw) {
    if (!_tftPtr) return;

    int canvasW = s_canvasW;  // Set in fetchAndPlay when GIF is opened
    int canvasH = s_canvasH;

    // Universal scaling: stretch GIF to fill entire screen
    int iW = pDraw->iWidth;
    int iX = pDraw->iX;
    int y  = pDraw->iY + pDraw->y;

    uint16_t *pal = pDraw->pPalette;

    if (_frameBuf) {
        // Proportional scaling: stretch to fill entire screen (clip at status bar if needed)
        uint8_t *canvas = _frameBuf + y * canvasW;
        int scaledY = (y * TFT_HEIGHT) / canvasH;
        int nextScaledY = ((y + 1) * TFT_HEIGHT) / canvasH;

        if (scaledY < 0 || scaledY >= s_gifClipH) return;

        // Track this frame so the NEXT frame can dispose it correctly.
        if (pDraw->y == 0) {
            _prevFrameX   = pDraw->iX;
            _prevFrameY   = pDraw->iY;
            _prevFrameW   = pDraw->iWidth;
            _prevFrameH   = pDraw->iHeight;
            _prevDisposal = pDraw->ucDisposalMethod;
        }

        // Simple proportional stretching
        uint16_t scaledBuf[240];
        for (int sx = 0; sx < TFT_WIDTH; sx++) {
            int cx = (sx * canvasW) / TFT_WIDTH;
            if (cx >= canvasW) cx = canvasW - 1;
            scaledBuf[sx] = pal[canvas[cx]];
        }

        // Fill all destination rows in range [scaledY, nextScaledY) to avoid gaps
        for (int dy = scaledY; dy < nextScaledY && dy < s_gifClipH; dy++) {
            pushScanlineWithTransition(_tftPtr, dy, scaledBuf);
        }
        return;
    }

    // Simple raw stretching - no post-processing (clip at status bar if needed)
    int scaledY = (y * TFT_HEIGHT) / canvasH;
    int nextScaledY = ((y + 1) * TFT_HEIGHT) / canvasH;
    if (scaledY < 0 || scaledY >= s_gifClipH) return;

    uint8_t *px = pDraw->pPixels;
    uint16_t bgColor = pal[pDraw->ucBackground];

    uint16_t scaledBuf[240];
    for (int sx = 0; sx < TFT_WIDTH; sx++) {
        int cx = (sx * canvasW) / TFT_WIDTH;
        if (cx >= canvasW) cx = canvasW - 1;

        uint16_t color = bgColor;
        if (cx >= iX && cx < iX + iW) {
            int px_idx = cx - iX;
            if (!pDraw->ucHasTransparency || px[px_idx] != pDraw->ucTransparent) {
                color = pal[px[px_idx]];
            }
        }
        scaledBuf[sx] = color;
    }

    // Fill all destination rows in range [scaledY, nextScaledY) to avoid gaps
    for (int dy = scaledY; dy < nextScaledY && dy < s_gifClipH; dy++) {
        pushScanlineWithTransition(_tftPtr, dy, scaledBuf);
    }
}
