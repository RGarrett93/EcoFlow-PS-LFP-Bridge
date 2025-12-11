#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <wi-fi.h>
#include <FS.h>
#include <SPIFFS.h>

void otaHandle() {
  ArduinoOTA.handle();
}

void otaInit(AsyncWebServer& server) {

  // --- OTA (ArduinoOTA) ---
  ArduinoOTA.onStart([](){ Serial.println("Start updating..."); })
            .onEnd([](){ Serial.println("\nUpdate finished."); })
            .onProgress([](unsigned int progress, unsigned int total){
              Serial.printf("Progress: %u%%\r", (progress * 100) / total);
            })
            .onError([](ota_error_t error){
              Serial.printf("Error[%u]: ", error);
              if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
              else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
              else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
              else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
              else if (error == OTA_END_ERROR) Serial.println("End Failed");
            });

  ArduinoOTA.begin();

  IPAddress ip = WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
  Serial.println("OTA ready. IP: " + ip.toString());

  // === OTA Manual Update: form page (GET) ===
  server.on("/ota_update", HTTP_GET, [](AsyncWebServerRequest* request){
    if (!SPIFFS.exists("/ota_update.html")) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    AsyncWebServerResponse* res = request->beginResponse(SPIFFS, "/ota_update.html", "text/html");
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });

  // POST: streaming upload + final response
  server.on("/ota_update", HTTP_POST,
    // Called once the upload is finished (or error flagged)
    [](AsyncWebServerRequest* request){
      const bool ok = !Update.hasError();

      // Schedule reboot only after the client connection is really closed,
      // so the browser can receive the 200 OK and won't show a network error.
      if (ok) {
        request->onDisconnect([](){
          Serial.println("OTA: client disconnected, rebooting");
          delay(250);
          ESP.restart();
        });
      }

      AsyncWebServerResponse *res =
        request->beginResponse(200, "text/plain", ok ? "OK" : "FAIL");
      res->addHeader("Connection", "close");
      request->send(res);
    },

    // Called repeatedly with file chunks
    [](AsyncWebServerRequest* request, String filename, size_t index,
       uint8_t *data, size_t len, bool final)
    {
      if (index == 0) {
        Serial.printf("OTA: Start '%s' (contentLength=%u)\n",
                      filename.c_str(), (unsigned)request->contentLength());
        // start with max available size
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
        // Optional: honor MD5 header if you send it
        if (request->hasHeader("X-MD5")) {
          Update.setMD5(request->getHeader("X-MD5")->value().c_str());
        }
      }

      if (len) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }

      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA: End ok, total=%u bytes\n", (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  // === Legacy OTA page (GET) ===
  server.on("/ota_old", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
      "<form method='POST' action='/ota_old' enctype='multipart/form-data'>"
      "<input type='file' name='update'><br><br>"
      "<input type='submit' value='OTA Update'>"
      "</form>");
  });

  // === Legacy OTA Upload endpoint (POST) ===
  server.on("/ota_old", HTTP_POST,
    // Request done: send result and reboot
    [](AsyncWebServerRequest *request) {
      String status = Update.hasError() ? "OTA Update Failed"
                                        : "OTA Update Success. Rebooting...";
      AsyncWebServerResponse *resp =
          request->beginResponse(200, "text/plain", status);
      resp->addHeader("Connection", "close");
      request->send(resp);

      delay(1000);
      ESP.restart();
    },
    // Upload handler: called repeatedly with chunks
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA Update: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }

      if (len) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }

      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA Update Success: %u bytes\n", (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    }
  );
}
