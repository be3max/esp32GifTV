#include "ota_manager.h"
#include "ui_theme.h"
#include "boot_screen.h"
#include "gif_player.h"   // free GIF heap before TLS to GitHub

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>

static const char OTA_REPO[]       = "be3max/esp32GifTV";
static const char OTA_ASSET_NAME[] = "firmware.bin";

// Touch pad — same capacitive pad/threshold as main.cpp (PIN_TOUCH / TOUCH_THRESHOLD).
// Duplicated here so the blocking confirm screen can read the button standalone.
static const uint8_t OTA_PIN_TOUCH    = 32;
static const uint8_t OTA_TOUCH_THRESH = 95;

OTAManager otaManager;

// ── Semver comparison ─────────────────────────────────────────────────────────

static bool isNewerVersion(const char *current, const char *candidate) {
    const char *c = (*current   == 'v') ? current   + 1 : current;
    const char *l = (*candidate == 'v') ? candidate + 1 : candidate;
    int cmaj = 0, cmin = 0, cpatch = 0;
    int lmaj = 0, lmin = 0, lpatch = 0;
    sscanf(c, "%d.%d.%d", &cmaj, &cmin, &cpatch);
    sscanf(l, "%d.%d.%d", &lmaj, &lmin, &lpatch);
    if (lmaj != cmaj) return lmaj > cmaj;
    if (lmin != cmin) return lmin > cmin;
    return lpatch > cpatch;
}

// ── Init ──────────────────────────────────────────────────────────────────────

void OTAManager::begin(TFT_eSPI *tft) {
    _tft = tft;
    _lastCheckMs = millis();  // skip immediate check on boot
    Serial.println("[OTA] manager ready, version=" FIRMWARE_VERSION_STR);
}

// ── Periodic tick ─────────────────────────────────────────────────────────────

void OTAManager::tick(uint32_t now) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (now - _lastCheckMs < CHECK_INTERVAL_MS) return;
    _lastCheckMs = now;
    checkNow(false);  // hands-free: install immediately, no confirm prompt
}

// ── Version check + update entry point ───────────────────────────────────────

