#pragma once
#include <Arduino.h>
#include <stddef.h>
#include "driver/twai.h"
#include "config.h"
#include "bms.h"

// ---- XOR state (set by decoder) ----
extern uint8_t xor3C;
extern uint8_t xor8C;
extern uint8_t xor24;
extern uint8_t xorCB;
extern uint8_t xorCounter;

// ---- Headers ----
extern uint8_t header_3C[];
extern uint8_t header_13[];
extern uint8_t header_70[];
extern uint8_t header_0B_02[];
extern uint8_t header_0B_04[];
extern uint8_t header_0B_05[];
extern uint8_t header_0B_08[];
extern uint8_t header_0B_50[];
extern uint8_t header_5C[];
extern uint8_t header_68[];
extern uint8_t header_C4[];
extern uint8_t header_4F[];
extern uint8_t header_8C[];
extern uint8_t header_24[];
extern uint8_t header_CB_2031[];
extern uint8_t header_CB_2033[];
extern uint8_t header_CB_321[];
extern uint8_t header_CB_141[];
extern uint8_t header_CB_150[];

// ---- Payloads ----
extern uint8_t payload_13[];
extern uint8_t payload_3C[];
extern uint8_t payload_CB[];
extern uint8_t payload_70[];
extern uint8_t payload_0B[];
extern uint8_t payload_5C[];
extern uint8_t payload_68[];
extern uint8_t payload_C4[69];
extern uint8_t payload_4F[];
extern uint8_t payload_8C[];
extern uint8_t payload_24[];

// ---- Timer ---- 
double now_seconds();

// ---- Wrappers ---- 

void ecoflowSend3C(); //
void ecoflowSend8C(); //
void ecoflowSend24(); //
void ecoflowSendCB2031(); //
void ecoflowSendCB2033(); //

// ---- API ----
void ecoflowMessagesInit();             // xorCounter initialiser
void canSequencer_onHeartbeatC4();      // called by decoder after heartbeat (type 0xC4)
void canTxSequencerTick();              // called from loop()

// ---- Send helpers used by decoder ----
void sendCANMessage(uint8_t* header, uint8_t* payload, size_t headerSize, size_t payloadSize);

// ---- Prepare functions used by decoder ----
void prepareMessage13(uint8_t *message);
void prepareMessage3C(uint8_t *message);
void prepareMessageEB(uint8_t *message);
void prepareMessage0B(uint8_t *message);
void prepareMessageCB(uint8_t *message);
void prepareMessage70(uint8_t *message);
void prepareMessage5C(uint8_t *message);
void prepareMessage68(uint8_t *message);
void prepareMessage4F(uint8_t *message);
void prepareMessage8C(uint8_t *message);
void prepareMessage24(uint8_t *message);
void prepareMessageCB(uint8_t *message);

// ---- EcoFlow CAN Rx Processor ----
void processEcoFlowCAN(const twai_message_t &rx);

// ---- Returns EcoFlow PowerStream serial from C4 ----
const char* getPeerSerial();
