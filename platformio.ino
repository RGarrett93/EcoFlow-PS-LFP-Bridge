; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:lilygo_tcan485]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200
lib_deps =
  esp32async/ESPAsyncWebServer @ ^3.7.10
  knolleary/PubSubClient @ ^2.8
;upload_protocol = espota
;upload_port = 192.168.XXX.XXX
