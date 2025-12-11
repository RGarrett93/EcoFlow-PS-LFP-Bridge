#include <Arduino.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>

#include "config.h"
#include "time_ntp.h"
#include "wi-fi.h"
#include "mqtt.h"
#include "bms.h"
#include "can.h"
#include "ecoflow.h"
#include "web.h"
#include "ota.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define SERIALDEBUG 0
#define CANDUMP 1
#define VERBOSE_BMS_PRINTS 0

// -----------------------------------------------------------------------------
// Global
// -----------------------------------------------------------------------------
AsyncWebServer server(80);
Preferences prefs;

// Used by web/API state reporting
bool canHealth = false;
String canLog;

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  // --- Hardware init ---
  pinMode(ME2107_EN, OUTPUT);
  digitalWrite(ME2107_EN, HIGH);

  pinMode(CAN_SPEED_MODE, OUTPUT);
  digitalWrite(CAN_SPEED_MODE, LOW);

  // --- Core services ---
  mqttInit(deviceId());
  loadCoreConfig();
  bmsInit();

  // --- File system ---
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  }

  // --- WiFi (persistent + AP fallback) ---
  loadWiFiConfig();

  const bool ok = startSTA();
  if (!ok) {
    Serial.println("No saved WiFi or connect failed → starting AP");
    startAP();
  }

  // --- CAN ---
  canInitDriver();
  if (twai_ok) {
    canStartTasks();
  } else {
    Serial.println("TWAI not ready; CAN tasks not started. Visit /can_try_init to retry later.");
  }

  // --- Time ---
  if (!initNTP()) {
    Serial.println("Time not synced (AP/offline mode). Web UI will still run.");
  }

  // --- Web / OTA ---
  webInit(server);
  setupServerRoutes(server);
  otaInit(server);
  webSetupStaticRoutes(server);

  // --- Start server ---
  server.begin();

  // --- EcoFlow TX sequencer/messages ---
  ecoflowMessagesInit();
}

// -----------------------------------------------------------------------------
// Loop
// -----------------------------------------------------------------------------
void loop() {
  otaHandle();
  ensureWiFi();
  mqttLoopTick();

  applyBatteryMasterIfChanged();
  bmsLoopTick();

  webTick();
  canTxSequencerTick();
}
