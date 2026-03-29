#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebPortal {
public:
    void begin();   // start server on port 80
    void stop();

    // Call from main loop — handles any deferred config saves
    void update();

    bool pendingRestart = false;

private:
    AsyncWebServer _server{80};

    void setupRoutes();
    void handleGetConfig(AsyncWebServerRequest *req);
    void handlePostConfig(AsyncWebServerRequest *req, uint8_t *data, size_t len);
    void handleGetStatus(AsyncWebServerRequest *req);
};

extern WebPortal webPortal;
