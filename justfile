# This message
help:
    @just --list

sdk_version := "0.17.2"

# Download and install Zephyr SDK in ~/.local
[confirm("Download and install Zephyr SDK in ~/.local?")]
fetch-zephyr-sdk:
    mkdir -p $HOME/.local
    curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v{{sdk_version}}/zephyr-sdk-{{sdk_version}}_{{os()}}-{{arch()}}_minimal.tar.xz | tar xJ -C $HOME/.local/
    curl -L https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v{{sdk_version}}/toolchain_{{os()}}-{{arch()}}_arm-zephyr-eabi.tar.xz | tar xJ -C $HOME/.local/zephyr-sdk-{{sdk_version}}/

# Download Zephyr project and fetch python dependencies
fetch-zephyr:
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


# Fech python dependencies
fetch-python-dep:
    uv pip install -r zephyr/scripts/requirements.txt

# Build Crazyradio2 firmware
build:
    west build -b crazyradio2
