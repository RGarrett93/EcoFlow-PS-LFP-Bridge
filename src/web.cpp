#include "web.h"

#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>

#include "config.h"
#include "can.h"
#include "wi-fi.h"
#include "mqtt.h"
#include "bms.h"
#include "ecoflow.h"

// ----------------------------------------------------------------------------
// WebSockets
// ----------------------------------------------------------------------------
static AsyncWebSocket wsLog("/log");
static AsyncWebSocket wsBms("/bms");
static AsyncWebSocket wsDebug("/debug");

extern Preferences prefs;

extern bool canHealth;

// Escape a string for JSON inclusion
static String jsonEscape(const String &s) {
  String out; out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

void setupServerRoutes(AsyncWebServer &server) {

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* r){
    auto getS = [&](const char* n)->String{
      return r->hasParam(n, true) ? r->getParam(n, true)->value() : String();
    };

    String ssid = getS("ssid");
    String pass = getS("pass");

    if (ssid.length()) net.wifiSsid = ssid;
    net.wifiPass = pass; // allow empty

    saveWiFiConfig();
    wifiCredsUpdatedKick();

    r->send(200, "application/json", "{\"ok\":true}");

    startSTA(5000);
  });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {

    const bool staConnected = WiFi.isConnected();
    const String staIp = staConnected ? WiFi.localIP().toString() : String("-");
    const String apIp  = WiFi.softAPIP().toString();

    // Load MQTT config
    prefs.begin("mqtt", true);
    String   mHost    = prefs.getString("host", "");
    uint16_t mPort    = prefs.getUShort("port", 1883);
    String   mUser    = prefs.getString("user", "");
    String   mPass    = prefs.getString("pass", "");
    String   mBase    = prefs.getString("base", "ecoflow_bridge");
    bool     mEnabled = prefs.getBool("enabled", false);
    prefs.end();

    String json = "{";

    // Firmware / UI version
    json += "\"version\":\"" + String(FW_VERSION) + "\",";

    json += "\"deviceId\":\"" + deviceId() + "\",";

    // Core parameter values
    json += "\"volt\":" + String((unsigned)config.volt) + ",";
    json += "\"chgvolt\":" + String((unsigned)config.chgvolt) + ",";
    json += "\"temp\":" + String((unsigned)config.temp) + ",";
    json += "\"soc\":" + String((unsigned)config.soc) + ",";
    json += "\"disruntime\":" + String((unsigned)config.disruntime) + ",";
    json += "\"chgruntime\":" + String((unsigned)config.chgruntime) + ",";
    json += "\"bmsChgUp\":" + String((unsigned)config.bmsChgUp) + ",";
    json += "\"bmsChgDn\":" + String((unsigned)config.bmsChgDn) + ",";
    json += "\"serial\":\"" + jsonEscape(config.serialStr) + "\",";

    // Toggles
    json += "\"batteryMaster\":" + String(config.batteryMaster ? "true" : "false") + ",";
    json += "\"batt\":" + String(config.batt ? "true" : "false") + ",";
    json += "\"canTxEnabled\":" + String(config.canTxEnabled ? "true" : "false") + ",";
    json += "\"canRxEnabled\":" + String(config.canRxEnabled ? "true" : "false") + ",";
    json += "\"txlogging\":" + String(config.txlogging ? "true" : "false") + ",";
    json += "\"rxlogging\":" + String(config.rxlogging ? "true" : "false") + ",";

    json += "\"message3C\":" + String(config.message3C ? "true" : "false") + ",";
    json += "\"message13\":" + String(config.message13 ? "true" : "false") + ",";
    json += "\"messageCB\":" + String(config.messageCB ? "true" : "false") + ",";
    json += "\"message70\":" + String(config.message70 ? "true" : "false") + ",";
    json += "\"message0B\":" + String(config.message0B ? "true" : "false") + ",";
    json += "\"message5C\":" + String(config.message5C ? "true" : "false") + ",";
    json += "\"message68\":" + String(config.message68 ? "true" : "false") + ",";
    json += "\"message4F\":" + String(config.message4F ? "true" : "false") + ",";
    json += "\"message8C\":" + String(config.message8C ? "true" : "false") + ",";
    json += "\"message24\":" + String(config.message24 ? "true" : "false") + ",";
    json += "\"acout5C\":" + String(config.acout5C ? "true" : "false") + ",";
    json += "\"flagCB\":" + String(config.flagCB ? "true" : "false") + ",";
    json += "\"moschg\":" + String(config.moschg ? "true" : "false") + ",";
    json += "\"mosdis\":" + String(config.mosdis ? "true" : "false") + ",";

    // CAN Health
    json += "\"canHealth\":" + String(canHealth ? "true" : "false") + ",";
    // EcoFlow PowerStream Serial
    json += "\"peerSerial\":\"" + jsonEscape(String(getPeerSerial())) + "\",";
    // WiFi object
    json += "\"wifi\":{";
    json += "\"connected\":" + String(staConnected ? "true" : "false") + ",";
    json += "\"mode\":\"" + wifiModeToString(WiFi.getMode()) + "\",";
    json += "\"ip\":\"" + staIp + "\",";
    json += "\"ap_ip\":\"" + apIp + "\",";
    json += "\"ssid\":\"" + jsonEscape(net.wifiSsid) + "\",";
    json += "\"pass\":\"" + jsonEscape(net.wifiPass) + "\"";
    json += "},";

    // MQTT object
    json += "\"mqtt\":{";
    json += "\"host\":\"" + jsonEscape(mHost) + "\",";
    json += "\"port\":" + String(mPort) + ",";
    json += "\"user\":\"" + jsonEscape(mUser) + "\",";
    json += "\"pass\":\"" + jsonEscape(mPass) + "\",";
    json += "\"base\":\"" + jsonEscape(mBase) + "\",";
    json += "\"enabled\":" + String(mEnabled ? "true" : "false");
    json += "}";

    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/bms", HTTP_GET, [](AsyncWebServerRequest *request) {

    int soc = (int)  bms.get_state_of_charge();
    float v = bms.get_voltage();
    float c = bms.get_current();
    float t = bms.get_ntc_temperature(0);

    uint16_t minMv = 0, maxMv = 0;
    int n = (int) bms.get_num_cells();

    for (int i = 1; i <= n; i++) {
      float cv = bms.get_cell_voltage(i); // volts
      uint16_t mv = (uint16_t) lround(cv * 1000.0f);
      if (i == 1 || mv < minMv) minMv = mv;
      if (mv > maxMv) maxMv = mv;
    }

    String json = "{";
    json += "\"soc\":" + String(soc) + ",";
    json += "\"voltage\":" + String(v, 3) + ",";
    json += "\"current\":" + String(c, 3) + ",";
    json += "\"temperature\":" + String(t, 1) + ",";
    json += "\"min_cell_mv\":" + String(minMv) + ",";
    json += "\"max_cell_mv\":" + String(maxMv);
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/net", HTTP_GET, [](AsyncWebServerRequest *request) {

    const bool staConnected = WiFi.isConnected();
    String mode = wifiModeToString(WiFi.getMode());
    String ip   = staConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String ssid = staConnected ? WiFi.SSID() : WiFi.softAPSSID();

    String json = "{";
    json += "\"mode\":\"" + mode + "\",";
    json += "\"ip\":\"" + ip + "\",";
    json += "\"ssid\":\"" + jsonEscape(ssid) + "\"";
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/net", HTTP_POST, [](AsyncWebServerRequest *request) {

    // --- WiFi fields ---
    String ssid = request->arg("ssid");
    String pass = request->arg("pass");

    bool   wifiChanged = false;
    bool   staOk       = false;
    String staIp;   // new

    if (ssid.length()) {
      // Check if WiFi credentials actually changed
      wifiChanged = (ssid != net.wifiSsid) || (pass != net.wifiPass);

      if (wifiChanged) {
        // Update in-RAM config
        net.wifiSsid = ssid;
        net.wifiPass = pass;

        // Persist + poke WiFi state machine
        saveWiFiConfig();
        wifiCredsUpdatedKick();

        // Try STA connect with timeout (e.g. 5s)
        staOk = startSTA(5000);
        if (staOk) {
          staIp = WiFi.localIP().toString();
        }
      }
    }

    // --- MQTT fields (always saved) ---
    String mhost = request->arg("mhost");
    String mport = request->arg("mport");
    String muser = request->arg("muser");
    String mpass = request->arg("mpass");
    String mbase = request->arg("mbase");
    bool men = request->arg("men") == "on" || request->arg("men") == "true";

    prefs.begin("mqtt", false);
    prefs.putString("host", mhost);
    prefs.putUShort("port", (uint16_t)mport.toInt());
    prefs.putString("user", muser);
    prefs.putString("pass", mpass);
    prefs.putString("base", mbase);
    prefs.putBool("enabled", men);
    prefs.end();

    // Reload MQTT runtime config and mark discovery dirty
    loadMqttConfig();
    mqttMarkDiscoveryDirty();

    // JSON response so the UI knows what happened
    String json = "{";
    json += "\"ok\":true";
    json += ",\"wifi_changed\":"; json += (wifiChanged ? "true" : "false");
    if (wifiChanged) {
      json += ",\"sta_ok\":"; json += (staOk ? "true" : "false");
      if (staOk && staIp.length()) {
        json += ",\"sta_ip\":\"";
        json += staIp;
        json += "\"";
      }
    }
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/toggle", HTTP_POST, [](AsyncWebServerRequest* request) {

    String k;
    if (request->hasParam("k", true))
      k = request->getParam("k", true)->value();
    else if (request->hasParam("key", true))
      k = request->getParam("key", true)->value();

    if (!k.length()) {
      request->send(400, "application/json", "{\"ok\":false,\"err\":\"missing key\"}");
      return;
    }

    // Special handling so MOSFET UI actually triggers BMS write
    if (k == "moschg") {
      config.moschg = !config.moschg;
      lastWebMoschg = config.moschg;
      pendingMoschgChange = true;

      String resp = String("{\"ok\":true,\"k\":\"") + k + "\",\"v\":" + (config.moschg ? "true" : "false") + "}";
      request->send(200, "application/json", resp);
      return;
    }

    if (k == "mosdis") {
      config.mosdis = !config.mosdis;
      lastWebMosdis = config.mosdis;
      pendingMosdisChange = true;

      String resp = String("{\"ok\":true,\"k\":\"") + k + "\",\"v\":" + (config.mosdis ? "true" : "false") + "}";
      request->send(200, "application/json", resp);
      return;
    }

    bool* p = getTogglePtrByKey(k);
    if (!p) {
      String resp = String("{\"ok\":false,\"err\":\"unknown key\",\"k\":\"") + k + "\"}";
      request->send(404, "application/json", resp);
      return;
    }

    *p = !*p;

    String resp = String("{\"ok\":true,\"k\":\"") + k + "\",\"v\":" + (*p ? "true" : "false") + "}";
    request->send(200, "application/json", resp);
  });

}


