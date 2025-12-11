#include "mqtt.h"
#include "config.h"
#include "can.h"
#include "ecoflow.h"          // for getPeerSerial()

#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <bms2.h>

// Persistent config
extern Preferences prefs;
extern bool canHealth;        // from main/can layer

// ---------- MQTT + HA Discovery --------------------
struct MqttConfig {
  String host;
  uint16_t port = 1883;
  String user;
  String pass;
  String base = "ecoflow_bridge";
  bool enabled = false;
};

static MqttConfig mqttCfg;

static WiFiClient mqttNet;
static PubSubClient mqttClient(mqttNet);

static bool mqttDiscoverySent = false;
static uint32_t mqttLastConnAttempt = 0;
static uint32_t mqttLastStatePub = 0;

static const uint32_t MQTT_RECONNECT_MS = 5000;
static const uint32_t MQTT_STATE_MS     = 2000; // publish state every 2s

static String mqttDeviceId;
static String mqttDevName;

// topic helpers
static String t_state() {
  return mqttCfg.base + "/" + mqttDeviceId + "/state";
}
static String t_switch_state(const String& key) {
  return mqttCfg.base + "/" + mqttDeviceId + "/switch/" + key + "/state";
}
static String t_switch_set(const String& key) {
  return mqttCfg.base + "/" + mqttDeviceId + "/switch/" + key + "/set";
}
static String t_avail() {
  return mqttCfg.base + "/" + mqttDeviceId + "/availability";
}

static const char* onOff(bool v){ return v ? "ON" : "OFF"; }

static void mqttPublish(const String& topic, const String& payload, bool retain = false) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(topic.c_str(), payload.c_str(), retain);
}

static String haDeviceJson() {
  String d = "{";
  d += "\"identifiers\":[\"" + mqttDeviceId + "\"],";
  d += "\"name\":\"" + mqttDevName + "\",";
  d += "\"manufacturer\":\"RGarrett93\",";
  d += "\"model\":\"EcoFlow PowerStream CAN/BMS LFP Bridge\",";
  d += "\"sw_version\":\"";
  d += String(FW_VERSION);
  d += "\"";
  d += "}";
  return d;
}

static void haPublishSensor(const String& objId,
                            const String& name,
                            const String& unit,
                            const String& devClass,
                            const String& stateClass,
                            const String& valueTmpl) {
  String topic = "homeassistant/sensor/" + mqttDeviceId + "_" + objId + "/config";

  String payload = "{";
  payload += "\"name\":\"" + name + "\",";
  payload += "\"uniq_id\":\"" + mqttDeviceId + "_" + objId + "\",";
  payload += "\"stat_t\":\"" + t_state() + "\",";
  payload += "\"avty_t\":\"" + t_avail() + "\",";
  payload += "\"pl_avail\":\"online\",";
  payload += "\"pl_not_avail\":\"offline\",";
  payload += "\"val_tpl\":\"" + valueTmpl + "\",";
  if (unit.length())       payload += "\"unit_of_meas\":\"" + unit + "\",";
  if (devClass.length())   payload += "\"dev_cla\":\"" + devClass + "\",";
  if (stateClass.length()) payload += "\"stat_cla\":\"" + stateClass + "\",";
  payload += "\"dev\":" + haDeviceJson();
  payload += "}";

  mqttPublish(topic, payload, true);
}

static void haPublishSwitch(const String& key, const String& friendlyName) {
  String topic = "homeassistant/switch/" + mqttDeviceId + "_" + key + "/config";

  String payload = "{";
  payload += "\"name\":\"" + friendlyName + "\",";
  payload += "\"uniq_id\":\"" + mqttDeviceId + "_" + key + "\",";
  payload += "\"cmd_t\":\"" + t_switch_set(key) + "\",";
  payload += "\"stat_t\":\"" + t_switch_state(key) + "\",";
  payload += "\"avty_t\":\"" + t_avail() + "\",";
  payload += "\"pl_avail\":\"online\",";
  payload += "\"pl_not_avail\":\"offline\",";
  payload += "\"pl_on\":\"ON\",";
  payload += "\"pl_off\":\"OFF\",";
  payload += "\"dev\":" + haDeviceJson();
  payload += "}";

  mqttPublish(topic, payload, true);
}

