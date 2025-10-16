#!/bin/bash

echo "Delete remap the device serial port(ttyACMX) to  arduino_uno"
echo "sudo rm   /etc/udev/rules.d/arduino.rules"
sudo rm /etc/udev/rules.d/70-arduino.rules
echo " "
echo "Restarting udev"
echo ""
sudo udevadm control --reload-rules
sudo service udev restart
sudo udevadm trigger
echo "finish  delete"
