# This message
help:
    @just --list

sdk_version := "0.17.2"

# Fetch system dependencies with apt or dnf. Works on fedora:43 and ubuntu:24 official containers.
fetch-system-dep:
	grep "NAME=\"Fedora Linux\"" < /etc/os-release && sudo dnf install -y uv just libusb1-devel libudev-devel git gcc
	uv python install 3.14


# Download and install Zephyr SDK in ~/.local
[confirm("Download and install Zephyr SDK in ~/.local?")]
fetch-zephyr-sdk:
    mkdir -p $HOME/.local
    curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v{{sdk_version}}/zephyr-sdk-{{sdk_version}}_{{os()}}-{{arch()}}_minimal.tar.xz | tar xJ -C $HOME/.local/
    curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v{{sdk_version}}/toolchain_{{os()}}-{{arch()}}_arm-zephyr-eabi.tar.xz | tar xJ -C $HOME/.local/zephyr-sdk-{{sdk_version}}/

# Download Zephyr project and fetch python dependencies
fetch-zephyr: west-exists
    #!/usr/bin/env bash
    set -e
    # Test if the .west folder exists
    if [ ! -d .west ]; then
        # If not, then initialize the west project
        west init -l tools
    fi

    # Always update the west project
    west update

    just fetch-python-dep


# Fetch python dependencies
fetch-python-dep:
    uv pip install -r zephyr/scripts/requirements.txt
    uv pip install cmake ninja

# Prepare system to build the project. Alias for [fetch-zephyr fetch-zephyr-sdk fetch-python-dep]
prepare-system:
	just fetch-zephyr
	[ -d $HOME/.local/zephyr-sdk-{{sdk_version}} ] || just --yes fetch-zephyr-sdk
	just fetch-python-dep

# Build Crazyradio2 firmware
build: west-exists
    west build -b crazyradio2

# Flash and reset Crazyradio to firmware mode using probe-rs
flash: west-exists
    west flash
    sleep 1
    probe-rs reset --chip nrf52840_xxAA

rtt: flash
    west rtt

west-exists:
	@type west &> /dev/null || (echo "West not found, please enter the venv with '. enter.sh'" && exit 1)
