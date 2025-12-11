# EcoFlow PS LFP Bridge

ESP32-based bridge that lets you connect a **generic LiFePO₄ pack with a JBD/Overkill BMS** to an **EcoFlow PowerStream** via CAN, and emulate a **Delta 2 battery** on the PowerStream’s battery port.

In short:  
**Any JBD LiFePO₄ pack → ESP32 LilyGO TCAN485 → EcoFlow PowerStream (thinks it's a Delta 2).**

This allows:

- PowerStream to **charge the external pack from PV**  
- PowerStream to **discharge from the pack into your AC loads**, just like with a real EcoFlow battery.

> ⚠️ **High-level hacky project for tinkerers. Not affiliated with or endorsed by EcoFlow.  
> Use at your own risk. You are responsible for your own safety and compliance.**

## Thanks

Many thanks to [@bulldog5046](https://github.com/bulldog5046) for kick-starting all of this - go look at the extensive details in  
[EcoFlow-CanBus-Reverse-Engineering](https://github.com/bulldog5046/EcoFlow-CanBus-Reverse-Engineering).

This project would have been considerably more difficult without the work done by [@stewartoallen](https://github.com/stewartoallen) on  
[wattzup](https://github.com/GridSpace/wattzup), which was invaluable for understanding and validating EcoFlow CAN behaviour.

## Hardware Required

* LilyGO T-CAN485 ESP32 board
* EcoFlow PowerStream
* LiFePO₄ battery with JBD / Overkill Solar BMS
* Suitable wiring and connectors (PS Battery Cable)

## WiFi Behaviour

- On boot:
  - Tries saved WiFi credentials (if any)
  - If connect fails or none saved → starts AP:
    - SSID: `EcoFlowBridge-XXXX` (XXXX = last 4 hex characters of MAC/deviceId)
    - AP IP: `192.168.4.1`
- Web UI available on AP IP or WiFi IP
- WiFi configuration stored

## MQTT & Home Assistant

- Configurable MQTT:
  - Host / port / user / password / base topic (default `ecoflow_bridge`)
  - Enable/disable MQTT
- Uses **Home Assistant MQTT Discovery**

<img width="1893" height="942" alt="image" src="https://github.com/user-attachments/assets/854715c0-1d1e-4377-8843-c59026121f3d" />
