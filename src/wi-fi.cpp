#include "wi-fi.h"
#include <Preferences.h>

extern Preferences prefs;
extern String deviceId();

// ---------- WiFi persistent config + AP fallback ----------
NetConfig net;

static bool apMode = false;
static uint32_t wifiLastOkMs = 0;
static uint32_t wifiNextRetryMs = 0;

// sticky AP until new creds
static bool apHold = false;

void loadWiFiConfig() {
  prefs.begin("net", true);
  net.wifiSsid = prefs.getString("ssid", "");
  net.wifiPass = prefs.getString("pass", "");
  prefs.end();
}

void saveWiFiConfig() {
  prefs.begin("net", false);
  prefs.putString("ssid", net.wifiSsid);
  prefs.putString("pass", net.wifiPass);
  prefs.end();
}

String wifiModeToString(wifi_mode_t m) {
  switch (m) {
    case WIFI_MODE_NULL:  return "NULL";
    case WIFI_MODE_STA:   return "STA";
    case WIFI_MODE_AP:    return "AP";
    case WIFI_MODE_APSTA: return "AP+STA";
    default:              return "UNKNOWN";
  }
}

void startAP() {
  // If we're already in AP or AP+STA, don't restart it.
  wifi_mode_t m = WiFi.getMode();
  if (apMode && (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA)) {
    return;
  }

  apMode = true;
  apHold = true;  // <-- sticky AP until user provides new creds

  String apSsid = "EcoFlowBridge-" + deviceId();
  String apPass = "ecoflow123";

  // Use AP-only mode for stability
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  Serial.printf("AP started: %s  IP=%s\n",
                apSsid.c_str(),
                WiFi.softAPIP().toString().c_str());

  // Back off retries hard
  wifiNextRetryMs = millis() + 60000; // 60s
}

bool startSTA(uint32_t timeoutMs) {
  if (net.wifiSsid.isEmpty()) return false;

  wifi_mode_t m = WiFi.getMode();
  bool apAlreadyUp = (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);

  WiFi.mode(apAlreadyUp ? WIFI_AP_STA : WIFI_STA);

  WiFi.begin(net.wifiSsid.c_str(), net.wifiPass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiLastOkMs = millis();
    apMode = false;   // "STA-first"
    // apHold = false;

    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  return false;
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiLastOkMs = millis();
    apHold = false;  // STA recovered
    return;
  }

  uint32_t now = millis();

  // If AP is sticky, don't try switching to STA
  if (apMode && apHold) {
    return;
  }

  // Throttle retries
  if ((int32_t)(now - wifiNextRetryMs) < 0) {
    return;
  }

  // Try STA briefly
  bool ok = startSTA(3000);

  if (!ok) {
    startAP();
    // Next retry later — not in a loop
    wifiNextRetryMs = now + 30000; // 30s backoff
  } else {
    // Connected
    wifiNextRetryMs = now + 30000;
  }
}

void wifiCredsUpdatedKick() {
  apHold = false;
  wifiNextRetryMs = 0;
}
