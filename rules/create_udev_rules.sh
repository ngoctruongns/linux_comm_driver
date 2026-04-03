#!/bin/bash

echo "Remap the device serial port(ttyACMx) to  mcu_serial"
echo "Arduino usb connection as /dev/mcu_serial , check it using the command : ls -l /dev|grep ttyACM"
echo "start copy raspberry_pi.rules and arduino rules to /etc/udev/rules.d/"
sudo cp ./70-raspberry.rules /etc/udev/rules.d/
sudo cp ./70-arduino.rules /etc/udev/rules.d/
echo " "
echo "Restarting udev"
echo ""
sudo udevadm control --reload-rules
sudo service udev restart
sudo udevadm trigger
echo "finish "
