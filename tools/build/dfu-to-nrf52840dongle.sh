#!/usr/bin/env bash

nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
                     --application build/zephyr/zephyr.hex \
                     --application-version 1 build/zephyr/zephyr.zip

echo Using NRF DFU bootloader on /dev/ttyACM0

nrfutil dfu usb-serial -pkg build/zephyr/zephyr.zip -p /dev/ttyACM0