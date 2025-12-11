#include "config.h"
#include "web.h"
#include "ecoflow.h"
#include "can.h"   // must provide sendCANFrame()
#include <string.h>

//EcoFlow PowerStream serial (from C4), 16 chars + null
static char SerialPS[17] = {0};

const char* getPeerSerial() {
  return SerialPS;
}

extern volatile uint32_t can_rx_count;
extern volatile uint32_t can_rx_dropped;
extern volatile uint32_t can_decoded;

extern float inputWatt;
extern float outputWatt;
extern String canLog;


// ================= XOR state =================
uint8_t xor3C; // Save C4 XOR
uint8_t xor8C; // Save 8C XOR
uint8_t xor24; // Save 8C XOR
uint8_t xorCB; // Save CB but only for 2031 & 2033 BMS Limits
uint8_t xorCounter; //Incrementing counter for XOR byte

// ================= Sequencer health input =================
#ifndef C4_LOSS_TIMEOUT_MS
#define C4_LOSS_TIMEOUT_MS 800
#endif

// Action Sequencer definitions
enum TxAction : uint8_t {
  A_70,
  A_0B_04, A_0B_02, A_0B_05, A_0B_50, A_0B_08,
  A_4F,
  A_68,
  A_13,
  A_CB_321, A_CB_141,
  A_5C,
  A_CB_150,
};

// One cycle step: each action and the gap in ms
struct Step { TxAction act; uint16_t gap_ms; };

static const Step kSeq[] = {
  {A_70,     25},
  {A_0B_04,   1},
  {A_0B_02,   1},
  {A_0B_05,   1},
  {A_0B_50,   1},
  {A_0B_08, 100},
  {A_4F,    100},
  {A_0B_04,   1},
  {A_0B_02,   1},
  {A_0B_05,   1},
  {A_0B_50,   1},
  {A_0B_08, 100},
  {A_68,     4},
  {A_13,     1},
  {A_CB_321, 1},
  {A_CB_141, 1},
  {A_5C,     1},
  {A_CB_150,100},
  {A_0B_04,  1},
  {A_0B_02,  1},
  {A_0B_05,  1},
  {A_0B_50,  1},
  {A_0B_08,200},
};
static const uint8_t kSeqCount = sizeof(kSeq)/sizeof(kSeq[0]);

// Runtime state
static bool     g_seqRunning = false;
static uint8_t  g_seqIndex   = 0;
static uint32_t g_nextDueMs  = 0;
static uint32_t g_lastC4ms   = 0;

// Forward
static void sendAction(TxAction a);

// ================= Headers =================
uint8_t header_3C[] = {
    0xaa, 0x03, 0x84, 0x00, 0x3c, 0x2e, 0xac, 0x04,
    0x00, 0x00, 0x0b, 0x3c, 0x03, 0x14, 0x01, 0x01,
    0x03, 0x2f
};

uint8_t header_13[] = {
    0xAA ,0x03, 0xBA, 0x00, 0x13, 0x2C, 0x00, 0x1a,
    0x00, 0x00, 0x0B, 0x3C, 0x03, 0x14, 0x01, 0x00,
    0x03, 0x1A
};


uint8_t header_CB_2031[] = { // BMS Upper Ack
    0xAA, 0x03, 0x01, 0x00, 0xCB, 0x2E, 0xF7, 0x3A, 
    0x00, 0x00, 0x0B, 0x3C, 0x03, 0x14, 0x01, 0x01, 
    0x20, 0x31
};

uint8_t header_CB_2033[] = { // BMS Lower Ack
    0xAA, 0x03, 0x01, 0x00, 0xCB, 0x2E, 0xF7, 0x3A, 
    0x00, 0x00, 0x0B, 0x3C, 0x03, 0x14, 0x01, 0x01, 
    0x20, 0x33
};

uint8_t header_CB_321[] = { // BMS Lower Ack
    0xAA, 0x03, 0x01, 0x00, 0xCB, 0x2C, 0x5E, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x08, 0x01, 0x00, 
    0x03, 0x21
};

uint8_t header_CB_141[] = { 
    0xAA, 0x03, 0x01, 0x00, 0xCB, 0x2C, 0x5F, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x08, 0x01, 0x00, 
    0x01, 0x41
};

uint8_t header_CB_150[] = { 
    0xAA, 0x03, 0x01, 0x00, 0xCB, 0x2C, 0x72, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x08, 0x01, 0x00, 
    0x01, 0x50
};

uint8_t header_70[] = {
    0xAA, 0x03, 0x20, 0x00, 0x70, 0x2C, 0x86, 0x44, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x35, 0x01, 0x00, 
    0x35, 0x10
};

uint8_t header_0B_02[] = {
    0xAA, 0x03, 0x1A, 0x00, 0x0B, 0x2C, 0x8E, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x02, 0x01, 0x00, 
    0x03, 0x07
};

uint8_t header_0B_04[] = {
    0xAA, 0x03, 0x1A, 0x00, 0x0B, 0x2C, 0x8E, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x04, 0x01, 0x00, 
    0x03, 0x07
};

uint8_t header_0B_05[] = {
    0xAA, 0x03, 0x1A, 0x00, 0x0B, 0x2C, 0x8C, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x05, 0x01, 0x00, 
    0x03, 0x07
};

uint8_t header_0B_08[] = {
    0xAA, 0x03, 0x1A, 0x00, 0x0B, 0x2C, 0x8E, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x08, 0x01, 0x00, 
    0x03, 0x07
};

uint8_t header_0B_50[] = {
    0xAA, 0x03, 0x1A, 0x00, 0x0B, 0x2C, 0x8D, 0x47, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x50, 0x01, 0x00, 
    0x03, 0x07
};

uint8_t header_5C[] = {
    0xAA, 0x03, 0x0A, 0x00, 0x5C, 0x2C, 0x98, 0x46, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x08, 0x01, 0x00, 
    0x03, 0x22
};

