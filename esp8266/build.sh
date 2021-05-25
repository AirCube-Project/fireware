#!/bin/bash
PATH=/home/dzolotov/go/bin/:/home/dzolotov/avr-gcc-11.1.0-x64-linux/bin:$PATH tinygo build -target arduino-nano -o main.hex --opt 2 esp.go
PATH=/home/dzolotov/go/bin/:/home/dzolotov/avr-gcc-11.1.0-x64-linux/bin:$PATH avrdude -c arduino -p atmega328p -b 115200 -P /dev/ttyUSB0 -U flash:w:main.hex:i
