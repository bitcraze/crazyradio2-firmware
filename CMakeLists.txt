cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crazyradio2)

target_sources(app PRIVATE src/main.c src/esb.c src/crusb.c src/led.c src/rpc.c src/radio_mode.c src/api.c src/fem.c src/contwave.c src/button.c src/system.c src/power_measurement.c src/legacy_usb.c)
