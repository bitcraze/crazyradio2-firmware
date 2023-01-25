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

Crazyradio 2.0 implement exclusives radio mode: only one mode can be enabled at
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
with the nRF24 line of radio tranceiver from Nordic semiconductor. The
implementation only handles the PRX mode of ESB. This protocol is used to
communicate with the Crazyradio 2.x bootloader as well as its firmware.

### esb.sendPacket

Send a packet, wait for the ack and returns the status and payload of the ack.

 - Request param: `[channel(uint), address(bytes), payload(bytes)]`
 - Response result: `[acked(bool), ackPayload(bytes), rssi(int)]`

Parameters:
 - **channel**: Radio channel to send/receive the packet to/from. Must be between 0 and 100. Channel 0 corresponds to 2400MHz and 100 to 2500MHz.
 - **address**: ESB link address as a byte string. The lenght of the address must be 5 bytes.
 - **Payload**: Payload as a byte string. Length of the payload can be from 0 to 32 bytes.

Result:
 - **acked**: True if an acknoledgement packet has been received.
 - **ackPayload**: Payload received in the ack packer, as a byte string.
 - **rssi**: Receive power as a positive integer. 25 means -25dBm. Valid range from the measurement is -90 to -20 dBm (values from 20 to 90).

Can return the following errors:
 - `"NotInitialized"`: ESB module not enabled in the [radio mode](#radio-mode-switch) module.
 - `"BadRequest"`: Wrong format of the request param. This includes wrong CBOR format as well as the following condition:
    - Channel is not between 0 and 100 inclusive
    - Address is not 5 byte long
    - Payload is more than 32 bytes long

## Oryx radio

### oryx.scan

Oryx scans for Oryx targets and returns a list of targets.

 - Request param: `[channel(uint), timeout_ms(uint)]`
 - Response result: `{mac_address(string): {'rssi':rssi(int)}, ... }`

Parameters:
 - **channel**: Radio channel to scan. Must be between 0 and 100. Channel 0 corresponds to 2400MHz and 100 to 2500MHz.
 - **timeout_ms**: The scanning timeout in miliseconds.

Result:
 - **mac_addresses**: A map of mac-addresses as a key and information per target.

Can return the following errors:
 - `"NotInitialized"`: Oryx module not enabled in the [radio mode](#radio-mode-switch) module.
 - `"BadRequest"`: Wrong format of the request param. This includes wrong CBOR format as well as the following condition:
    - Channel is not between 0 and 100 inclusive


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
