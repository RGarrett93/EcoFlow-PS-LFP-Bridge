#pragma once

#include <ESPAsyncWebServer.h>

// Initialises ArduinoOTA and registers all OTA web routes
void otaInit(AsyncWebServer& server);

// Call from loop()
void otaHandle();