// ---- WiFi mode helper ----
static void wsAttachHandlers(const char* name, AsyncWebSocket& ws) {
  ws.onEvent([name](AsyncWebSocket * server,
                    AsyncWebSocketClient * client,
                    AwsEventType type,
                    void * arg,
                    uint8_t * data,
                    size_t len) {
    switch (type) {
      case WS_EVT_CONNECT: {
        IPAddress ip = client->remoteIP();
        Serial.printf("[%s] #%u CONNECTED from %s\n", name, client->id(), ip.toString().c_str());
        client->text(String("[") + name + "] hello from ESP32");
        break;
      }
      case WS_EVT_DISCONNECT:
        Serial.printf("[%s] #%u DISCONNECTED\n", name, client->id());
        break;
      case WS_EVT_DATA:
        Serial.printf("[%s] #%u TEXT %u bytes\n", name, client->id(), (unsigned)len);
        break;
      case WS_EVT_PONG:
        Serial.printf("[%s] #%u PONG\n", name, client->id());
        break;
      case WS_EVT_ERROR:
        Serial.printf("[%s] #%u ERROR\n", name, client->id());
        break;
    }
  });
}

// ----------------------------------------------------------------------------
// Ring buffers (CAN + DEBUG)
// ----------------------------------------------------------------------------
static constexpr size_t WSBUF_SZ       = 8192;     // per channel
static constexpr size_t WSFLUSH_SLICE  = 1024;     // bytes per burst (tune)

