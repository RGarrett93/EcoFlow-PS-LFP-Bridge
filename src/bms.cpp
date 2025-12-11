#include "bms.h"
#include "config.h"

#include <math.h>

// Internal 3s timer (mirrors old main.cpp behaviour)
static unsigned long bmsTimer = 0;
#define RS485_BAUD 9600

// These live in main.cpp (UI + pending MOS changes)
extern bool lastWebMoschg;
extern bool lastWebMosdis;
extern bool pendingMoschgChange;
extern bool pendingMosdisChange;

// Battery Master Disconnect tracking
static bool prev_canTxEnabled = true;
static bool prev_moschg = false;
static bool prev_mosdis = false;
static bool batteryMasterLast = true;

// If these debug macros were only in main.cpp, ensure defaults here
#ifndef VERBOSE_BMS_PRINTS
#define VERBOSE_BMS_PRINTS 0
#endif

// --- Definitions for externs ---
bool lastWebMoschg = false;
bool lastWebMosdis = false;

bool pendingMoschgChange = false;
bool pendingMosdisChange = false;

float inputWatt = 0;
float outputWatt = 0;


// Direction control (unchanged behaviour)
static void setRS485Transmit(bool enable) {
  digitalWrite(RS485_CALLBACK, enable ? LOW : HIGH); // LOW=Transmit, HIGH=Receive
}

// Keep the same Serial1 alias you were using
HardwareSerial& bmsSerial = Serial1;
OverkillSolarBms2 bms = OverkillSolarBms2();

static void preTransmission() {
  digitalWrite(RS485_CALLBACK, HIGH);
}
static void postTransmission() {
  digitalWrite(RS485_CALLBACK, LOW);
}

void bmsInit() {
  // Ensure pins are configured as before
  pinMode(RS485_CALLBACK, OUTPUT);
  pinMode(RS485_EN, OUTPUT);

  digitalWrite(RS485_EN, HIGH);
  setRS485Transmit(false);

  // Same begin() as your current setup
  bmsSerial.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

  // Same library wiring
  bms.begin(&bmsSerial);
  bms.preTransmission(preTransmission);
  bms.postTransmission(postTransmission);

  // First poll exactly as before
  bms.main_task(true);

  batteryMasterInit();
  bmsLoopInit();
}

void batteryMasterInit() {
  // Init BatteryMaster tracking from current config state
  batteryMasterLast = config.batteryMaster;
  prev_canTxEnabled = config.canTxEnabled;
  prev_moschg = config.moschg;
  prev_mosdis = config.mosdis;
}

void applyBatteryMasterIfChanged() {
  if (config.batteryMaster == batteryMasterLast) return;

  // Transition: ON -> OFF
  if (!config.batteryMaster) {
    // Save previous states
    prev_canTxEnabled = config.canTxEnabled;

    // These reflect UI intent; actual BMS state is mirrored later in loop()
    prev_moschg = config.moschg;
    prev_mosdis = config.mosdis;

    // Force TX off
    config.canTxEnabled = false;

    // Force MOSFET OFF via existing pending mechanism
    lastWebMoschg = false;
    pendingMoschgChange = true;

    lastWebMosdis = false;
    pendingMosdisChange = true;

    // Update UI-facing intent immediately
    config.moschg = false;
    config.mosdis = false;

    Serial.println("[BatteryMaster] OFF: saved states + disabled CAN TX + requested MOSFET OFF");
  }
  // Transition: OFF -> ON
  else {
    // Restore TX only if it was previously enabled
    config.canTxEnabled = prev_canTxEnabled;

    // Restore MOSFETs only if they were previously enabled
    if (prev_moschg) {
      lastWebMoschg = true;
      pendingMoschgChange = true;
      config.moschg = true;
    }
    if (prev_mosdis) {
      lastWebMosdis = true;
      pendingMosdisChange = true;
      config.mosdis = true;
    }

    Serial.println("[BatteryMaster] ON: restored previous CAN TX + requested prior MOSFET states");
  }

  batteryMasterLast = config.batteryMaster;
}

void bmsLoopInit() {
  bmsTimer = millis();
}

