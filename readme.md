Shutter Basic
=============

A Basic system to control a shutter via esp8266 and mqtt.
The power consumption is minimal. It only wakes up for several seconds
after two minutes to check the mqtt server for new messages.

Wifi setup is done via wps and will be written to eeprom.
Basic commands can be done via Mqtt or via button.
It is also possible to reset eeprom via button.
If relay is triggerd, a sensor tracks the power consumptions\
and disables the relay if the senor tracks nothing.
Every other Status will be send via MQTT (if possible) and\
will be displayed on a small oled display.

If debugmode is true. There will be additional informations written to\
serial.