OTAManager::Result OTAManager::checkNow(bool interactive) {
    if (WiFi.status() != WL_CONNECTED) {
        bootScreen.showDialog("[ OTA Update ]", "No WiFi connection", 3000);
        return Result::FAILED;
    }

    // Free the GIF buffers/canvas (~100 KB+) so the TLS handshake to GitHub has a
    // large contiguous heap block. Without this, fetch fails with "Version check
    // failed" once GIF playback has fragmented the heap.
    gifPlayer.stop();
    Serial.printf("[OTA] free heap before check=%u (largest block=%u)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    _tft->fillScreen(TFT_BLACK);
    drawFrame();
    drawProgress("Checking GitHub...", 0, 0, 0);

    char latestTag[32] = {0};
    if (!fetchLatestVersion(latestTag, sizeof(latestTag))) {
        drawResult(false, "Version check failed");
        delay(4000);
        return Result::FAILED;
    }

    Serial.printf("[OTA] current=%s  latest=%s\n", FIRMWARE_VERSION_STR, latestTag);

    if (!isNewerVersion(FIRMWARE_VERSION_STR, latestTag)) {
        // Already on latest — show success, dismiss after 3 s
        _tft->fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 0x03E0); // dark green fill
        _tft->fillRect(8, 78, 224, 16, UI_NAVY);
        _tft->setTextDatum(TC_DATUM);
        _tft->setTextColor(TFT_GREEN, UI_NAVY);
        _tft->drawString("Firmware up to date", 120, 78, 2);
        _tft->fillRect(8, 124, 224, 16, UI_NAVY);
        _tft->setTextColor(UI_AMBER, UI_NAVY);
        _tft->drawString(FIRMWARE_VERSION_STR, 120, 124, 2);
        delay(3000);
        return Result::UP_TO_DATE;
    }

    // Newer firmware found.
    if (interactive) {
        // Manual check — let the user choose Back or Install.
        int choice = waitForChoice(FIRMWARE_VERSION_STR, latestTag);
        if (choice != 1) return Result::BACK;   // Back or timeout
    }

    // Show version transition then download
    char verLine[40];
    snprintf(verLine, sizeof(verLine), "%s -> %s",
             FIRMWARE_VERSION_STR, latestTag);
    _tft->fillScreen(TFT_BLACK);
    drawFrame();
    _tft->fillRect(8, 152, 224, 16, UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(UI_CGA_DARKGRAY, UI_NAVY);
    _tft->drawString(verLine, 120, 154, 2);

    drawProgress("Preparing download...", 0, 0, 0);

    performUpdate(latestTag);
    return Result::UPDATING;  // only reached if performUpdate failed (no restart)
}

// ── Confirm screen ─────────────────────────────────────────────────────────────

void OTAManager::drawConfirm(const char *current, const char *latest, uint8_t sel) {
    if (!_tft) return;

    _tft->fillScreen(TFT_BLACK);
    drawFrame();

    // Version lines
    char line[40];
    _tft->setTextDatum(TC_DATUM);

    // Strip a leading 'v'/'V' so build version and git tag display the same way.
    auto verText = [](const char *s) { return (s && (s[0] == 'v' || s[0] == 'V')) ? s + 1 : s; };

    snprintf(line, sizeof(line), "Installed:  v%s", verText(current));
    _tft->setTextColor(TFT_WHITE, UI_NAVY);
    _tft->drawString(line, 120, 84, 2);

    snprintf(line, sizeof(line), "Available:  v%s", verText(latest));
    _tft->setTextColor(UI_AMBER, UI_NAVY);
    _tft->drawString(line, 120, 108, 2);

    // Buttons (drawn here and repainted alone on toggle to avoid full-screen flicker)
    drawConfirmButtons(sel);

    // Footer hint
    _tft->fillRect(8, 176, 224, 22, UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(UI_CGA_DARKGRAY, UI_NAVY);
    _tft->drawString("Tap=move  Hold 1s=select", 120, 180, 2);
}

// Repaint only the two buttons — called on every selection toggle so the rest of
// the screen is left untouched (no flicker, no missing-framebuffer flash).
void OTAManager::drawConfirmButtons(uint8_t sel) {
    if (!_tft) return;
    const int16_t btnY = 140, btnH = 26;
    const int16_t backX = 24,  backW = 80;
    const int16_t instX = 136, instW = 80;
    _tft->setTextDatum(TC_DATUM);

    auto drawBtn = [&](int16_t x, int16_t w, const char *label, bool selected) {
        uint16_t bg = selected ? TFT_YELLOW : UI_NAVY;
        uint16_t fg = selected ? UI_NAVY    : TFT_LIGHTGREY;
        _tft->fillRect(x, btnY, w, btnH, bg);
        _tft->drawRect(x, btnY, w, btnH, TFT_LIGHTGREY);
        _tft->setTextColor(fg, bg);
        _tft->drawString(label, x + w / 2, btnY + btnH / 2 - 6, 2);
    };
    drawBtn(backX, backW, "Back",    sel == 0);
    drawBtn(instX, instW, "Install", sel == 1);
}

// ── Blocking choice loop (touch pad, edge + debounce) ──────────────────────────

int OTAManager::waitForChoice(const char *current, const char *latest) {
    uint8_t  sel        = 1;   // default highlight = Install
    drawConfirm(current, latest, sel);

    bool     lastRaw    = false;
    uint32_t debounceMs = millis();
    bool     pressed    = false;
    uint32_t pressStart = 0;
    bool     wasPressed = false;
    uint32_t lastTouch  = millis();

    const uint32_t HOLD_MS    = 1000;
    const uint32_t TIMEOUT_MS = 15000;

    for (;;) {
        uint32_t now = millis();
        bool raw = (touchRead(OTA_PIN_TOUCH) < OTA_TOUCH_THRESH);

        // Debounce ~20 ms
        if (raw != lastRaw) { lastRaw = raw; debounceMs = now; }
        if ((now - debounceMs) >= 20) { pressed = raw; }

        // Rising edge
        if (pressed && !wasPressed) {
            pressStart = now;
            lastTouch  = now;
        }
        if (pressed) lastTouch = now;

        // Falling edge — decide tap vs hold
        if (!pressed && wasPressed) {
            uint32_t held = now - pressStart;
            if (held >= HOLD_MS) {
                return sel;                 // hold → confirm
            } else {
                sel = (sel == 0) ? 1 : 0;   // tap → toggle
                drawConfirmButtons(sel);    // repaint buttons only — no flicker
            }
        }

        wasPressed = pressed;

        // Idle timeout → treat as Back
        if ((now - lastTouch) >= TIMEOUT_MS) return -1;

        delay(8);
        yield();
    }
}

// ── GitHub API: fetch latest release tag ─────────────────────────────────────

bool OTAManager::fetchLatestVersion(char *outTag, size_t tagLen) {
    char apiUrl[96];
    snprintf(apiUrl, sizeof(apiUrl),
             "https://api.github.com/repos/%s/releases/latest", OTA_REPO);

    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;

    // Retry transient TLS connect failures (negative codes) — even after the GIF
    // buffers are freed, the heap can stay fragmented enough that mbedTLS can't
    // grab its ~40 KB contiguous handshake block (fails with -32512 → HTTP -1).
    // A short backoff lets freed blocks coalesce. Mirrors the GIF-list fetch.
    int code = 0;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        http.begin(sec, apiUrl);
        http.setTimeout(10000);
        http.addHeader("User-Agent",  "esp32GifTV/1.0");
        http.addHeader("Accept",      "application/vnd.github+json");

        code = http.GET();
        if (code == HTTP_CODE_OK) break;

        // Negative codes are HTTPClient/TLS errors (e.g. -1 connection refused),
        // usually heap/handshake failures; positive are HTTP status (403 rate limit).
        Serial.printf("[OTA] API HTTP %d (%s) try %u maxAlloc=%u heap=%u\n",
                      code, HTTPClient::errorToString(code).c_str(),
                      attempt + 1, ESP.getMaxAllocHeap(), ESP.getFreeHeap());
        http.end();
        if (code >= 0) break;          // real HTTP error — don't retry
        delay(400);
    }
    if (code != HTTP_CODE_OK) return false;

    // Filter: only extract tag_name — saves heap on large JSON response
    JsonDocument filter;
    filter["tag_name"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[OTA] JSON error: %s\n", err.c_str());
        return false;
    }

    const char *tag = doc["tag_name"] | (const char *)nullptr;
    if (!tag || tag[0] == '\0') return false;

    strlcpy(outTag, tag, tagLen);
    return true;
}

// ── Download + flash ──────────────────────────────────────────────────────────

void OTAManager::performUpdate(const char *tag) {
    char url[192];
    snprintf(url, sizeof(url),
             "https://github.com/%s/releases/download/%s/%s",
             OTA_REPO, tag, OTA_ASSET_NAME);
    Serial.printf("[OTA] fetch: %s\n", url);

    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;
    http.begin(sec, url);
    http.setTimeout(60000);  // uint16_t max ~65 s
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "esp32GifTV/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA] download HTTP %d (%s) heap=%u\n",
                      code, HTTPClient::errorToString(code).c_str(), ESP.getFreeHeap());
        char msg[40];
        if (code < 0) snprintf(msg, sizeof(msg), "Connect failed (%d)", code);
        else          snprintf(msg, sizeof(msg), "Download HTTP %d", code);
        drawResult(false, msg);
        delay(5000);
        http.end();
        return;
    }

    int totalBytes = http.getSize();
    size_t freeSpace = ESP.getFreeSketchSpace();   // size of the inactive OTA slot
    Serial.printf("[OTA] size=%d  otaSlot=%u  heap=%u\n",
                  totalBytes, (unsigned)freeSpace, ESP.getFreeHeap());

    // Guard: a bogus/oversized Content-Length is a download problem, not a full
    // flash. Report it honestly instead of the misleading "No flash space".
    if (totalBytes > 0 && (size_t)totalBytes > freeSpace) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Bad size %d B", totalBytes);
        Serial.printf("[OTA] image (%d) exceeds OTA slot (%u)\n", totalBytes, (unsigned)freeSpace);
        drawResult(false, msg);
        delay(5000);
        http.end();
        return;
    }

    if (!Update.begin(totalBytes > 0 ? (size_t)totalBytes : UPDATE_SIZE_UNKNOWN)) {
        Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        drawResult(false, Update.errorString());
        delay(5000);
        http.end();
        return;
    }

    WiFiClient *stream  = http.getStreamPtr();
    static uint8_t buf[512];
    int written   = 0;
    int lastPct   = -1;
    uint32_t deadline = millis() + 60000UL;

    while (http.connected()
           && (totalBytes < 0 || written < totalBytes)
           && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (n > 0) {
                size_t w = Update.write(buf, (size_t)n);
                if (w != (size_t)n) {
                    drawResult(false, Update.errorString());
                    delay(5000);
                    http.end();
                    return;
                }
                written += n;
                int pct = (totalBytes > 0) ? (written * 100 / totalBytes) : 0;
                if (pct != lastPct) {
                    drawProgress("Downloading...", pct, written, totalBytes);
                    lastPct = pct;
                }
            }
        }
        yield();
    }

    http.end();

    if (written == 0 || (totalBytes > 0 && written < totalBytes)) {
        Update.abort();
        drawResult(false, "Download incomplete");
        delay(5000);
        return;
    }

    if (!Update.end(true)) {
        drawResult(false, Update.errorString());
        delay(5000);
        return;
    }

    drawResult(true, "Complete!  Restarting...");
    delay(3000);
    ESP.restart();
}

