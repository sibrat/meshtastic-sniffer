# meshtastic-sniffer
small LoRa sniffer for meshtastic networks
This is arduino project in it's very early stage.

This small progect aims to assist those who need tools for administrating or debugging local meshtastic networks. Unlike similar projects, this software collects all Meshtastic packets, including those with bad checksums, directly from the air and sends them for further analysis. It also has the ability to proxy packets (only the good ones) from the air directly to MQTT server without any interference from meshtastic firmware.

# compilation
Use Arduino IDE.

Select your board before compilation in first line in file meshtastic-sniffer.ino

For successful compilation following libraries are required:
LoRaRF by Chandra Wijaya Sentosa (https://github.com/chandrawi/LoRaRF-Arduino)
ArduinoJson by Benoit Blanchon v7+ (https://arduinojson.org/)
ConfigStorage by Tost69 (https://github.com/tost69/ConfigStorage)
base64 by Densaugeo (https://github.com/Densaugeo/base64_arduino)
lwmqtt by Joël Gähwiler (https://github.com/256dpi/arduino-mqtt)
and 
nanoPB (https://github.com/nanopb/nanopb)
which needs to be installed manually from tar.gz release (nanopb-0.4.9.1.tar.gz) to Arduino libraries directory.

Additional boards manager url:
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Supported boards:

TTGO
board=TTGO LoRa32-OLED

HELTECV2
board=Heltec WiFi LoRa 32(V2)

DIYV1
board=uPesy ESP32 Wroom DevKit

# configuration

Set serial port to 115200 bod, use both NL & CR.
To configure WiFi, LoRa and MQTT settings send following JSON messages to serial console (via Serial Monitor in Arduino IDE for example):

{"jsonrpc": "2.0", "method": "setup_wifi", "params": {"ssid": "yourSSID", "pass": "yourPassword"}, "id":1}
{"jsonrpc": "2.0", "method": "setup_lora", "params":{"freq": "868825000", "SF": "9", "BW": "250000", "CR": "5"},"id": 1}
{"jsonrpc": "2.0", "method": "setup_mqtt", "params":{"rootTopic": "yourRootTopic", "channel": "MediumFast", "host": "mymqttserver.lan", "mqttUser": "testuser", "passwrd": "testpassword"},"id": 2

current configuration and status can be readed using following commands:
{"jsonrpc": "2.0", "method": "get_wifi", "id": 1}
{"jsonrpc": "2.0", "method": "get_ip", "id": 1}
{"jsonrpc": "2.0", "method": "get_mac", "id": 1}
{"jsonrpc": "2.0", "method": "get_lora", "id": 1}
{"jsonrpc": "2.0", "method": "get_mqtt", "id": 1}
