---
title: Flashing
page_id: flash
---

The Crazyradio 2.0 is flashed via USB. When the radio is in bootloader mode it will appear as a USB-drive, flashing is
simply done by dragging-and-dropping a new firmware file to the drive.

## Bootloader mode

To enter bootloader mode, press and hold the button on the Crazyradio 2.0 while you plug it in. The LED on the
Crazyradio 2.0 should turn red and slowly pulsate. The Crazyradio 2.0 will now behave like a USB-drive and new
drive named `Crazyradio2` should appear in your file browser containing a few files.

The file `CURRENT.UF2` contains the current versions of the firmware that is installed. If you want to make a backup
before you flash new firmware to the device, you can simply drag-and-drop `CURRENT.UF2` to some other location.

## Flashing

To flash new firmware to the Crazyradio, just drag-and-drop the new firmware (a .UF2 file) to the `Crazyradio2` drive.
The new file will be written to the flash memory, and the Crazyradio 2.0 will restart, running the new firmware.
Writing the firmware takes a second or two and when the Crazyradio 2.0 restarts, the LED will no longer be red and the
USB-drive will disappear.

You may get an error message in your file browser as the Crazyradio 2.0 exited the bootloader mode and the
`Crazyradio2` drive is no longer available.

Note: if you enter bootloader mode again the current firmware will always be named `CURRENT.UF2`, regardless of the
name of the file you dropped when flashing.

## Flashing with USB serial port

The bootloader also contains a (CDC) USB serial port interface. This allows to flash the firmware from command line
or from a script which might be helpful during development.

Flashing using the serial port interface requires the `adafruit-nrfutil` command line tool. This tool can be installed
with `pip install adafruit-nrfutil`.

Then, from within the firmware build folder, a package can be generated and flashed:

```
adafruit-nrfutil  dfu genpkg --dev-type 0x0052 --application build/zephyr/crazyradio2.hex crazyradio2.zip
adafruit-nrfutil  dfu serial --package crazyradio2.zip -p /dev/ttyACM0 -b 115200
```

In those commands, `/dev/ttyACM0` is the serial port of the Crazyradio bootloader.
