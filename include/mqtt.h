#pragma once
#include <Arduino.h>

class OverkillSolarBms2;

struct Config;
extern Config config;

// BMS instance exposed for MQTT/CAN/Web
extern OverkillSolarBms2 bms;

// Public API
void mqttInit(const String& devId);
void mqttLoopTick();
void mqttMarkDiscoveryDirty();
void loadMqttConfig();
void mqttDisconnectClean();