struct WsRing {
  char buf[WSBUF_SZ];
  size_t head = 0, tail = 0;
  SemaphoreHandle_t mtx = nullptr;
  inline size_t used() const { return (head + WSBUF_SZ - tail) % WSBUF_SZ; }
  inline size_t free() const { return WSBUF_SZ - 1 - used(); } // keep 1 byte gap
  inline void   pushByte(char c){
    buf[head] = c;
    head = (head + 1) % WSBUF_SZ;
    if (head == tail) tail = (tail + 1) % WSBUF_SZ;
  }
  inline bool   popByte(char &out){
    if (tail == head) return false;
    out = buf[tail];
    tail = (tail + 1) % WSBUF_SZ;
    return true;
  }
};

static WsRing ringCan, ringDbg;

static void wsbuf_init(){
  ringCan.mtx = xSemaphoreCreateMutex();
  ringDbg.mtx = xSemaphoreCreateMutex();
}

// Drop exactly ONE whole line (until and including the next '\n')
static void rb_drop_one_line(WsRing &rb){
  char c;
  while (rb.tail != rb.head) {
    if (rb.popByte(c) && c == '\n') break;
  }
}

// Enqueue a full line (adds '\n'); if not enough space, drop oldest whole lines first
static void rb_enqueue_line(WsRing &rb, const char* s){
  if (!s || !*s) return;
  if (xSemaphoreTake(rb.mtx, 0) != pdTRUE) return;
  size_t need = strlen(s) + 1;
  while (rb.free() < need) rb_drop_one_line(rb);
  while (*s) rb.pushByte(*s++);
  rb.pushByte('\n');
  xSemaphoreGive(rb.mtx);
}

