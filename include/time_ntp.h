#pragma once
#include <Arduino.h>

// call once, after you have a valid time(nullptr) > some threshold
void recordNtpSync();

// returns floating‐point “Unix time” with microsecond precision
double now_seconds();

// NTP init
bool initNTP(uint32_t timeoutMs = 10000);

// small deadline helper 
inline bool due(uint32_t now, uint32_t &next_deadline) {
  return (int32_t)(now - next_deadline) >= 0;
}
