---
title: Building the firmware
page_id: build
---

This project is a Zephyr app. If you already have all dependencies installed it can be compiled with `west build -b crazyradio2`.
The rest of this page documents how to setup your development environment.

This project uses [just](https://github.com/casey/just?tab=readme-ov-file#just) to simplify running commands. It can either be installed in your system or run with `uv run just`. Any commands can be run manually as well, look in the `justfile` at the root of the repository for the commands

## Host dependencies

This project can be compiled on Linux and MacOS. Compiling on Windows is not supported at the moment.

The minimum required tools are [uv](https://docs.astral.sh/uv/) and `git`. It can be installed either from UV's webpage or from your package manager. On Ubuntu with:
```
apt install uv
```

and on MacOS with Brew:
```
brew install uv
```

The project also makes use of `just` and `cmake` as native dependencies, they are part of the UV venv but they can also be installed on the host with the package manager (apt or brew)

## Entering build environment

Zephyr is built with `west` which is a Python tool and uses many Python dependencies. Hence it is advised to work in a python venv.

To set up the venv and install build dependencies:
``` bash
uv venv
source ./.venv/bin/activate
uv pip install .
```

Each subsequent time, to enter the venv to work with the project:
``` bash
source ./.venv/bin/activate
```

## Toolchain

The Zephyr sdk is required. If you do not already have it. You can either install it manually, or this command can be used to install the required sdk only (for arm) in `~/.local/`:

``` bash
just fetch-zephyr-sdk
```

### Fetching zephyr and installing dependencies

Before being able to build the project, Zephyr and its python dependencies must be fetched:

``` bash
just fetch-zephyr
```

### Building, flashing

To build the firmware:
```bash
west build -b crazyradio2
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
