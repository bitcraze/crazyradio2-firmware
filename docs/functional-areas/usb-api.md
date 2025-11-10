---
title: Crazyradio 2.0 USB API
page_id: usb_api
---

Crazyradio 2.0 exposes an API over [CBOR RPC](CBOR-RPC.md) over [usb](usb-protocol.md).
This document lists the implemented functions and their usage.

The notation of CBOR object in this document is based on the
[CBOR diagnostic notation](https://www.rfc-editor.org/rfc/rfc8949.html#name-diagnostic-notation)
with the addition of named parameter being written as `name(type)`.

## General functions

### version

This function returns the version of the various part of the firmware.

 - Request param: `null`
 - Response result: `{'zephyr': [major(int), minor(int), patch(int)]}`

Never returns an error and does not check the params.

## Radio mode switch

Crazyradio 2.0 implements exclusive radio modes: only one mode can be enabled at
any one time. The radioMode module is responsible for managing the modes.

The currently supported modes are:

| Mode    | Description                   |
|---------|-------------------------------|
| disable | Disable the radio hardware    |
| esb     | nRF24 PTX ESB compatible mode |

### radioMode.list

Returns a list of the supported radio modes.

 - Request param: `null`
 - Response result: `['disable', mode(string) ...]`

Never returns an error and does not check the params.

### radiomode.get

Returns the currently active mode.

 - Request param: `null`
 - Response result: `mode(string)`

Never returns an error and does not check the params.

### radiomode.set

Set the active mode. If the mode is changed, this function will disable the
currently active mode and enable the new mode.

 - Request param: `mode(string)`
 - Response result: `null`

Can return the following error:

 - `"BadRequest"`: The param is not a string
 - `"NotFound"`: Mode requested not found

## ESB radio

The "Enhanced ShortBurst" radio module implements a radio protocol compatible
with the nRF24 line of radio transceivers from Nordic Semiconductor. The
implementation only handles the PRX mode of ESB. This protocol is used to
communicate with the Crazyradio 2.x bootloader as well as its firmware.

### esb.sendPacket

Send a packet, wait for the ack and returns the status and payload of the ack.

 - Request param: `[channel(uint), address(bytes), payload(bytes)]`
 - Response result: `[acked(bool), ackPayload(bytes), rssi(int)]`

Parameters:
 - **channel**: Radio channel to send/receive the packet to/from. Must be between 0 and 100. Channel 0 corresponds to 2400MHz and 100 to 2500MHz.
 - **address**: ESB link address as a byte string. The length of the address must be 5 bytes.
 - **Payload**: Payload as a byte string. Length of the payload can be from 0 to 32 bytes.

Result:
 - **acked**: True if an acknowledgement packet has been received.
 - **ackPayload**: Payload received in the ack packet, as a byte string.
 - **rssi**: Receive power as a positive integer. 25 means -25dBm. Valid range from the measurement is -90 to -20 dBm (values from 20 to 90).

Can return the following errors:
 - `"NotInitialized"`: ESB module not enabled in the [radio mode](#radio-mode-switch) module.
 - `"BadRequest"`: Wrong format of the request param. This includes wrong CBOR format as well as the following condition:
    - Channel is not between 0 and 100 inclusive
    - Address is not 5 bytes long
    - Payload is more than 32 bytes long

## Continuous wave

The continuous wave is a test mode that makes the radio transmit a continuous carrier wave at the center frequency of a channel.
It is intended to be used to test the radio during design and production.

### contWave.start

Start the continuous wave.

 - Request parameters: `[channel(uint), power(int)]`
 - Response result: `null`

Parameters:
 - **channel**: Channel to setup the radio on. The continuous wave will be transmitted on 2400+channel MHz.
 - **Power**: Output power to setup the power amplifier. Value in dBm. This value is not currently used by the firmware, the power is currently +20dBm.

Can return the following errors:
 - `"NotInitialized"`: contWave module not enabled in the [radio mode](#radio-mode-switch) module.
 - `"BadRequest"`: Wrong format of the request param. This includes wrong CBOR format as well as the following condition:
    - Channel is not between 0 and 100 inclusive

### contWave.stop

Stop transmitting the continuous wave

 - Request param: `null`
 - Response result: `null`

Can return the following errors:
 - `"NotInitialized"`: contWave module not enabled in the [radio mode](#radio-mode-switch) module.

## Radio power measurement

The `powerMeasurement` radio mode allows measuring the amount of radio power seen on one channel.
It needs to be enabled as a radio mode before being used.

### powerMeasurement.measure_channel

Measure the radio power on a channel and return it

 - Request parameters: `[channel(uint)]`
 - Response result: `rssi(int)`

Parameters:
 - **channel**: Channel to measure on. The measurement will be carried on 2400+channel MHz.

Result:
 - **rssi**: Receive power as reported by the radio hardware.
             Needs to be negated to get the rssi in dBm. 
             For example `48` represents `-48 dBm`.
             The valid range of measurement is 20 to 90 (i.e. -90 dBm to -20 dBm).

Can return the following errors:
 - `"NotInitialized"`: powerMeasurement module not enabled in the [radio mode](#radio-mode-switch) module.
 - `"BadRequest"`: Wrong format of the request param. This includes wrong CBOR format as well as the following condition:
    - Channel is not between 0 and 100 inclusive

**Known bugs**: Currently only one measurement can be carried out. The radio mode has to be switched to `esb` and back to `powerMeasurement` in order to measure again otherwise a bogus value will be returned.

## Led control

The RGB LED state can be controlled from the USB API.
The LED will also be controlled by the firmware so this is mostly intended as a debug functionality:
depending of the radio state there is no guarantee the LED state set with this API will stay.

### led.set

Set LED status

 - Request parameters: `[red(bool), green(bool), blue(bool)]`
 - Response result: `null`

Parameters:
 - **red**: `true` to switch ON the red LED, `false` otherwise.
 - **green**: `true` to switch ON the green LED, `false` otherwise.
 - **blue**: `true` to switch ON the blue LED, `false` otherwise.

Can return the following errors:
 - `"Invalid parameter"`: Wrong format of the request param.

## Button control

The button state can be read from the API.

### button.get

Read the instant button state.

 - Request parameters: `null`
 - Response result: `state(bool)`

Result:
 - **state**: `true` if the button is currently pressed, `false` otherwise.

## System functions

### system.get_vcc

Measure the voltage of the radio main VCC power supply.
Crazyradio 2.0 has a nominal VCC of 3.15V

 - Request parameters: `null`
 - Response result: `vcc(float)`

Result:
 - **vcc**: Voltage, in volt, of the VCC power supply.

### system.test_ios

Manufacturing test of the IO lines. This function execute the following algorithm:

 1. Setup IO2 and IO3 as output, IO1 as analog input.
 2. Set IO2=1 and IO3=0
 3. Wait 100ms
 4. Measure IO1 voltage and save it in `io1_0`
 5. Set IO2=0 and IO3=1
 6. Wait 100ms
 7. Measure IO1 voltage and save it in `io1_1`

 - Request parameters: `null`
 - Response result: `[io1_0(float), io1_1(float)]`

Result:
 - **io1_0**: Voltage, in volt, of `io1_0`.
 - **io1_1**: Voltage, in volt, of `io1_1`.
