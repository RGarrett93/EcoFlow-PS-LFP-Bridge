#include "time_ntp.h"
#include <WiFi.h> // Core library for WiFi
#include <time.h>

static time_t    ntp_sec      = 0;      // last NTP‐synced whole seconds
static uint64_t  ntp_micros   = 0;   

void recordNtpSync() {
  ntp_sec    = time(nullptr);
  ntp_micros = micros();
}

// returns floating‐point “Unix time” with microsecond precision for logs
double now_seconds() {
  uint64_t delta_us = micros() - ntp_micros;
  return double(ntp_sec) + double(delta_us) * 1e-6;
}


bool initNTP(uint32_t timeoutMs) {
  const long  gmtOffset_sec      = 0;
  const int   daylightOffset_sec = 3600;

  // If now WiFi available or set, skip NTP
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skipping NTP: no WiFi STA connection.");
    ntp_sec = 0;
    return false;
  }

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");

  Serial.print("Waiting for NTP");
  const uint32_t start = millis();
  time_t now = 0;

  while ((now = time(nullptr)) < 24L * 3600L && (millis() - start) < timeoutMs) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (now < 24L * 3600L) {
    Serial.println("NTP timeout; continuing without synced time.");
    ntp_sec = 0;
    return false;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time after NTP.");
    ntp_sec = 0;
    return false;
  }

  ntp_sec = mktime(&timeinfo);
  Serial.printf("NTP synced: %ld\n", (long)ntp_sec);
  return true;
}
