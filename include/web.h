#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// WebSocket + ring-buffer logging service
void webInit(AsyncWebServer& server);

// Static pages + utility endpoints (SPIFFS, CAN stats, reboot, update_param, 404)
void webSetupStaticRoutes(AsyncWebServer& server);

void setupServerRoutes(AsyncWebServer &server);

// Call from loop() (replaces the old inline flush/ping/BMS push block)
void webTick();

// Logging APIs used by CAN/EcoFlow modules
void streamCanLog(const char* message);
void streamDebug(const char* message);
