#include "config.h"

// Define the global instance
Config config;

// Returns pointer to a config boolean by key name, or nullptr if unknown
bool* getTogglePtrByKey(const String& k) {
  if (k == "canTxEnabled") return &config.canTxEnabled;
  if (k == "canRxEnabled") return &config.canRxEnabled;
  if (k == "txlogging")    return &config.txlogging;
  if (k == "rxlogging")    return &config.rxlogging;

  if (k == "batteryMaster") return &config.batteryMaster;
  if (k == "batt")         return &config.batt;

  if (k == "message3C")    return &config.message3C;
  if (k == "message13")    return &config.message13;
  if (k == "messageCB")    return &config.messageCB;
  if (k == "message70")    return &config.message70;
  if (k == "message0B")    return &config.message0B;
  if (k == "message5C")    return &config.message5C;
  if (k == "message68")    return &config.message68;
  if (k == "message4F")    return &config.message4F;
  if (k == "message24")    return &config.message24;
  if (k == "message8C")    return &config.message8C;

  if (k == "acout5C")      return &config.acout5C;
  if (k == "flagCB")       return &config.flagCB;
  if (k == "moschg")       return &config.moschg;
  if (k == "mosdis")       return &config.mosdis;
  return nullptr;
}

// ---------- Core Config Load ----------
void loadCoreConfig() {
  prefs.begin("core", true);
  String s = prefs.getString("serial", "");
  uint16_t cv = prefs.getUShort("chgvolt", 0);
  prefs.end();

  if (s.length() == 16) {
    s.toCharArray(config.serialStr, 17);
  }
  if (cv != 0) {
    config.chgvolt = cv;
  }
}

// ---------- Core Config Save ----------
void saveCoreConfig() {
  prefs.begin("core", false);
  prefs.putString("serial", String(config.serialStr));
  prefs.putUShort("chgvolt", config.chgvolt);
  prefs.end();
}

// ---------- Device ID (from MAC) ----------
String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t last16 = (uint16_t)(mac & 0xFFFF);   // lowest 16 bits

  char buf[5];                                  // 4 hex chars + NUL
  snprintf(buf, sizeof(buf), "%04X", last16);   // zero-padded, uppercase
  return String(buf);
}
