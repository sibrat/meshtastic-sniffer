#!/bin/sh

echo -e '{"jsonrpc": "2.0", "method": "setup_wifi", "params": {"ssid": "meshtastic", "pass": "meshpassword"}, "id":1}\r\n' > /dev/ttyUSB0 