void bmsLoopTick() {
  if (millis() - bmsTimer <= 3000) return;
  bmsTimer = millis();

  bms.main_task(true);

  // --- Charging runtime estimation ---
  float soc = bms.get_state_of_charge();
  float balance_capacity = bms.get_balance_capacity(); // Ah
  float charging_capacity = bms.get_rate_capacity() - balance_capacity;
  float current = bms.get_current(); // A
  float soc_fraction = soc / 100.0f;

  if (current > 0) {
    inputWatt = bms.get_voltage() * bms.get_current();
    outputWatt = 0;
  } else if (bms.get_current() < 0) {
    outputWatt = bms.get_voltage() * bms.get_current();  // Will be negative
    inputWatt = 0;
  } else {
    inputWatt = 0;
    outputWatt = 0;
  }

  // --- Discharge runtime estimation ---
  float standby_current = 0.0195f; // 19.5mA in Amps, always positive
  float used_current = current < -0.01f ? fabs(current) : standby_current;
  float discharge_runtime_hours = (balance_capacity * soc_fraction) / used_current;
  int discharge_runtime_minutes = int(discharge_runtime_hours * 60);

  // --- Charging runtime estimation ---
  int charge_runtime_minutes = 0;
  if (current > 0.01f) {
    float charge_runtime_hours = charging_capacity / current;
    charge_runtime_minutes = int(charge_runtime_hours * 60);
  } else {
    charge_runtime_minutes = discharge_runtime_minutes;
  }

  float runtime_hours = (balance_capacity * soc_fraction) / used_current;
  int runtime_minutes = int(runtime_hours * 60);

  int hours = int(runtime_hours);
  int minutes = int((runtime_hours - hours) * 60);

  if (bms.get_bms_name() != NULL) {
#if VERBOSE_BMS_PRINTS
    Serial.println("***********************************************");
    Serial.print("State of charge:\t"); Serial.print(bms.get_state_of_charge()); Serial.println("\t% ");
    Serial.print("Current:\t\t"); Serial.print(bms.get_current()); Serial.println("\tA  ");
    Serial.print("Voltage:\t\t"); Serial.print(bms.get_voltage()); Serial.println("\tV  ");

    for (uint8_t i = 0; i < bms.get_num_cells(); i++) {
      Serial.print((String)"Cell " + (i + 1) + " -\t\t");
      Serial.print(bms.get_cell_voltage(i), 3);
      Serial.print("\tV\t");
      Serial.println(bms.get_balance_status(i) ? "(balancing)" : "(not balancing)");
    }

    Serial.print("Balance capacity:\t"); Serial.print(bms.get_balance_capacity()); Serial.println("\tAh  ");
    Serial.print("Rate capacity:\t\t"); Serial.print(bms.get_rate_capacity()); Serial.println("\tAh  ");

    for (uint8_t i = 0; i < bms.get_num_ntcs(); i++) {
      Serial.print((String)"Termometer " + (i + 1) + " -\t\t");
      Serial.print(bms.get_ntc_temperature(i));
      Serial.println("\tdeg.\t");
    }

    Serial.print((String)"Charge mosfet" + " -\t\t");
    bms.get_charge_mosfet_status() ? Serial.print("Enabled") : Serial.print("Disabled");
    Serial.println("");

    Serial.print((String)"Discharge mosfet" + " -\t");
    bms.get_discharge_mosfet_status() ? Serial.print("Enabled") : Serial.print("Disabled");
    Serial.println("");

    Serial.print((String)"Cycle count" + " -\t\t"); Serial.print(bms.get_cycle_count()); Serial.println("");
    Serial.print((String)"protection_status" + " -\t"); Serial.print(bms.get_protection_status_summary()); Serial.println("");
    Serial.print((String)"get_bms_name" + " -\t\t"); Serial.print(bms.get_bms_name()); Serial.println("");

    Serial.printf("Estimated dischargeruntime: %dH-%dM (%d minutes, using %s)\n",
      hours, minutes, runtime_minutes,
      (fabs(current) > 0.01f) ? "measured current" : "standby current"
    );

    Serial.printf("Estimated charging time: %d min\n", charge_runtime_minutes);
#endif
  }

  // Apply pending MOSFET changes requested from UI
  if (pendingMoschgChange && bms.get_charge_mosfet_status() != lastWebMoschg) {
    bms.set_0xE1_mosfet_control_charge(lastWebMoschg);
    Serial.print("Charge MOSFET set to: "); Serial.println(lastWebMoschg);
    pendingMoschgChange = false;
  }

  if (pendingMosdisChange && bms.get_discharge_mosfet_status() != lastWebMosdis) {
    bms.set_0xE1_mosfet_control_discharge(lastWebMosdis);
    Serial.print("Discharge MOSFET set to: "); Serial.println(lastWebMosdis);
    pendingMosdisChange = false;
  }

  // Always update config to reflect the actual BMS state for display
  config.moschg = bms.get_charge_mosfet_status();
  config.mosdis = bms.get_discharge_mosfet_status();

  if (config.batt) {
    config.soc = bms.get_state_of_charge();
    config.volt = bms.get_voltage() * 1000; // Convert to mV
    config.temp = bms.get_ntc_temperature(0); // Assuming first NTC is the main temperature sensor
    config.chgruntime = charge_runtime_minutes;
    config.disruntime = runtime_minutes;
  } else {
    // intentionally unchanged: your original code left these commented out
  }
}