static void mqttPublishDiscovery() {
  if (!mqttCfg.enabled) return;
  if (!mqttClient.connected()) return;

  // BMS metrics
  haPublishSensor("soc",         "BMS SoC",          "%",  "battery",    "measurement", "{{ value_json.soc }}");
  haPublishSensor("voltage",     "BMS Voltage",      "V",  "voltage",    "measurement", "{{ value_json.voltage }}");
  haPublishSensor("current",     "BMS Current",      "A",  "current",    "measurement", "{{ value_json.current }}");
  haPublishSensor("temp",        "BMS Temperature",  "°C", "temperature","measurement", "{{ value_json.temperature }}");
  haPublishSensor("chg_runtime", "Charge Runtime",   "min","",           "measurement", "{{ value_json.chgruntime }}");
  haPublishSensor("dis_runtime", "Discharge Runtime","min","",           "measurement", "{{ value_json.disruntime }}");

  // NEW: PS info from state JSON
  haPublishSensor("ps_serial", "PS Serial Number", "", "", "",
                  "{{ value_json.ps_serial_number }}");
  haPublishSensor("ps_online", "PS Online", "", "", "",
                  "{{ value_json.ps_online }}");

  // Switches
  haPublishSwitch("battery_master", "Battery Master Switch");
  haPublishSwitch("mos_chg",        "MOSFET Charge");
  haPublishSwitch("mos_dis",        "MOSFET Discharge");
  haPublishSwitch("can_tx",         "CAN TX Enabled");
  haPublishSwitch("can_rx",         "CAN RX Enabled");

  Serial.printf("[MQTT] Publishing discovery...\n");
  mqttDiscoverySent = true;
}

static void mqttPublishStates() {
  if (!mqttCfg.enabled) return;
  if (!mqttClient.connected()) return;

  const int   soc     = (int)bms.get_state_of_charge();
  const float voltage = bms.get_voltage();
  const float current = bms.get_current();
  const int   temp    = (int)bms.get_ntc_temperature(0);
  const int   chg     = (int)config.chgruntime;
  const int   dis     = (int)config.disruntime;

  String json = "{";
  json += "\"soc\":"          + String(soc)          + ",";
  json += "\"voltage\":"      + String(voltage, 3)   + ",";
  json += "\"current\":"      + String(current, 3)   + ",";
  json += "\"temperature\":"  + String(temp)         + ",";
  json += "\"chgruntime\":"   + String(chg)          + ",";
  json += "\"disruntime\":"   + String(dis)          + ",";
  json += "\"batteryMaster\":" + String(config.batteryMaster ? "true" : "false") + ",";
  json += "\"chgMOSFET\":"    + String(config.moschg ? "true" : "false") + ",";
  json += "\"disMOSFET\":"    + String(config.mosdis ? "true" : "false") + ",";
  json += "\"canTxEnabled\":" + String(config.canTxEnabled ? "true" : "false") + ",";
  json += "\"canRxEnabled\":" + String(config.canRxEnabled ? "true" : "false") + ",";

  // PS hardware serial number (EcoFlow PowerStream)
  const char* ps = getPeerSerial();
  if (ps && ps[0] != '\0') {
    json += "\"ps_serial_number\":\"";
    json += ps;
    json += "\",";
  }

  // PS online state derived from CAN health
  json += "\"ps_online\":\"";
  json += (canHealth ? "connected" : "disconnected");
  json += "\"";

  json += "}";

  mqttPublish(t_state(), json, false);

  mqttPublish(t_switch_state("battery_master"), onOff(config.batteryMaster),   true);
  mqttPublish(t_switch_state("mos_chg"),        onOff(config.moschg),          true);
  mqttPublish(t_switch_state("mos_dis"),        onOff(config.mosdis),          true);
  mqttPublish(t_switch_state("can_tx"),         onOff(config.canTxEnabled),    true);
  mqttPublish(t_switch_state("can_rx"),         onOff(config.canRxEnabled),    true);

  mqttPublish(t_avail(), "online", true);
}

