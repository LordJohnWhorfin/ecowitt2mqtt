# ecowitt2mqtt
Daemon to extract local Ecowitt weather gateway data and publish it to an MQTT broker.

This small daemon is written in simple C and runs on Debian and probably most other Linux distributions. It only requires the libmqtt library (libmqtt-dev package on Debian).
It is configured to be installed in /usr/local/bin and reads its configuration from /etc/ecowitt2mqtt.conf

# Building
Add package libmqtt-dev and run make.

# Testing
You can run the daemon in foreground mode using the --foreground option. There is a more talkative verbose mode accessible with --verbose.

# Installing
sudo cp ecowitt2mqtt.c /usr/local/bin/
Edit ecowitt2mqtt.conf to match your environment (ecowitt gateway IP address, mqtt broker IP address, etc...). If your MQTT broker requires authentication... Submit a PR :-)
sudo cp ecowitt2mqtt.conf /etc/
sudo cp ecowitt2mqtt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ecowitt2mqtt.service
sudo systemctl start ecowitt2mqtt.service

Verify the system is running correctly:
sudo systemctl status ecowitt2mqtt.service

# Topics
The daemon publishes the data it reads using the main root topic indicated, by default ecowitt. Sensor data may be split by type, for instance temperature from
temperature & humidity, temperature only, and soil temperature sensors all appear under ecowitt/temperature; temperature from temperature & humidity sensor #1 will be
published under topic ecowitt/temperature/th_1 and humidity under ecowitt/humidity/th_1 (if available)

You can query all recent data by publishing the message "json" on topic ecowitt/all_data/request. The daemon will respond with a json message on topic ecowitt/all_data/json
containing all the current data.
The binary gateway reply is also available (mostly for debugging purposes) by sending "raw" instead of "json"

# Units
All units are SI (temperatures in C, pressure in hPa...) Humidity is in percent units.
