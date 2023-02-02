cmake_minimum_required(VERSION 3.20.0)

# Setting up default configuration
set($ENV{Zephyr_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/)
set($ENV{ZEPHYR_BASE} ${CMAKE_CURRENT_SOURCE_DIR}/)
set(ZEPHYR_TOOLCHAIN_VARIANT cross-compile)
set(CROSS_COMPILE /usr/bin/arm-none-eabi-)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crazyradio2)

target_sources(app PRIVATE src/main.c src/esb.c src/crusb.c src/led.c src/rpc.c src/radio_mode.c src/api.c src/oryx.c src/oryx_ll.c src/fem.c src/contwave.c src/button.c)