static void mqttOnMessage(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String p;
  p.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  p.trim();

  auto isOn  = [&](const String& s){ return s == "ON"  || s == "on"  || s == "1" || s == "true"; };
  auto isOff = [&](const String& s){ return s == "OFF" || s == "off" || s == "0" || s == "false"; };

  if (t == t_switch_set("battery_master")) {
    if (isOn(p))  config.batteryMaster = true;
    if (isOff(p)) config.batteryMaster = false;
    return;
  }

  if (t == t_switch_set("mos_chg")) {
    if (isOn(p))  config.moschg = true;
    if (isOff(p)) config.moschg = false;
    return;
  }

  if (t == t_switch_set("mos_dis")) {
    if (isOn(p))  config.mosdis = true;
    if (isOff(p)) config.mosdis = false;
    return;
  }

  if (t == t_switch_set("can_tx")) {
    if (isOn(p))  config.canTxEnabled = true;
    if (isOff(p)) config.canTxEnabled = false;
    return;
  }

  if (t == t_switch_set("can_rx")) {
    if (isOn(p))  config.canRxEnabled = true;
    if (isOff(p)) config.canRxEnabled = false;
    return;
  }
}

static void mqttSubscribeTopics() {
  mqttClient.subscribe(t_switch_set("battery_master").c_str());
  mqttClient.subscribe(t_switch_set("mos_chg").c_str());
  mqttClient.subscribe(t_switch_set("mos_dis").c_str());
  mqttClient.subscribe(t_switch_set("can_tx").c_str());
  mqttClient.subscribe(t_switch_set("can_rx").c_str());
}

void mqttDisconnectClean() {
  if (mqttClient.connected()) {
    mqttPublish(t_avail(), "offline", true);
    mqttClient.disconnect();
  }
}

void loadMqttConfig() {
  prefs.begin("mqtt", true);
  mqttCfg.host    = prefs.getString("host", "");
  mqttCfg.port    = prefs.getUShort("port", 1883);
  mqttCfg.user    = prefs.getString("user", "");
  mqttCfg.pass    = prefs.getString("pass", "");
  mqttCfg.base    = prefs.getString("base", "ecoflow_bridge");
  mqttCfg.enabled = prefs.getBool("enabled", false);
  prefs.end();

  if (mqttCfg.base.length() == 0) mqttCfg.base = "ecoflow_bridge";
}

static void mqttEnsureConnected() {
  if (!mqttCfg.enabled) {
    mqttDisconnectClean();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttCfg.host.length() == 0) return;

  if (mqttClient.connected()) return;

  uint32_t now = millis();
  if (now - mqttLastConnAttempt < MQTT_RECONNECT_MS) return;
  mqttLastConnAttempt = now;

  mqttClient.setServer(mqttCfg.host.c_str(), mqttCfg.port);
  mqttClient.setCallback(mqttOnMessage);

  String cid = mqttDeviceId + "_bridge";

  bool ok;
  if (mqttCfg.user.length()) {
    ok = mqttClient.connect(cid.c_str(), mqttCfg.user.c_str(), mqttCfg.pass.c_str(),
                            t_avail().c_str(), 1, true, "offline");
  } else {
    ok = mqttClient.connect(cid.c_str(), t_avail().c_str(), 1, true, "offline");
  }

  if (ok) {
    mqttDiscoverySent = false;
    mqttSubscribeTopics();
    mqttPublish(t_avail(), "online", true);
    mqttPublishDiscovery();
    mqttPublishStates();
    Serial.println("[MQTT] Connected");
  } else {
    Serial.printf("[MQTT] Connect failed rc=%d\n", mqttClient.state());
  }
}

void mqttMarkDiscoveryDirty() {
  mqttDiscoverySent = false;
  mqttLastConnAttempt = 0;
}

void mqttInit(const String& devId) {
  mqttDeviceId = devId;
  mqttDevName  = "EcoFlow Bridge " + mqttDeviceId;

  loadMqttConfig();

  mqttClient.setBufferSize(1024);
  mqttDiscoverySent     = false;
  mqttLastConnAttempt   = 0;
  mqttLastStatePub      = 0;
}

void mqttLoopTick() {
  mqttEnsureConnected();

  if (mqttClient.connected()) {
    mqttClient.loop();

    uint32_t nowMs = millis();
    if (nowMs - mqttLastStatePub >= MQTT_STATE_MS) {
      mqttLastStatePub = nowMs;
      mqttPublishStates();
      if (!mqttDiscoverySent) mqttPublishDiscovery();
    }
  }
}
