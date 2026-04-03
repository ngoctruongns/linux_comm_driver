#!/bin/bash

echo "Delete remap the device serial port(ttyACMX) to  mcu_serial"
echo "sudo rm   /etc/udev/rules.d/arduino.rules"
sudo rm /etc/udev/rules.d/70-arduino.rules
sudo rm /etc/udev/rules.d/70-raspberry.rules
echo " "
echo "Restarting udev"
echo ""
sudo udevadm control --reload-rules
sudo service udev restart
sudo udevadm trigger
echo "finish  delete"
