#pragma once
#include <Arduino.h>
#include <WiFi.h>


struct NetConfig {
  String wifiSsid;
  String wifiPass;
};

extern NetConfig net;

// ---- WiFi mode helper ----
String wifiModeToString(wifi_mode_t m);

// Persistent config
void loadWiFiConfig();
void saveWiFiConfig();

// AP/STA control
void startAP();
bool startSTA(uint32_t timeoutMs = 15000);
void ensureWiFi();

// Helper used by web routes after saving creds
void wifiCredsUpdatedKick();
