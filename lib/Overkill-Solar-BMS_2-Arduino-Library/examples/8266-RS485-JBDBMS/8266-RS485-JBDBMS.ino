///TESTED ON NODEMCU 8266 (ESP-12E)
//
//              +-----------------+
//              |0               0|
//              |    +-------+    |
//              |    |       |    *
//              |    |       |    *
//              |    |       |    *
//              |    |       |    *
//              |    +-------+    *
//              |                 * - 3v3
//              |   [NODEMCU v3]  * - gnd                 +---------------------------------------+
//              |              D5 * ---> [GPIO14]    ---> |*RO                                    |
//              |              D6 * ---> [GPIO12]    ---> |*RE              [MAX485]            O | ---> RS485 line B ---> to JBD BMS
//              |              D7 * ---> [GPIO13]    ---> |*DE              [board ]            O | ---> RS485 line A ---> to JBD BMS
//              |              D8 * ---> [GPIO15]    ---> |*DI                                    |
//              |              RX * - gpio3               +---------------------------------------+
//              |              TX * - gpio1
//              |                 * - gnd
//              |                 * - 3v3
//              |                 |
//              |O               O|
//              +-----------------+
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include<SoftwareSerial.h>
#include <bms2.h>


#define MAX485_RO   14      // The RX signal pin
////////////////////////////////////////////////////////////
#define MAX485_DE   12      // RE and DE pins was sending a same logical signals
#define MAX485_RE   13      // what switching the MAX485-chip between "receiving" and "transmission" sessions
////////////////////////////////////////////////////////////
#define MAX485_DI   15      // The TX signal pin

SoftwareSerial rs485(MAX485_RO, MAX485_DI); // RX, TX
OverkillSolarBms2 bms = OverkillSolarBms2();

unsigned long timer = millis();

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
////  This "piece-of-code" was copied from scatch for "ModbusMaster" library
////  https://github.com/4-20ma/ModbusMaster/blob/master/examples/RS485_HalfDuplex/RS485_HalfDuplex.ino
////  He is declared two functions, that switching the "RX/TX" session
/////////////////////////////////////////////////////////////
void preTransmission()
{
  digitalWrite(MAX485_RE, 1);
  digitalWrite(MAX485_DE, 1);
}

void postTransmission()
{
  digitalWrite(MAX485_RE, 0);
  digitalWrite(MAX485_DE, 0);
}
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////


void setup() {

  pinMode(MAX485_RE, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  preTransmission();
  postTransmission();

  Serial.begin(115200);

  rs485.begin(9600);
  while (!rs485) {  // Wait for the BMS serial port to initialize
  }
  bms.begin(&rs485);
  ////  The functions, that was declared before, are sending to library
  ////  for automatic switching between "RX/TX" states
  bms.preTransmission(preTransmission);
  bms.postTransmission(postTransmission);

  bms.main_task(true);


}

void loop() {

  if (millis() - timer > 3000) {
    timer = millis();
    bms.main_task(true);

    Serial.println("***********************************************");
    Serial.print("State of charge:\t"); Serial.print(bms.get_state_of_charge()); Serial.println("\t% ");
    Serial.print("Current:\t\t"); Serial.print(bms.get_current()); Serial.println("\tA  ");
    Serial.print("Voltage:\t\t"); Serial.print(bms.get_voltage()); Serial.println("\tV  ");


    
    for (uint8_t i = 0; i < bms.get_num_cells(); i++) {
      Serial.print((String)"Cell " + (i + 1) + " -\t\t"); Serial.print(bms.get_cell_voltage(i), 3); Serial.print("\tV\t"); Serial.println(bms.get_balance_status(i) ? "(balancing)" : "(not balancing)");
    }
    //      bms.query_0x03_basic_info();

    //
    Serial.print("Balance capacity:\t"); Serial.print(bms.get_balance_capacity()); Serial.println("\tAh  ");
    Serial.print("Rate capacity:\t\t"); Serial.print(bms.get_rate_capacity()); Serial.println("\tAh  ");


    for (uint8_t i = 0; i < bms.get_num_ntcs(); i++) {
      Serial.print((String)"Termometer " + (i + 1) + " -\t\t"); Serial.print(bms.get_ntc_temperature(i)); Serial.println("\tdeg.\t");
    }
    Serial.print((String)"Charge mosfet" + " -\t\t"); bms.get_charge_mosfet_status() ? Serial.print("Enabled") : Serial.print("Disabled"); Serial.println("");
    Serial.print((String)"Discharge mosfet" + " -\t"); bms.get_discharge_mosfet_status() ? Serial.print("Enabled") : Serial.print("Disabled"); Serial.println("");

    Serial.print((String)"Cycle count" + " -\t\t"); Serial.print(bms.get_cycle_count()); Serial.println("");
    Serial.print((String)"protection_status" + " -\t"); Serial.print(bms.get_protection_status_summary()); Serial.println("");
    Serial.print((String)"fault_count" + " -\t\t"); Serial.print(bms.get_fault_count()); Serial.println("");
    Serial.print((String)"get_bms_name" + " -\t\t"); Serial.print(bms.get_bms_name()); Serial.println("");

    // Serial.println(ESP.getFreeHeap());
    //
    //    bms.set_0xE1_mosfet_control(1,1);
    //    bms.set_0xE1_mosfet_control_charge(1);
    //    bms.set_0xE1_mosfet_control_discharge(1);
  }

}
