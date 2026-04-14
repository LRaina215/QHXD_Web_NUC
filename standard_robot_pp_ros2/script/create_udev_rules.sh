#!/bin/bash

set -e

echo ""
echo "This script copies a udev rule to /etc/udev/rules.d to facilitate bringing up the C board usb connection as /dev/ttyCBoard"
echo ""

UDEV_RULES_FILE="/etc/udev/rules.d/99-RoboMaster_C_Board.rules"
UDEV_RULE='SUBSYSTEM=="tty", KERNEL=="ttyACM*", ATTRS{idVendor}=="0ffe", ATTRS{idProduct}=="0001", ATTRS{serial}=="32021919830108", SYMLINK+="ttyCBoard", GROUP="dialout", MODE="0666"'

echo "Setting udev rules..."
echo "$UDEV_RULE" | sudo tee "$UDEV_RULES_FILE" > /dev/null

echo "Restarting udev..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo -e "\e[32mUdev rules have been set and restarted successfully.\e[0m"

CURRENT_USER="${SUDO_USER:-$(whoami)}"

echo ""
echo "Adding user $CURRENT_USER to dialout group..."
sudo usermod -aG dialout "$CURRENT_USER"

echo -e "\e[32mUser '$CURRENT_USER' has been added to the dialout group successfully\e[0m"
