#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <bms2.h>

// ---- Expose the BMS instance for other modules (MQTT, CAN, Web UI) ----
extern HardwareSerial& bmsSerial;
extern OverkillSolarBms2 bms;

// ---- UI-driven BMS MOSFET tracking ----
extern bool lastWebMoschg;
extern bool lastWebMosdis;
extern bool pendingMoschgChange;
extern bool pendingMosdisChange;

// ---- Live power numbers derived from BMS ----
extern float inputWatt;
extern float outputWatt;

// ---- Loop helpers ----
void bmsLoopInit();   // call once in setup()
void bmsLoopTick();   // call every loop()


// ---- Init RS485 + bind callbacks + first poll ----
void bmsInit();
// ---- Battery Master feature ----
void batteryMasterInit();
// ---- Non-blocking tick. Call once per loop ----
void applyBatteryMasterIfChanged();

// ---- Poll BMS now (blocking) ----
inline void bmsPoll(bool force = true) {
  bms.main_task(force);
}
