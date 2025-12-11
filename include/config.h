#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define FW_VERSION "2.1.0"

// RS485
#define RS485_TX 22
#define RS485_RX 21
#define RS485_CALLBACK 17
#define RS485_EN 19
#define BOOST_ENABLE_PIN 16

// WS2812B
#define WS2812B_DATA 4

// CAN
#define CAN_TX 27
#define CAN_RX 26
#define CAN_SPEED_MODE 23

// RS485 and CAN Boost power supply
#define ME2107_EN 16 

// SD
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS 13

// ---- Preferences ----
extern Preferences prefs;

// ---- Global configuration structure ----
struct Config {
  uint8_t soc = 1;
  uint16_t volt = 0;
  uint16_t chgvolt = 58400;
  uint8_t temp = 20;
  uint32_t disruntime = 1;
  uint32_t chgruntime = 1;
  uint8_t bmsChgUp = 70;
  uint8_t bmsChgDn = 30;
  bool flagCB = false;
  char serialStr[17] = "M102Z3B4ZE5H1234";
  bool txlogging = true;
  bool rxlogging = true;
  bool canTxEnabled = true;
  bool canRxEnabled = true;
  bool message3C = true;
  bool message13 = true;
  bool messageCB = true;
  bool message70 = true;
  bool message0B = true;
  bool message5C = true;
  bool message68 = true;
  bool message4F = true;
  bool message8C = true;
  bool message24 = true;
  bool acout5C = false;
  bool moschg = false;
  bool mosdis = false;
  bool batt = true; // Auto-sync BMS values form battery
  bool batteryMaster = true; // Master battery disconnect (separate from 'batt' auto-sync)
};

// Global config instance
extern Config config;
extern bool canHealth;

// Returns pointer to a config boolean by key name, or nullptr if unknown
bool* getTogglePtrByKey(const String& k);

// ---- Storing core fields ----
void loadCoreConfig();
void saveCoreConfig();

String deviceId();