static void ws_flush_ring(AsyncWebSocket &ws, WsRing &rb){
  if (ws.count() == 0) return;
  static uint32_t lastCleanup = 0;
  uint32_t now = millis();
  if (now - lastCleanup > 500) { ws.cleanupClients(); lastCleanup = now; }

  if (xSemaphoreTake(rb.mtx, 0) != pdTRUE) return;
  const size_t used = rb.used();
  const size_t limit = (used < WSFLUSH_SLICE ? used : (size_t)WSFLUSH_SLICE);
  if (limit == 0) { xSemaphoreGive(rb.mtx); return; }

  size_t idx = rb.tail;
  ssize_t last_nl = -1;
  for (size_t i = 0; i < limit; ++i) {
    if (rb.buf[idx] == '\n') last_nl = (ssize_t)i;
    idx = (idx + 1) % WSBUF_SZ;
  }

  if (last_nl < 0 && used < (WSBUF_SZ - 64)) {
    xSemaphoreGive(rb.mtx);
    return;
  }

  size_t to_send = (last_nl >= 0) ? ((size_t)last_nl + 1) : limit;

  static char out[WSFLUSH_SLICE + 1];
  for (size_t i = 0; i < to_send; ++i) rb.popByte(out[i]);
  xSemaphoreGive(rb.mtx);

  if (auto *mb = ws.makeBuffer(to_send)) {
    memcpy(mb->get(), out, to_send);
    ws.textAll(mb);
  } else {
    ws.textAll(out, to_send);
  }
}

// ----------------------------------------------------------------------------
// Public logging APIs
// ----------------------------------------------------------------------------
void streamCanLog(const char* message) {
  if (wsLog.count())
    rb_enqueue_line(ringCan, message);
}

void streamDebug(const char* message) {
  if (wsDebug.count())
    rb_enqueue_line(ringDbg, message);
}

