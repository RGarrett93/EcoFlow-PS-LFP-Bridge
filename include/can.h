#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "config.h"

// ---- Queue sizes ----
#ifndef TWAI_RXQ
  #define TWAI_RXQ 32
  #define TWAI_TXQ 16
#endif

// ---- CAN state ----
extern bool twai_ok;

extern QueueHandle_t canRxQ;

extern volatile uint32_t can_rx_count;
extern volatile uint32_t can_rx_dropped;
extern volatile uint32_t can_decoded;

// ---- Driver initialiser ----
void canInitDriver();

// ---- Create RX queue + start tasks if driver ok ----
void canStartTasks();

// ---- /can_try_init Link ---- 
bool canTryInitAndStart();

// ---- CAN Frame EcoFlow sender ----
bool sendCANFrame(uint32_t can_id, const uint8_t* data, uint8_t len);