uint8_t header_68[] = {
    0xAA, 0x03, 0x80, 0x00, 0x68, 0x2C, 0xB9, 0x45, 
    0x01, 0x00, 0x0B, 0x3C, 0x03, 0x21, 0x01, 0x00, 
    0x03, 0x01
};

uint8_t header_C4[] = {
    0xAA, 0x03, 0x45, 0x00, 0xC4, 0x2D, 0x29, 0x3B, 
    0x00, 0x00, 0x01, 0x4B, 0x14, 0x03, 0x01, 0x01, 
    0x03, 0x02
};

uint8_t header_4F[] = {
   0xAA, 0x03, 0x23, 0x00, 0x4F, 0x2C, 0x8A, 0x05, 
   0x00, 0x00, 0x0B, 0x3C, 0x03, 0x21, 0x01, 0x00, 
   0x03, 0x01
};

uint8_t header_8C[] = {
  0xAA, 0x03, 0x2C, 0x00, 0x8C, 0x2F, 0xBF, 0x00, 
  0x00, 0x00, 0x0B, 0x3C, 0x03, 0x14, 0x01, 0x01, 
  0x01, 0x05
};

uint8_t header_24[] = {
  0xAA, 0x03, 0x24, 0x00, 0x24, 0x2F, 0xCD, 0x3A,
  0x00, 0x00, 0x0B, 0x3C, 0x03, 0x14, 0x01, 0x01,
  0x01, 0x41
};

// ================= Payloads =================
uint8_t payload_13[] = {
// Start of Payload 0 - 2
0x01, 0x01, 0x01, 

//Max Voltage 3 - 4 
0x60, 0xEA, 

0x00, 0x00, 

// 7 temp
0x12,

0x00, 0x00,

0x00, 0x08,

//12 - 13
//Min Voltage
0xE8, 0xCA, 

// Static
0x00, 0x00,
// 136 or -120 (signed 2's complement)
0x00, //0x88

// Static
0xFF, 0xFF, 0xFF,

//20 temp
0x12, 

// 0 & 1?
0x00, //0x00

// Static
0x40, 0x9C, 0x00, 0x00, 

// 105 ?
0x68, //0x68

// Static
0x0C, 0x00, 0x00, 0x5F, 0x94, 0x00, 0x00, 0x04, 
0x00, 0x00, 0x00, 0x64, 


//39   40 Max Cell Voltage
0xB6, 0x0C, 
//41   42 Min Cell Voltage
0x9A, 0x0C, 

//43  44    45    46 // cell temp
0x12, 0x12, 0x12, 0x12, 

0x00, 0x01, 0x01, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 

//57  58  Input Watts
0x00, 0x00, 

0x00, 0x00, 

//250 or -6 (signed 2's complement) Output watts
//61   62
0x6a, 0xFF, //0xFA, 0xFF

//63  64
0x02, 0x7B, 

0x7B, 0x02, 0x00, 0x00,

//0?
0x01, //0x00

// Static
0x00, 0x00, 0x1C, 0x00, 0x10, 


//16S Cell Voltage 
//75
0xB4, 0x0C, 0xB6, 0x0C, 0x9A, 0x0C, 0xAE, 0x0C, 
0xB1, 0x0C, 0xAF, 0x0C, 0xB1, 0x0C, 0xA5, 0x0C, 
0xB2, 0x0C, 0xB1, 0x0C, 0xB0, 0x0C, 0xB1, 0x0C,
0xAE, 0x0C, 0xB0, 0x0C, 0xAF, 0x0C, 0xAF, 0x0C, 
//107 108    109  110    111
0x02, 0x12, 0x00, 0x12, 0x00, 

// Firmwaware 112 - 117
0x56, 0x30, 0x2E, 0x30, 0x2E, 0x30, 

// static 118 - 121
0x03, 0x01, 0x00, 0x00, 

//Serial 122 - 137
0x4D, 0x31, 0x30, 0x32, 0x5A, 0x33, 0x42, 0x34, 
0x5A, 0x45, 0x35, 0x48, 0x30, 0x36, 0x30, 0x31, 

//Static 138 - 147
0x3C, 0x0B, 0x00, 0x00, 0x20, 0x41, 0x80, 0xB5, 0x80, 0x40, 

//Voltage 148 - 149
0x7A, 0xD1, 

0x05, 

0x41, 0x11, 0x01, 

0x00, 0x01, 
//4
0x04, 0x00, 

// End of Frame
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00
};

uint8_t payload_3C[] = {
    0x01, 

    0x84, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    
    0x3c, 0x00, 0x0b, 0x00, 0x01, 0x4d, 0x03, 0x01, 
    0x01, 0x11, 0x01, 0x00, 0x01, 0x02, 0x01, 0x02, 
    0x00, 0xc8, 0x00, 0x00, 0x01, 0x00,

    0x6e, 0xd2, 
    
    0x00, 0x00, 0x51, 0x56, 0x00, 0x00, 0x01, 0x00,
    0x02, 0x8e, 0x88, 0x05, 0x41, 
    
    0x0f, 
    
    0x9a, 0xca, 
    
    0x00, 0x00, 0x49, 0xff, 0xff, 0xff, 0x00, 0x01, 
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    
    0x15, 0x15, 
    
    0x40, 0x9c, 0x00, 0x00, 
    
    0x7f, 0x32, 0x02, 0x00, 
    0x7a, 0x02, 0x00, 0x00, 
    
    0x64, 
    0x05, 
    
    0x00, 0x64
  };

uint8_t payload_CB[] = {
    0x00
  };

uint8_t payload_70[] = {
    0x01, 0x4D, 0x31, 0x30, 0x32, 0x5A, 0x33, 0x42, 
    0x34, 0x5A, 0x45, 0x35, 0x48, 0x30, 0x36, 0x30, 
    0x31, 0x01, 0x0B, 0x3C, 0x01, 0x01, 0x03, 0x4D, 
    0x01, 0x00, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00
  };