// ----------------------------------------------------------------------------
// BMS WebSocket JSON push
// ----------------------------------------------------------------------------
static void sendBMSReadings() {
  if (wsBms.count() == 0) return;

  #ifdef ASYNC_WEBSOCKET_FEATURES
  if (!wsBms.availableForWriteAll()) return;
  #endif

  const float voltage = bms.get_voltage();
  const float current = bms.get_current();
  const int   soc     = bms.get_state_of_charge();
  const int   temp    = bms.get_ntc_temperature(0);
  const int   chg     = (int)config.chgruntime;
  const int   dis     = (int)config.disruntime;

  char json[160];
  snprintf(json, sizeof(json),
    "{\"soc\":%d,\"voltage\":%.2f,\"current\":%.2f,\"temperature\":%d,\"chgruntime\":%d,\"disruntime\":%d}",
    soc, voltage, current, temp, chg, dis);

  size_t n = strlen(json);
  if (auto *mb = wsBms.makeBuffer(n)) {
    memcpy(mb->get(), json, n);
    wsBms.textAll(mb);
  } else {
    wsBms.textAll(json);
  }
}

// ----------------------------------------------------------------------------
// Init + tick
// ----------------------------------------------------------------------------
void webInit(AsyncWebServer& server) {
  wsbuf_init();

  wsAttachHandlers("LOG",   wsLog);
  wsAttachHandlers("BMS",   wsBms);
  wsAttachHandlers("DEBUG", wsDebug);

  server.addHandler(&wsLog);
  server.addHandler(&wsBms);
  server.addHandler(&wsDebug);
}

void webTick() {
  static uint32_t lastBmsPush = 0;
  static const  uint32_t BMS_PUSH_MS = 250;

  static uint32_t lastFlush = 0;
  static uint32_t lastPing  = 0;

  uint32_t now = millis();

  if (now - lastBmsPush >= BMS_PUSH_MS) {
    lastBmsPush = now;
    sendBMSReadings();
  }

  if (now - lastFlush >= 50) {
    lastFlush = now;
    ws_flush_ring(wsLog,   ringCan);
    ws_flush_ring(wsDebug, ringDbg);
  }

  if (now - lastPing >= 15000) {
    lastPing = now;
    wsLog.pingAll();
    wsBms.pingAll();
    wsDebug.pingAll();
  }
}

