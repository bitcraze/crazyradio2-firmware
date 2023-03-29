---
title: Building the firmware
page_id: build
---

This project can be built using the [Toolbelt](https://github.com/bitcraze/toolbelt) or with natively installed tools.

## Using the Toolbelt

Make sure the [Toolbelt](https://github.com/bitcraze/toolbelt) is
[installed](https://www.bitcraze.io/documentation/repository/toolbelt/master/installation/)

From a terminal, run

To build the firmware with the Crazyradio 2.0 USB protocol:

```bash
tb build
```

To build with the legacy (ie Crazyradio-PA emulation) protocol;

```bash
tb build-legacy
```

The firmware, ready to be drag-and-dropped in the bootloader drive will be in `build/zephyr/crazyradio2.uf2`.

## Using natively installed tools

### Build dependencies

This firmware is built on Zephyr RTOS so it should compile on all platform supported by zephyr.
You can follow the [Zephyr getting started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) to install the required dependencies.

Alternatively, the following commands allows to install dependencies on a recent Ubuntu linux.
The procedure can be easily adapted to work on any modern linux distribution (including in Windows's WSL).

This procedure will install a lot of python packages so you may want to run in a [python venv](https://docs.python.org/3/library/venv.html).

```bash
# Build dependencies
sudo apt install python3-pip cmake curl ninja-build
pip install west

# installing Zephyr SDK for ARM only in ~/.local. See Zephyr documentation for other possible location.
mkdir -p $HOME/.local
curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.0/zephyr-sdk-0.16.0_linux-x86_64_minimal.tar.xz | tar xJ -C $HOME/.local/
curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.0/toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz | tar xJ -C $HOME/.local/zephyr-sdk-0.16.0/

# To run in the project folder to pull zephyr and all required libraries
tools/build/fetch_dependencies
pip install -r zephyr/scripts/requirements.txt
```

### Builing, flashing

To build the firmware:
```bash
west build -b bitcraze_crazyradio_2 
```

To build the legacy USB protocol (Crazyradio-PA compatible) firmware:
```bash
west build -b bitcraze_crazyradio_2 -- -DCONFIG_LEGACY_USB_PROTOCOL=y
```

All build artifacts and configuration are in the `build` folder so to clean the build one can simply remove this folder with `rm -r build`.

After the first build, the firmware can simply be re-built with
```bash
west build
```

And flashed using a SWD debug probe (like old ST-LinkV2, JLink or CMSIS-DAP probes):
```bash
west flash
```
