#!/bin/bash

# pass the 'f' parameter to reflash the CH4552T board

if [ "$1" == "f" ]; then
	sudo ./prog_pc -boot
	sleep 1
	lsusb
	make flash
else
	make
fi