void webSetupStaticRoutes(AsyncWebServer& server) {

  // Serve SPIFFS pages
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request){
    if (!SPIFFS.exists("/index.html")) { request->send(404, "text/plain", "File not found"); return; }
    request->send(SPIFFS, "/index.html", "text/html");
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request){
    if (!SPIFFS.exists("/canlog_streaming.html")) { request->send(404, "text/plain", "File not found"); return; }
    request->send(SPIFFS, "/canlog_streaming.html", "text/html");
  });

  server.on("/debug", HTTP_GET, [](AsyncWebServerRequest* request){
    if (!SPIFFS.exists("/debug_streaming.html")) { request->send(404, "text/plain", "File not found"); return; }
    request->send(SPIFFS, "/debug_streaming.html", "text/html");
  });

  server.on("/bms_readings", HTTP_GET, [](AsyncWebServerRequest* request){
    if (!SPIFFS.exists("/bms_readings.html")) { request->send(404, "text/plain", "File not found"); return; }
    request->send(SPIFFS, "/bms_readings.html", "text/html");
  });

  // CAN stats
  server.on("/can_stats", HTTP_GET, [](AsyncWebServerRequest* r){
    twai_status_info_t st; 
    twai_get_status_info(&st);

    char buf[256];
    snprintf(buf, sizeof(buf),
      "rx_cnt=%lu\nrx_sw_drop=%lu\ndecoded=%lu\nrx_missed=%u\nrx_overrun=%u\nstate=%d\n",
      (unsigned long)can_rx_count, 
      (unsigned long)can_rx_dropped, 
      (unsigned long)can_decoded,
      st.rx_missed_count, 
      st.rx_overrun_count, 
      st.state
    );

    r->send(200, "text/plain", buf);
  });

  // Try late CAN init
  server.on("/can_try_init", HTTP_GET, [](AsyncWebServerRequest* r){
    if (canTryInitAndStart()) {
      r->send(200, "text/plain", "TWAI init OK; tasks started");
    } else {
      r->send(500, "text/plain", "TWAI still not ready (see Serial for reason)");
    }
  });

  // Reboot
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* r){ 
    r->send(200, "text/plain", "Rebooting..."); 
    delay(200); 
    mqttDisconnectClean(); 
    ESP.restart(); 
  });

  // Form POST: update parameters
  server.on("/update_param", HTTP_POST, [](AsyncWebServerRequest* request){
    auto getS = [&](const char* n)->String{
      return request->hasParam(n, true) ? request->getParam(n, true)->value() : String();
    };

    bool touchedSerial = false;
    bool touchedChgVolt = false;

    // Voltage: allow "12.8" (volts) or "12800" (mV)
    {
      String sv = getS("volt");
      if (sv.length()) {
        float f = sv.toFloat();
        long mv;
        if (sv.indexOf('.') >= 0 || f <= 1000.0f) {
          mv = lroundf(f * 1000.0f);
        } else {
          mv = strtol(sv.c_str(), nullptr, 10);
        }
        mv = constrain(mv, 0L, 65535L);
        config.volt = static_cast<uint16_t>(mv);
      }
    }

    // Charge Voltage (mV)
    {
      String scv = getS("chgvolt");
      if (scv.length()) {
        long mv = strtol(scv.c_str(), nullptr, 10);
        mv = constrain(mv, 0L, 65535L);
        config.chgvolt = static_cast<uint16_t>(mv);
        touchedChgVolt = true;
      }
    }

    // Temperature (°C)
    {
      String st = getS("temp");
      if (st.length()) {
        long t = strtol(st.c_str(), nullptr, 10);
        t = constrain(t, -40L, 125L);
        config.temp = static_cast<uint8_t>(t);
      }
    }

    // SoC (%)
    {
      String ss = getS("soc");
      if (ss.length()) {
        long s = strtol(ss.c_str(), nullptr, 10);
        s = constrain(s, 0L, 100L);
        config.soc = static_cast<uint8_t>(s);
      }
    }

    // Runtimes (minutes)
    {
      String sr = getS("runtime");
      if (sr.length()) config.disruntime = static_cast<uint32_t>(max(0L, strtol(sr.c_str(), nullptr, 10)));

      String sc = getS("chgtime");
      if (sc.length()) config.chgruntime = static_cast<uint32_t>(max(0L, strtol(sc.c_str(), nullptr, 10)));
    }

    // BMS charge limits (%)
    {
      String su = getS("bmschgup");
      if (su.length()) config.bmsChgUp = (uint8_t)constrain(strtol(su.c_str(), nullptr, 10), 0L, 100L);

      String sd = getS("bmschgdn");
      if (sd.length()) config.bmsChgDn = (uint8_t)constrain(strtol(sd.c_str(), nullptr, 10), 0L, 100L);
    }

    // Serial (exactly 16 chars)
    {
      String s = getS("serial");
      if (s.length() == 16) {
        s.toCharArray(config.serialStr, 17);
        touchedSerial = true;
      }
    }

    // Persist only the two "worth storing" fields if they were provided
    if (touchedSerial || touchedChgVolt) {
      saveCoreConfig();
    }

    Serial.printf(
      "Updated via POST → volt=%u mV, chgvolt=%u mV, temp=%d C, soc=%u%%, chg=%u min, dis=%u min\n",
      (unsigned)config.volt,
      (unsigned)config.chgvolt,
      (int8_t)config.temp,
      (unsigned)config.soc,
      (unsigned)config.chgruntime,
      (unsigned)config.disruntime
    );

    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Serve entire SPIFFS as static (optional) and set default file
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("/index.html");

  // 404 handler
  server.onNotFound([](AsyncWebServerRequest *req){
    Serial.printf("[HTTP 404] %s %s\n",
                  req->method()==HTTP_GET?"GET": req->method()==HTTP_POST?"POST":"OTHER",
                  req->url().c_str());
    req->send(404, "text/plain", String("Not found: ")+req->url());
  });
}