uint8_t payload_0B[] = {
    0x02, 0xF0, 0xD2, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x01, 0xCf, 0x00, 0x00, 0x00, 0x01, 0x00, 
    0x03, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00
  };

uint8_t payload_5C[] = {
    0x00, 0x02, 0x07, 0xD3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00
  };

uint8_t payload_68[] = {
    0x4d, 0x31, 0x30, 0x32, 0x5a, 0x33, 0x42, 0x34, 
    0x5a, 0x45, 0x35, 0x48, 0x30, 0x36, 0x30, 0x31, 
    0x60, 0xea, 0x00, 0x00, 0x4d, 0x03, 0x01, 0x01, 
    0xc8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x18, 0xcb, 
    0x00, 0x00, 0xd1, 0x02, 0x00, 0x00, 0x16, 0x02, 
    0x01, 0x40, 0x9c, 0x00, 0x00, 0x5f, 0x94, 0x00, 
    0x00, 0xf5, 0x0b, 0x00, 0x00, 0x04, 0x00, 0x00, 
    0x00, 0xb8, 0x0c, 0x00, 0x00, 0x9d, 0x0c, 0x00, 
    0x00, 0x16, 0x16, 0x17, 0x17, 0x00, 0x54, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x0a, 
    0x00, 0x00, 0x01, 0x32, 0x05, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x01, 
    0x00, 0x00, 0x00, 0x64, 0x00, 0x03, 0x00, 0x17, 
    0x16, 0x03, 0x00, 0x00, 0x54, 0xec, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

uint8_t payload_C4[69];

uint8_t payload_4F[] = {
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA, 0xFF, 
    0xFF, 0xFF, 0x7F, 0x32, 0x02, 0x00, 0x00, 0x64, 
    0x05, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 
    0x00, 0x25, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00
  };

uint8_t payload_8C[] = { //Version Date
    0x3C, 0x00, 0x0B, 0x00, 0x01, 0x01, 0x03, 0x4D, 
    0x11, 0x01, 0x00, 0x01, 0x4A, 0x61, 0x6E, 0x20, 
    0x32, 0x32, 0x20, 0x32, 0x30, 0x32, 0x34, 0x20, 
    0x32, 0x32, 0x3A, 0x33, 0x39, 0x3A, 0x32, 0x33, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00
  };

uint8_t payload_24[] = { //Version Date
    0x7E, 0x06, 0x00, 0x00, 0x3C, 0x00, 0x0B, 0x00, 
    0x4D, 0x31, 0x30, 0x32, 0x5A, 0x33, 0x42, 0x34, 
    0x5A, 0x45, 0x35, 0x48, 0x30, 0x36, 0x30, 0x31, 
    0xA6, 0xF1, 0x32, 0x33, 0x34, 0x36, 0x0B, 0x00, 
    0x39, 0x38, 0x36, 0x37
  };

// ================= Prepare functions =================


void prepareMessage13(uint8_t *message) {
  message[7] = config.temp;
  message[12] = config.volt& 0xFF;
  message[13] = (config.volt>> 8) & 0xFF;
  message[20] = config.temp;
  
  message[43] = config.temp;
  message[44] = config.temp;
  message[45] = config.temp;
  message[46] = config.temp;

uint8_t cell_offset = 77;
uint16_t minCellMv = 65535; // Start with highest possible
uint16_t maxCellMv = 0;     // Start with lowest possible

for (uint8_t i = 0; i < 16; i++) {
    float cellVoltage = bms.get_cell_voltage(i);
    uint16_t cell_mv = (uint16_t)(cellVoltage * 1000.0f); // Convert V to mV

    // Store in message buffer
    message[cell_offset + i * 2]     = cell_mv & 0xFF;
    message[cell_offset + i * 2 + 1] = (cell_mv >> 8) & 0xFF;

    // Track min and max
    if (cell_mv < minCellMv) minCellMv = cell_mv;
    if (cell_mv > maxCellMv) maxCellMv = cell_mv;
}

// Store Max Cell Voltage (message[39-40])
message[39] = maxCellMv & 0xFF;
message[40] = (maxCellMv >> 8) & 0xFF;

// Store Min Cell Voltage (message[41-42])
message[41] = minCellMv & 0xFF;
message[42] = (minCellMv >> 8) & 0xFF;

int16_t outputWattInt = (int16_t)outputWatt;  // Convert float to int16_t
int16_t inputWattInt = (int16_t)inputWatt;  // Convert float to int16_t
  message[57] = inputWattInt & 0xFF;
  message[58] = (inputWattInt >> 8) & 0xFF;
  message[61] = outputWattInt & 0xFF;
  message[62] = (outputWattInt >> 8) & 0xFF;
  memcpy(&message[122], config.serialStr, 16);
  message[148] = (bms.get_0x12_full_charge_voltage()) & 0xFF;
  message[149] = ((bms.get_0x12_full_charge_voltage()) >> 8) & 0xFF;

}

void prepareMessage3C(uint8_t *message) {
  memcpy(&message[3], config.serialStr, 16);
  message[41] = (config.chgvolt + 3) & 0xFF;
  message[42] = ((config.chgvolt + 3) >> 8) & 0xFF;
  message[56] = config.soc;
  message[57] = (config.volt) & 0xFF;
  message[58] = ((config.volt) >> 8) & 0xFF;
  message[114] = message[115] = config.temp;

  message[120] = config.chgruntime & 0xFF;
  message[121] = (config.chgruntime >> 8) & 0xFF;
  message[122] = (config.chgruntime >> 16) & 0xFF;
  message[123] = (config.chgruntime >> 24) & 0xFF;

  message[124] = config.disruntime & 0xFF;
  message[125] = (config.disruntime >> 8) & 0xFF;
  message[126] = (config.disruntime >> 16) & 0xFF;
  message[127] = (config.disruntime >> 24) & 0xFF;

  message[128] = config.bmsChgUp;
  message[129] = config.bmsChgDn;
}

void prepareMessageEB(uint8_t *message) {
// Nothing to prepare yet... need to work out the message structure
}

void prepareMessage0B(uint8_t *message) {
  message[1] = (config.volt + 1000) & 0xFF;         // Consistently +1000mV Battery Voltage
  message[2] = ((config.volt + 1000) >> 8) & 0xFF;

  message[9] = (config.volt - 1896) & 0xFF;         //Roughly - 1896, Maybe BMS release or trigger voltage?
  message[10] = ((config.volt - 1896) >> 8) & 0xFF; //
}

void prepareMessageCB(uint8_t *message) {
  //message[0] = config.flagCB ? 0x01 : 0x00;
}

void prepareMessage70(uint8_t *message) {
  memcpy(&message[1], config.serialStr, 16);
}

void prepareMessage5C(uint8_t *message) {
  message[2] = (config.volt) & 0xFF;
  message[3] = ((config.volt) >> 8) & 0xFF;
  message[4] = 0x00;
}

void prepareMessage68(uint8_t *message) {
  int16_t outputWattInt = (int16_t)outputWatt;  // Convert float to int16_t
  int16_t inputWattInt = (int16_t)inputWatt;  // Convert float to int16_t
  memcpy(&message[0], config.serialStr, 16);
  message[37] = config.soc;
  message[38] = (config.volt) & 0xFF;
  message[39] = ((config.volt) >> 8) & 0xFF;
  message[46] = config.temp;
  if(inputWattInt > 0) message[47] = 0x02; else message[47] = 0x00;
  int16_t balanceCapInt = (int16_t)bms.get_balance_capacity();  // Convert float to int16_t
  message[57] = (balanceCapInt * 1000) & 0xFF;
  message[58] = ((balanceCapInt * 1000) >> 8) & 0xFF;

//uint8_t cell_offset = 77;
uint16_t minCellMv = 65535; // Start with highest possible
uint16_t maxCellMv = 0;     // Start with lowest possible

for (uint8_t i = 0; i < 16; i++) {
    float cellVoltage = bms.get_cell_voltage(i);
    uint16_t cell_mv = (uint16_t)(cellVoltage * 1000.0f); // Convert V to mV

    // Store in message buffer
  //  message[cell_offset + i * 2]     = cell_mv & 0xFF;
  //  message[cell_offset + i * 2 + 1] = (cell_mv >> 8) & 0xFF;

    // Track min and max
    if (cell_mv < minCellMv) minCellMv = cell_mv;
    if (cell_mv > maxCellMv) maxCellMv = cell_mv;
}
message[65] = maxCellMv & 0xFF;
message[66] = (maxCellMv >> 8) & 0xFF;
message[69] = minCellMv & 0xFF;
message[70] = (minCellMv >> 8) & 0xFF;


  message[78] = inputWattInt & 0xFF;
  message[79] = (inputWattInt >> 8) & 0xFF;
  message[82] = outputWattInt & 0xFF;
  message[83] = (outputWattInt >> 8) & 0xFF;

  message[86] = config.disruntime & 0xFF;
  message[87] = (config.disruntime >> 8) & 0xFF;
  message[88] = (config.disruntime >> 16) & 0xFF;
  message[89] = (config.disruntime >> 24) & 0xFF;

  message[91] = config.bmsChgUp;
  message[92] = config.bmsChgDn;
}

void prepareMessage4F(uint8_t *message) {
int32_t outputWattInt = (int32_t)outputWatt;
int32_t inputWattInt  = (int32_t)inputWatt;
  // Convert float to int16_t
  message[0] = config.soc;
  if(inputWattInt > 0) message[1] = 0x02; else message[1] = 0x00;
  message[2] = inputWattInt & 0xFF;
  message[3] = (inputWattInt >> 8) & 0xFF;
  message[4] = (inputWattInt >> 16) & 0xFF;
  message[5] = (inputWattInt >> 24) & 0xFF;
  message[6] = outputWattInt & 0xFF;
  message[7] = (outputWattInt >> 8) & 0xFF;
  message[8] = (outputWattInt >> 16) & 0xFF;
  message[9] = (outputWattInt >> 24) & 0xFF;
  message[10] = config.chgruntime & 0xFF;
  message[11] = (config.chgruntime >> 8) & 0xFF;
  message[12] = (config.chgruntime >> 16) & 0xFF;
  message[13] = (config.chgruntime >> 24) & 0xFF;
  message[15] = config.bmsChgUp;
  message[16] = config.bmsChgDn;
}

void prepareMessage8C(uint8_t *message) {
// Nothing to prepare yet... need to work out the message structure
}

void prepareMessage24(uint8_t *message) {
  memcpy(&message[8], config.serialStr, 16);
}

// ================= Wrapper functions =================

void ecoflowSend3C() {
  prepareMessage3C(payload_3C);
  sendCANMessage(header_3C, payload_3C, sizeof(header_3C), sizeof(payload_3C));
}

void ecoflowSend8C() {
  prepareMessage8C(payload_8C);
  sendCANMessage(header_8C, payload_8C, sizeof(header_8C), sizeof(payload_8C));
}

void ecoflowSend24() {
  prepareMessage24(payload_24);
  sendCANMessage(header_24, payload_24, sizeof(header_24), sizeof(payload_24));
}

void ecoflowSendCB2031() {
  prepareMessageCB(payload_CB);
  sendCANMessage(header_CB_2031, payload_CB, sizeof(header_CB_2031), sizeof(payload_CB));
}

void ecoflowSendCB2033() {
  prepareMessageCB(payload_CB);
  sendCANMessage(header_CB_2033, payload_CB, sizeof(header_CB_2033), sizeof(payload_CB));
}


// ================= CRC helper =================

uint16_t crc16(const uint8_t *data, uint16_t len) {
  static const uint16_t table[] PROGMEM = {
      0, 49345, 49537, 320, 49921, 960, 640, 49729,
      50689, 1728, 1920, 51009, 1280, 50625, 50305, 1088,
      52225, 3264, 3456, 52545, 3840, 53185, 52865, 3648,
      2560, 51905, 52097, 2880, 51457, 2496, 2176, 51265,
      55297, 6336, 6528, 55617, 6912, 56257, 55937, 6720,
      7680, 57025, 57217, 8000, 56577, 7616, 7296, 56385,
      5120, 54465, 54657, 5440, 55041, 6080, 5760, 54849,
      53761, 4800, 4992, 54081, 4352, 53697, 53377, 4160,
      61441, 12480, 12672, 61761, 13056, 62401, 62081, 12864,
      13824, 63169, 63361, 14144, 62721, 13760, 13440, 62529,
      15360, 64705, 64897, 15680, 65281, 16320, 16000, 65089,
      64001, 15040, 15232, 64321, 14592, 63937, 63617, 14400,
      10240, 59585, 59777, 10560, 60161, 11200, 10880, 59969,
      60929, 11968, 12160, 61249, 11520, 60865, 60545, 11328,
      58369, 9408, 9600, 58689, 9984, 59329, 59009, 9792,
      8704, 58049, 58241, 9024, 57601, 8640, 8320, 57409,
      40961, 24768, 24960, 41281, 25344, 41921, 41601, 25152,
      26112, 42689, 42881, 26432, 42241, 26048, 25728, 42049,
      27648, 44225, 44417, 27968, 44801, 28608, 28288, 44609,
      43521, 27328, 27520, 43841, 26880, 43457, 43137, 26688,
      30720, 47297, 47489, 31040, 47873, 31680, 31360, 47681,
      48641, 32448, 32640, 48961, 32000, 48577, 48257, 31808,
      46081, 29888, 30080, 46401, 30464, 47041, 46721, 30272,
      29184, 45761, 45953, 29504, 45313, 29120, 28800, 45121,
      20480, 37057, 37249, 20800, 37633, 21440, 21120, 37441,
      38401, 22208, 22400, 38721, 21760, 38337, 38017, 21568,
      39937, 23744, 23936, 40257, 24320, 40897, 40577, 24128,
      23040, 39617, 39809, 23360, 39169, 22976, 22656, 38977,
      34817, 18624, 18816, 35137, 19200, 35777, 35457, 19008,
      19968, 36545, 36737, 20288, 36097, 19904, 19584, 35905,
      17408, 33985, 34177, 17728, 34561, 18368, 18048, 34369,
      33281, 17088, 17280, 33601, 16640, 33217, 32897, 16448};
  uint16_t crc = 0;
  for (uint16_t i = 0; i < len; i++) {
    crc = pgm_read_word_near(table + ((crc ^ data[i]) & 0xFF)) ^ (crc >> 8);
  }
  return crc;
}

// ================= sendCANMessage =================
void sendCANMessage(uint8_t* header, uint8_t* payload, size_t headerSize, size_t payloadSize) {

  #define IDX_TRK0   16  // tracker = last 4 header bytes
  #define IDX_TRK1   17

  if (!header || headerSize < 7) { Serial.println("sendCANMessage: bad header"); return; }

  // Message type (5th byte) selects ID set and framing mode
  const uint8_t msg_type = header[4];

    uint8_t  t0 = header[16], t1 = header[17];
    uint16_t trackerBE = ((uint16_t)t0 << 8) | (uint16_t)t1;

  uint32_t id_first, id_middle, id_last;
  const bool use_length_byte = (msg_type == 0xA0);

  /*if (use_length_byte) {
    // A0: length-prefixed frames (len + up to 7 data)
    id_first  = 0x00431403;
    id_middle = 0x00831403;
    id_last   = 0x00C31403;*/
  /*if (msg_type == 0xE2) {
    id_first  = 0x10014001;
    id_middle = 0x10114001;
    id_last   = 0x10214001;
  }*/
     /*else if (msg_type == 0x13 || msg_type == 0x49) {
    // 0x13 / 0x49 (and default): classic 8-byte frames
    id_first  = 0x10003001;
    id_middle = 0x10103001;
    id_last   = 0x10203001;
  }*else */
 
   uint8_t xor_key;

    id_first  = 0x10003001;
    id_middle = 0x10103001;
    id_last   = 0x10203001;

  if (msg_type == 0x3C) {
    xor_key = xor3C;
  } else if (msg_type == 0x8C) {
    xor_key = xor8C;
  } else if (msg_type == 0x24) {
    xor_key = xor24;
  } else if (msg_type == 0xCB) {
    if (trackerBE == 0x2031 || trackerBE == 0x2033) {
      xor_key = xorCB; 
    } else xor_key = xorCounter++;
  } else {
      xor_key = xorCounter++;
    }

  // ALWAYS generate a new XOR key and write it into header[6]
  //uint8_t xor_key = (uint8_t)random(0, 256);
  header[6] = xor_key;

  // Encode payload (safe if payload == nullptr)
  uint8_t encoded[(payloadSize > 0) ? payloadSize : 1];
  for (size_t i = 0; i < payloadSize; i++) {
    uint8_t src = payload ? payload[i] : 0x00;
    encoded[i] = src ^ xor_key;
  }

  // Build final message: header + encoded payload + CRC(LE, unencoded)
  uint8_t crc_buf[headerSize + payloadSize];
  memcpy(crc_buf, header, headerSize);
  if (payloadSize) memcpy(crc_buf + headerSize, encoded, payloadSize);
  uint16_t crc = crc16(crc_buf, headerSize + payloadSize);

  uint8_t final_msg[headerSize + payloadSize + 2];
  memcpy(final_msg, crc_buf, headerSize + payloadSize);
  final_msg[headerSize + payloadSize]     = (uint8_t)(crc & 0xFF);
  final_msg[headerSize + payloadSize + 1] = (uint8_t)(crc >> 8);

  const size_t total = sizeof(final_msg);
  char logBuffer[128];

  // --- Framing & TX ---
  size_t pos = 0;
  size_t frame_idx = 0;

  while (pos < total) {
    size_t remain = total - pos;

    if (!use_length_byte) {
      // 13/49 etc: raw 8B frames
      uint8_t chunk = (remain > 8) ? 8 : (uint8_t)remain;
      bool is_last  = (remain <= 8);
      uint32_t id   = (frame_idx == 0) ? id_first : (is_last ? id_last : id_middle);

      sendCANFrame(id, &final_msg[pos], chunk);
if (config.txlogging) {
#if CANDUMP
      double ts = now_seconds();
      int len = snprintf(logBuffer, sizeof(logBuffer), "(%012.6f) vcanTx %08lX#", ts, id);
      for (uint8_t j = 0; j < chunk; j++) {
        len += snprintf(logBuffer + len, sizeof(logBuffer) - len, "%02X", final_msg[pos + j]);
      }
      streamCanLog(logBuffer);
#else
      canLog += "TX 0x" + String(id, HEX) + ": ";
      for (uint8_t j = 0; j < chunk; j++) {
        if (final_msg[pos + j] < 0x10) canLog += "0";
        canLog += String(final_msg[pos + j], HEX) + (j + 1 < chunk ? " " : "");
      }
      canLog += "<br>";
#endif
    }
      pos += chunk;

    } else {
      // A0: [len][<=7 data] → DLC = len + 1
      uint8_t chunk = (remain > 7) ? 7 : (uint8_t)remain;
      if (chunk == 0) break;

      uint8_t frame_bytes[8];
      frame_bytes[0] = chunk;                       // length of following bytes
      memcpy(&frame_bytes[1], &final_msg[pos], chunk);

      bool is_last  = (remain <= 7);
      uint32_t id   = (frame_idx == 0) ? id_first : (is_last ? id_last : id_middle);

      sendCANFrame(id, frame_bytes, (uint8_t)(chunk + 1));
if (config.txlogging) {
#if CANDUMP
      double ts = now_seconds();
      int len = snprintf(logBuffer, sizeof(logBuffer), "(%012.6f) vcanTx %08lX#", ts, id);
      for (uint8_t j = 0; j < (uint8_t)(chunk + 1); j++) {
        len += snprintf(logBuffer + len, sizeof(logBuffer) - len, "%02X", frame_bytes[j]);
      }
      streamCanLog(logBuffer);
#else
      canLog += "TX 0x" + String(id, HEX) + ": ";
      for (uint8_t j = 0; j < (uint8_t)(chunk + 1); j++) {
        if (frame_bytes[j] < 0x10) canLog += "0";
        canLog += String(frame_bytes[j], HEX) + (j + 1 < (uint8_t)(chunk + 1) ? " " : "");
      }
      canLog += "<br>";
#endif
    }
      pos += chunk;
    }

    frame_idx++;
    // vTaskDelay(1);   // remove this hard delay
if (frame_idx & 0x3) taskYIELD(); // tiny cooperative yield every 4 frames
  }
}

// ================= Sequencer =================

void canSequencer_onHeartbeatC4() {
  g_lastC4ms = millis();
  if (!g_seqRunning) {
    g_seqRunning = true;
    g_seqIndex   = 0;
    g_nextDueMs  = millis();   // start immediately
    canHealth = true;
  }
}

void canTxSequencerTick() {
  // stop if heartbeat lost
  if (g_seqRunning && (millis() - g_lastC4ms > C4_LOSS_TIMEOUT_MS)) {
    g_seqRunning = false;
    canHealth = false;
  }
  if (!g_seqRunning || !config.canTxEnabled) return;

  uint32_t now = millis();
  if (now < g_nextDueMs) return;

  // send current step
  const Step& step = kSeq[g_seqIndex];
  sendAction(step.act);

  // schedule next
  g_nextDueMs = now + step.gap_ms;
  g_seqIndex = (uint8_t)((g_seqIndex + 1) % kSeqCount);
}

// ================= Send action dispatcher =================

static void sendAction(TxAction a) {
  if (!config.canTxEnabled) return;

  switch (a) {
    case A_70:
      if (config.message70) {
        prepareMessage70(payload_70);
        sendCANMessage(header_70, payload_70, sizeof(header_70), sizeof(payload_70));
      }
      break;

    case A_0B_04:
      if (config.message0B) {
        prepareMessage0B(payload_0B);
        sendCANMessage(header_0B_04, payload_0B, sizeof(header_0B_04), sizeof(payload_0B));
      }
      break;
    case A_0B_02:
      if (config.message0B) {
        prepareMessage0B(payload_0B);
        sendCANMessage(header_0B_02, payload_0B, sizeof(header_0B_02), sizeof(payload_0B));
      }
      break;
    case A_0B_05:
      if (config.message0B) {
        prepareMessage0B(payload_0B);
        sendCANMessage(header_0B_05, payload_0B, sizeof(header_0B_05), sizeof(payload_0B));
      }
      break;
    case A_0B_50:
      if (config.message0B) {
        prepareMessage0B(payload_0B);
        sendCANMessage(header_0B_50, payload_0B, sizeof(header_0B_50), sizeof(payload_0B));
      }
      break;
    case A_0B_08:
      if (config.message0B) {
        prepareMessage0B(payload_0B);
        sendCANMessage(header_0B_08, payload_0B, sizeof(header_0B_08), sizeof(payload_0B));
      }
      break;

    case A_4F:
      if (config.message4F) {
        prepareMessage4F(payload_4F);
        sendCANMessage(header_4F, payload_4F, sizeof(header_4F), sizeof(payload_4F));
      }
      break;

    case A_68:
      if (config.message68) {
        prepareMessage68(payload_68);
        sendCANMessage(header_68, payload_68, sizeof(header_68), sizeof(payload_68));
      }
      break;

    case A_13:
      if (config.message13) {
        prepareMessage13(payload_13);
        sendCANMessage(header_13, payload_13, sizeof(header_13), sizeof(payload_13));
      }
      break;

    case A_CB_321:
      if (config.messageCB) {
        prepareMessageCB(payload_CB);
        sendCANMessage(header_CB_321, payload_CB, sizeof(header_CB_321), sizeof(payload_CB));
      }
      break;

    case A_CB_141:
      if (config.messageCB) {
        prepareMessageCB(payload_CB);
        sendCANMessage(header_CB_141, payload_CB, sizeof(header_CB_141), sizeof(payload_CB));
      }
      break;

    case A_5C:
      if (config.message5C) {
        prepareMessage5C(payload_5C);
        sendCANMessage(header_5C, payload_5C, sizeof(header_5C), sizeof(payload_5C));
      }
      break;

    case A_CB_150:
      if (config.messageCB) {
        prepareMessageCB(payload_CB);
        sendCANMessage(header_CB_150, payload_CB, sizeof(header_CB_150), sizeof(payload_CB));
      }
      break;
  }
}

// ================= Xor Counter Initialiser =================

void ecoflowMessagesInit() {
  xorCounter = (uint8_t)random(0, 256);
}

// ================= EcoFlow CAN Rx Processor =================

void processEcoFlowCAN(const twai_message_t &rx) {
  uint32_t id = rx.identifier;
  uint32_t fullID = id & 0x1FFFFFFF;

  // 14001 IDs
  #define MSG14001_START_ID   0x10014001UL
  #define MSG14001_MID_ID     0x10114001UL
  #define MSG14001_END_ID     0x10214001UL

  // Fixed parts
  #define MSG14001_HDR_LEN    18
  #define MSG14001_TIMEOUT_MS 300

  // Header indices
  #define IDX_TYPE   4   // msg_type
  #define IDX_XOR    6   // XOR key (unencoded)
  #define IDX_LEN_LO 2   // payload length (lo)
  #define IDX_LEN_HI 3   // payload length (hi)
  #define IDX_TRK0   16  // tracker = last 4 header bytes
  #define IDX_TRK1   17

  #ifndef MSG14001_MAX_PAYLOAD
  #define MSG14001_MAX_PAYLOAD 2048
  #endif
  #define MSG14001_BUF_CAP (MSG14001_HDR_LEN + MSG14001_MAX_PAYLOAD + 2)

  static uint8_t  buf[MSG14001_BUF_CAP];
  static size_t   have = 0;
  static bool     active = false;
  static uint32_t lastTime = 0;

  // dynamic fields for the in-progress message
  static bool     lenKnown = false;
  static uint16_t payloadLen = 0;
  static size_t   targetTotal = 0; // = 18 + payloadLen + 2 once known

  // monitoring
  static uint16_t typeCount[256] = {0};
  static uint8_t  curType = 0xFF;       static bool curTypeValid = false;
  static uint16_t curTrackerBE = 0;     static bool curTrackerValid = false;
  static uint8_t  lastType = 0;
  static uint16_t lastTrackerBE = 0;

  auto reset_state = [&](){
    have = 0; active = false; lastTime = 0;
    lenKnown = false; payloadLen = 0; targetTotal = 0;
    curTypeValid = false; curTrackerValid = false; curType = 0xFF; curTrackerBE = 0;
  };

  auto append_bytes = [&](const uint8_t* data, uint8_t dlc){
    if (!active || dlc == 0) return;
    if (have + dlc > MSG14001_BUF_CAP) dlc = (uint8_t)(MSG14001_BUF_CAP - have); // clamp
    memcpy(&buf[have], data, dlc);
    have += dlc;
    lastTime = millis();

    // Capture type early
    if (!curTypeValid && have >= (IDX_TYPE + 1)) {
      curType = buf[IDX_TYPE];
      curTypeValid = true;
    }
    // Determine payload length when we have first 4 header bytes
    if (!lenKnown && have >= (IDX_LEN_HI + 1)) {
      payloadLen = (uint16_t)buf[IDX_LEN_LO] | ((uint16_t)buf[IDX_LEN_HI] << 8); // little-endian
      // Guard against oversize messages
      if (payloadLen > MSG14001_MAX_PAYLOAD) {
        char m[96];
        snprintf(m, sizeof(m), "14001 oversize payload %u > cap %u — dropping",
                 payloadLen, (unsigned)MSG14001_MAX_PAYLOAD);
        streamDebug(m);
        reset_state();
        return;
      }
      targetTotal = (size_t)MSG14001_HDR_LEN + (size_t)payloadLen + 2U;
      lenKnown = true;
    }
    // Tracker available once full header is buffered
    if (!curTrackerValid && have >= MSG14001_HDR_LEN) {
      curTrackerBE = ((uint16_t)buf[IDX_TRK0] << 8)  |  (uint16_t)buf[IDX_TRK1];
      curTrackerValid = true;
    }
  };

  auto is_printable = [](uint8_t c){ return (c >= 32 && c <= 126); };

  auto try_finish = [&](){
    if (!active || !lenKnown) return false;
    if (have < targetTotal)   return false;

    uint8_t msg_type = buf[IDX_TYPE];
    uint8_t xor_key  = buf[IDX_XOR];

    uint8_t  t0 = buf[IDX_TRK0], t1 = buf[IDX_TRK1];
    uint16_t trackerBE = ((uint32_t)t0 << 8) | (uint32_t)t1;

    // CRC16 at end (hi before lo)
    uint8_t  crc_hi = buf[targetTotal - 2];
    uint8_t  crc_lo = buf[targetTotal - 1];
    uint16_t crc    = ((uint16_t)crc_hi << 8) | crc_lo;

    static uint8_t decoded[MSG14001_MAX_PAYLOAD];
    for (uint16_t i = 0; i < payloadLen; ++i)
      decoded[i] = buf[MSG14001_HDR_LEN + i] ^ xor_key;

    typeCount[msg_type]++; lastType = msg_type; lastTrackerBE = trackerBE;

    if (msg_type == 0xC4) {
      // Serial is expected at [3..18] for C4
      char serial[17] = {0};
      bool printable = (payloadLen >= 19);
      for (int i = 0; i < 16 && i + 3 < payloadLen; ++i) {
        uint8_t c = decoded[3 + i];
        serial[i] = is_printable(c) ? (char)c : '?';
        if (!is_printable(c)) printable = false;
      }

      if (printable) {
        strncpy(SerialPS, serial, sizeof(SerialPS) - 1);
        SerialPS[sizeof(SerialPS) - 1] = '\0';
      } else {
        SerialPS[0] = '\0'; // invalid / missing → clear
      }

      char dbg[256];
      snprintf(dbg, sizeof(dbg),
               "14001 OK type=C4 len=%u cnt=%u XOR=0x%02X CRC=%04X tracker=%02X%02X (BE=0x%04XX) serial=%s%s",
               payloadLen, (unsigned)typeCount[msg_type], xor_key, crc,
               t0, t1, (uint16_t)trackerBE,
               serial, printable ? "" : " (non-printable/missing)");
      streamDebug(dbg);

      // Save XOR for 3C reply
      xor3C = xor_key;

      // Reply to heartbeat only
      if (config.canTxEnabled && config.message3C) {
        ecoflowSend3C();
      }

      // Begin sequencer
      canSequencer_onHeartbeatC4();

    } else if (msg_type == 0xDE) {
      char dbg[128];
      snprintf(dbg, sizeof(dbg),
               "14001 OK type=DE len=%u cnt=%u XOR=0x%02X CRC=%04X tracker=%02X%02X (BE=0x%04X)",
               payloadLen, (unsigned)typeCount[msg_type], xor_key, crc,
               t0, t1, (uint16_t)trackerBE);
      streamDebug(dbg);

      if (trackerBE == 0x0105) {
        xor8C = xor_key;
        if (config.canTxEnabled && config.message8C) {
          ecoflowSend8C();
        }
      }
      if (trackerBE == 0x0141) {
        xor24 = xor_key;
        if (config.canTxEnabled && config.message24) {
          ecoflowSend24();
        }
      }

    } else if (msg_type == 0xCB) {

      if (trackerBE == 0x2031) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "14001 OK type=CB len=%u cnt=%u XOR=0x%02X CRC=%04X BE=0x%04X Upper Limit=%u",
                 payloadLen, (unsigned)typeCount[msg_type], xor_key, crc,
                 (uint16_t)trackerBE, (payloadLen ? decoded[0] : 0));
        streamDebug(dbg);

        xorCB = xor_key;
        if (payloadLen >= 1) config.bmsChgUp = decoded[0];

        if (config.canTxEnabled && config.messageCB) {
          ecoflowSendCB2031();
        }
      }

      if (trackerBE == 0x2033) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "14001 OK type=CB len=%u cnt=%u XOR=0x%02X CRC=%04X BE=0x%04X Lower Limit=%u",
                 payloadLen, (unsigned)typeCount[msg_type], xor_key, crc,
                 (uint16_t)trackerBE, (payloadLen ? decoded[0] : 0));
        streamDebug(dbg);

        xorCB = xor_key;
        if (payloadLen >= 1) config.bmsChgDn = decoded[0];

        if (config.canTxEnabled && config.messageCB) {
          ecoflowSendCB2033();
        }
      }

    } else {
      char preview[3*8+1] = {0};
      int p = 0;
      int show = (payloadLen < 8) ? payloadLen : 8;
      for (int i = 0; i < show; ++i)
        p += snprintf(preview + p, sizeof(preview) - p, "%02X", decoded[i]);

      char dbg[256];
      snprintf(dbg, sizeof(dbg),
               "14001 OK type=0x%02X len=%u cnt=%u XOR=0x%02X CRC=%04X tracker=%02X%02X (BE=0x%04X) payload[0..%d]=%s",
               msg_type, payloadLen, (unsigned)typeCount[msg_type], xor_key, crc,
               t0, t1, (uint16_t)trackerBE, show-1, preview);
      streamDebug(dbg);
    }

    reset_state();
    return true;
  };

  // ----- route incoming frame -----
  if (fullID == MSG14001_START_ID) {
    reset_state(); active = true; lastTime = millis();
    append_bytes(rx.data, rx.data_length_code);
    streamDebug("14001 start");
  } else if (fullID == MSG14001_MID_ID) {
    append_bytes(rx.data, rx.data_length_code);
  } else if (fullID == MSG14001_END_ID) {
    append_bytes(rx.data, rx.data_length_code);
    (void)try_finish();
  }

  // ----- timeout -----
  if (active && (millis() - lastTime > MSG14001_TIMEOUT_MS)) {
    streamDebug("14001 timeout — reset");
    reset_state();
  }

  // optional raw logging
  if (config.rxlogging) {
    char logBuffer[96];
    double ts = now_seconds();
    int len = snprintf(logBuffer, sizeof(logBuffer), "(%012.6f) vcanRx %08lX#", ts, id);
    for (uint8_t i = 0; i < rx.data_length_code; i++)
      len += snprintf(logBuffer + len, sizeof(logBuffer) - len, "%02X", rx.data[i]);
    streamCanLog(logBuffer);
  }
}