// ── UI helpers ────────────────────────────────────────────────────────────────

void OTAManager::drawFrame() {
    uiDrawDosFrame(_tft, 0, 0, 240, 240, 18, " OTA UPDATE ", UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(UI_AMBER, UI_NAVY);
    _tft->drawString("FIRMWARE UPDATE", 120, 52, 2);
}

void OTAManager::drawProgress(const char *status, int pct, int written, int total) {
    if (!_tft) return;

    // Status text
    _tft->fillRect(8, 78, 224, 16, UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(TFT_WHITE, UI_NAVY);
    _tft->drawString(status, 120, 78, 2);

    // Progress bar
    _tft->drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_LIGHTGREY);
    int fill = (pct > 0) ? ((BAR_W - 2) * pct / 100) : 0;
    if (fill > 0)
        _tft->fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, TFT_WHITE);
    if (fill < BAR_W - 2)
        _tft->fillRect(BAR_X + 1 + fill, BAR_Y + 1, BAR_W - 2 - fill, BAR_H - 2, UI_NAVY);

    // Bytes / percentage
    _tft->fillRect(8, 124, 224, 16, UI_NAVY);
    char bline[48] = "";
    if (total > 0)
        snprintf(bline, sizeof(bline), "%d%%   %d / %d B", pct, written, total);
    else if (written > 0)
        snprintf(bline, sizeof(bline), "%d B", written);
    _tft->setTextColor(UI_AMBER, UI_NAVY);
    _tft->drawString(bline, 120, 124, 2);
}

void OTAManager::drawResult(bool ok, const char *msg) {
    if (!_tft) return;

    // Fill bar solid green on success, red on failure
    uint16_t barCol = ok ? 0x03E0 : TFT_RED;  // 0x03E0 = dark green RGB565
    _tft->fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, barCol);

    // Status
    _tft->fillRect(8, 78, 224, 16, UI_NAVY);
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextColor(ok ? TFT_GREEN : TFT_RED, UI_NAVY);
    _tft->drawString(ok ? "SUCCESS" : "FAILED", 120, 78, 2);

    // Message
    _tft->fillRect(8, 124, 224, 16, UI_NAVY);
    _tft->setTextColor(ok ? TFT_GREEN : TFT_RED, UI_NAVY);
    _tft->drawString(msg, 120, 124, 2);
}
