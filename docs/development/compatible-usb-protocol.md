---
title: Crazyradio PA compatible USB and Radio protocol
page_id: compatible_usb_radio_protocol
---

Crazyradio 2.0 implements a USB and radio protocol that is a strict
superset of the Crazyradio PA USB protocol. When using this protocol
Crazyradio 2.0 is compatible with all software that supports Crazyradio
PA in the context of controlling a Crazyflie.

The radio communication is done using the Nordic \"Enhanced
ShockBurst™\" packet protocol in PTX mode with acknowledge. Variable
sized packet, from 1 to 32 bytes, can be send and acknowledged by the
copter. The acknowledgement packet can contain a payload from 0 to 32
Bytes.

This page documents the protocol used in version 5.0 of the Crazyradio 2.0
firmware.

Radio configuration
-------------------

Crazyradio is configured in PTX mode. It can communicate with Nordic
chips compatible with the nrf24L family (at least nrf24L01p, nRF51 and
nRF52 tested). In order to communicate with the Crazyradio the target
has to be configure correctly:

-   PRX mode
-   One active pipe with the address configured in the Crazyradio
    dongle, by default it is 0xE7E7E7E7E7
-   5 byte address
-   Dynamic payload length enable
-   Payload with ack enable

The Crazyflie is already configured that way. The relevant source code
can be seen in
[radiolink.c](https://github.com/bitcraze/crazyflie-firmware/blob/02a2b6d48800334ed455144d9061226418fab7b7/hal/src/radiolink.c#L163).

USB protocol
------------

The USB devices has the VID/PID couple 0x1915/0x7777.

|  ----------- | ---------| --------------------------------------------------------|
|  EP0         | Control |  Control endpoint. Used to configure the dongle |
|  EP1IN/OUT   | Bulk    |  Data endpoints. Used to send and receive radio packets |


### Data transfer

When the radio dongle is configured to use PTX (emitter) mode it sends a
packet to the copter and waits for the acknowledge. The acknowledge can
contain a payload which is the mean to receive data. If the auto
acknowledge is disabled there is no IN transaction.

To send a packet, the following sequence must be followed:

-   Send the packet to EP1\_OUT. Its length should be between 1 to 32
    Bytes. If Inline mode is enabled, the packet payload is prepended by
    radio settings (see [Inline setting mode](#inline-settings-mode))
-   Read the ACK from EP1\_IN. The first byte is the transfer status and
    the following bytes are the content of the ACK payload, if any.

 ![usb protocol](/docs/images/usbprotocol.png)

The status byte contains flags indicating the quality of the link:

|  Status Bit  | Role			   |
|  ------------| --------------------------|
|  4..7        | Number of retransmission  |
|  2..3        | Reserved |
|  1           | Power detector |
|  0           | ACK received |

### Dongle configuration and functions summary

Crazyradio vendor requests summary:

|  bmRequestType  | bRequest                               | wValue     | wIndex  | wLength  | data|
|  ---------------| ---------------------------------------| -----------| --------| ---------| ---------|
|  0x40           | SET\_RADIO\_CHANNEL (0x01)             | channel    | Zero    | Zero     | None|
|  0x40           | SET\_RADIO\_ADDRESS (0x02)             | Zero       | Zero    | 5        | Address|
|  0x40           | SET\_DATA\_RATE (0x03)                 | Data rate  | Zero    | Zero     | None|
|  0x40           | SET\_RADIO\_POWER (0x04)               | Power      | Zero    | Zero     | None|
|  0x40           | SET\_RADIO\_ARD (0x05)                 | ARD        | Zero    | Zero     | None|
|  0x40           | SET\_RADIO\_ARC (0x06)                 | ARC        | Zero    | Zero     | None|
|  0x40           | ACK\_ENABLE (0x10)                     | Active     | Zero    | Zero     | None|
|  0x40           | SET\_CONT\_CARRIER (0x20)              | Active     | Zero    | Zero     | None|
|  0x40           | START\_SCAN\_CHANNELS (0x21)           | Start      | Stop    | Length   | Packet|
|  0xC0           | GET\_SCAN\_CHANNELS (0x21)             | Zero       | Zero    | 63       | Result|
|  0x40           | SET\_INLINE\_MODE (0x23)               | Active     | Zero    | Zero     | None |
|  0x40           | SET\_PACKET\_LOSS\_SIMULATION (0x30)   | Zero       | Zero    | 2        | [packet_loss_percent: u8, ack_loss_percent:u8]
|  0x40           | LAUNCH\_BOOTLOADER (0xFF)     | Zero       | Zero    | Zero     | None|

### Set radio channel

  |bmRequestType  | bRequest                    | wValue   | wIndex  | wLength  | data   |
  |---------------| ----------------------------| ---------| --------| ---------| ------ |
  |0x40           | SET\_RADIO\_CHANNEL (0x01)  | channel  | Zero    | Zero     | None   |

The nRF24LU1 chip provides 126 Channels of 1MHz from 2400MHz to 2525MHz.
The channel parameter shall be between 0 and 125 (if not, the command
will be ignored).

The radio channel is set as soon as the USB setup transaction is
completed, which takes about 1ms. The new frequency is going to be used
for the following transferred packets. The default value for the radio
channel is 2.

---
***Note:*** This command disables inline mode if it was previously enabled.
---

### Set radio address

|  bmRequestType  | bRequest                    | wValue   | wIndex  | wLength  | data    |
|  ---------------| ----------------------------| -------- |-------- |--------- |---------|
|  0x40           | SET\_RADIO\_ADDRESS (0x02)  | Zero     |Zero     |5         |Address  |

The packet sent by the radio contains a 5 bytes address. The same
address must be configured in the receiver for the communication to
work.

The address must follow the requirement of section 6.4.3.2 of the
nRF24LU1 documentation:

>    Addresses where the level shifts only one time (that is, 000FFFFFFF) can often be detected in
>    noise and can give a false detection, which may give a raised Packet-Error-Rate. Addresses
 >   as a continuation of the preamble (hi-low toggling) raises the Packet-Error-Rate.

The default address is 0xE7E7E7E7E7.

---
***Note:*** This command disables inline mode if it was previously enabled.
---

### Set data rate

|  bmRequestType  | bRequest                | wValue     | wIndex  | wLength  | data   |
|  ---------------| ------------------------| -----------| --------| ---------| ------ |
|  0x40           | SET\_DATA\_RATE (0x03)  | Data rate  | Zero    | Zero     | None   |

Possible values for the data rate:

|  Value   Radio data rate
|  ------- -----------------
|  0       250Kbps
|  1       1MBps
|  2       2Mbps (Default)

---
***Note:*** This command disables inline mode if it was previously enabled.
---

### Set radio power

|  bmRequestType  | bRequest                  | wValue  | wIndex  | wLength  | data  |
|  ---------------| --------------------------| --------| --------| ---------| ------|
|  0x40           | SET\_RADIO\_POWER (0x04)  | Power   | Zero    | Zero     | None  |

Sets the radio amplifier output power. Possible values:

|  Value  | Power|
|  -------|  --------|
|  0      | -18dBm |
|  1      | -12dBm |
|  2      | -6dBm |
|  3      | 0dBm |

### Configure auto retry (ARD/ARC)

|  bmRequestType  | bRequest                | wValue  | wIndex  | wLength  | data
|  ---------------| ------------------------| --------| --------| ---------| ------
|  0x40           | SET\_RADIO\_ARD (0x05)  | ARD     | Zero    | Zero     | None
|  0x40           | SET\_RADIO\_ARC (0x06)  | ARC     | Zero    | Zero     | None

After sending a packet the radio automatically waits for an acknowledge.
ARD and ARC permit to configure the delay the radio waits for the
acknowledge and the number of times the transfer will be retried in case
the acknowledge is not received in that delay.

The delay ARD depends on the length, in seconds, of the ACK packet. This
depends on the data rate and the payload length contained in the ACK
packet. The ARD can be configured either by steps of 250us or by ACK
payload length. If the ACK payload length is configured, the time will
be recalculated automatically even if the data rate is changed. To set
the ACK payload length the bit 7 of ARD must be set (length \| 0x80).

Possible values for ARD:

|  Value  | ARD wait time|
|  -------| --------------------|
|  0x00   | 250us |
|  0x01   | 500us |
|  \...   | \... |
|  0x0F   | 4000us |
|  Value  | ACK payload length |
|  0x80   | 0Byte |
|  0x81   | 1Byte |
|  \...   | \... |
|  0xA0   | 32Bytes |

ARC configures the number of times the radio will retry a transfer if
the ACK has not been received, it can be set from 0 to 15.

By default ARD=32Bytes (0xA0) and ARC=3.

### Auto ACK configuration

|  bmRequestType  | bRequest            | wValue  | wIndex  | wLength  | data |
|  ---------------| --------------------| --------| --------| ---------| ------ |
|  0x40           | ACK\_ENABLE (0x10)  | Active  | Zero    | Zero     | None |

By default, the Crazyradio is configured with auto ACK enabled. This
means that after transmitting a packet, the radio waits for an
acknowledge from the receiver. This setting permits to deactivate
waiting for the ACK packet so that the packet will be sent only one time
and there is no guarantee that it has been correctly received.

|  Active values  | Meaning|
|  ---------------| ---------------------------|
|  0              | Auto ACK deactivated|
|  Not 0          | Auto ACK enable (default)|

---
***Note:*** This command disables inline mode if it was previously enabled.
---

### Continuous carrier mode

|  bmRequestType  | bRequest                   | wValue  | wIndex  | wLength  | data  |
|  ---------------| ---------------------------| --------| --------| ---------| ------|
|  0x40           | SET\_CONT\_CARRIER (0x20)  | Active  | Zero    | Zero     | None  |

The nRF24L radio chip provides a test mode in which a continuous
non-modulated sine wave is emitted. This permits, among other things, to
test the quality of the RF elements of the board. When this mode is
activated the radio dongle does not transmit any packets.

While the continuous carrier mode is active, it is possible to set
channel and power to change the frequency and power of the emitted wave.

|  Active values  | Meaning|
|  ---------------| -----------------------------------|
|  0              | Dongle working normally (default)|
|  Not 0          | Dongle in continuous carrier mode|

### Channels scanning

This function is implemented in Crazyradio version 0.5 and over.

|  bmRequestType  | bRequest                      | wValue  | wIndex  |   wLength|   data  |
|  ---------------| ------------------------------| --------| --------| ---------| --------|
|  0x40           | START\_SCAN\_CHANNELS (0x21)  | Start   | Stop    | Length   | Packet  |
|  0xC0           | GET\_SCAN\_CHANNELS (0x21)    | Zero    | Zero    | 64       | Result  |

Scan a range of channels and compile a list of channel from which an ACK
has been received. The command START\_SCAN\_CHANNELS should be executed
first with start being the first scanned channel and stop the last one.
Those should be within 0 to 125. The data is the packet payload sent on
each channel, it should be at least one byte long.

All parameters, except the channel, are used unmodified during the scan.
If the data rate is set to 2MBPs the scan is done every second channel.
To get the list of channels that answered, GET\_SCANN\_CHANNELS should
be called just after a scan. Up to 63 bytes are returned corresponding
to up to 63 channels on which the packet was acknowledged.

---
***Note***

After scanning, the channel will be set to the last scanned channel.

---

---
***Warning***
On some platform, an empty scan will return a
buffer of 64 bytes. This is though to be a USB host implementation bug,
see [ticket
\#9](https://github.com/bitcraze/crazyradio-firmware/issues/9) in the
Crazyradio firmware project. If a buffer of more than 63 bytes is
returned, it means that no channel have been received.

---
### Inline settings mode

|  bmRequestType  | bRequest                   | wValue  | wIndex  | wLength  | data   |
|  ---------------| ---------------------------| --------| --------| ---------| ------ |
|  0x40           | SET\_INLINE\_MODE (0x23)     | Active  | Zero    | Zero     | None   |

This mode allows sending radio configuration together with packet payload on the OUT endpoint.
This makes the communication with multiple PRX much more efficient!

|  Active values   | Meaning|
|  --------------- | ----------------------------------------------------------- |
|  0               | Inline mode deactivated (default)                           |
|  1               | Inline mode enabled                                         |
|  ...             | Reserved, STALL the setup phase                             |

When enabled, the data format on the OUT endpoint becomes:

**OUT endpoint format (host to device):**

| Byte position | Length (bytes) | Description                               |
| ------------- | -------------- | ----------------------------------------- |
| 0             | 1              | Total length (including this header)      |
| 1             | 1              | Datarate (bits 0-1: 0=250kbps, 1=1Mbps, 2=2Mbps)<br>Ack enabled (bit 4: 0=disabled, 1=enabled) |
| 2             | 1              | Radio channel (0-100)                     |
| 3-7           | 5              | Radio address (5 bytes)                   |
| 8+            | 0-32           | Radio packet payload                      |

**IN endpoint format (device to host):**

After sending a packet in inline mode, the response format on the IN endpoint is:

| Byte position | Length (bytes) | Description                               |
| ------------- | -------------- | ----------------------------------------- |
| 0             | 1              | Total length (including this header)      |
| 1             | 1              | Ack received (bit 0: 0=no ack, 1=ack received)<br>RSSI < -64dBm (bit 1: 0=strong signal, 1=weak signal)<br>Invalid settings (bit 2: 0=valid, 1=invalid settings)<br>Retransmission count (bits 4-7: 0-15) |
| 2+            | 0-32           | ACK payload data (if any)                 |

**Settings validation:**

Invalid settings that are not handled by Crazyradio 2 (causing invalid_settings flag to be set):
- Datarate value of 0 (250kbps is not handled by Crazyradio 2.0)
- Channel value greater than 100

The settings are applied in the same way as if they were set using setup commands:
they replace any other settings that have been made before and will stick to be
used by future packets if inline mode is disabled.

---

### Packet loss simulation

|  bmRequestType  | bRequest                             | wValue  | wIndex  | wLength  | data   |
|  ---------------| ---------------------------          | --------| --------| ---------| ------ |
|  0x40           | SET\_PACKET\_LOSS\_SIMULATION (0x30) | Zero    | Zero    | 2        | [packet_loss_percent: u8, ack_loss_percent:u8]

Crazyradio 2.0 has the capability to simulate packet loss. This is
useful for working with and debuging communication protocols.

The packet loss percentage, makes the radio drop a certain percentage
of packet before sending them to the radio. This means the receiver
will not receive the packet.

The Ack loss percentage drops a certain percentage of ack packets.
This means that the receiver will receive the packet, send the ack
but the ack packet might be dropped by Crazyradio and reported as lost.

Both case will look the same on the PC side: the packet is reported as not acked.

---

### Launch bootloader

|  bmRequestType  | bRequest                   | wValue  | wIndex  | wLength  | data   |
|  ---------------| ---------------------------| --------| --------| ---------| ------ |
|  0x40           | LAUNCH\_BOOTLOADER (0xFF)  | Zero    | Zero    | Zero     | None   |

This command is used to launch the Nordic semiconductor USB bootloader.
After sending this command, a USB reset shall be emitted which will
trigger the dongle to start the bootloader. After sending this command,
the Dongle is only waiting for a USB reset which means that any other
commands or data will be ignored.

The bootloader is pre-loaded by Nordic Semi. in the nRF24LU1 chip at the
address 0x7800. The Crazyradio firmware will jump to it when the
sequence LAUCH\_BOOTLOADER followed by a USB reset is executed. The
bootloader will appear at VID/PID of 0x1915/0x0101. See nRF24LU1
datasheet for the bootloader documentation.

A PC client for the bootloader is part of the Crazyflie ground station
program.

Radio protocol
--------------

The Crazyradio dongle is currently only operating in PTX mode. To get a
downlink from the copter the ACK payload is used, which means that data
are received only when data are sent. In the case of Crazyflie, to have
bidirectional communication even when no data is send, a null packet
(0xff) is sent periodically to pull the downlink data stream.

For Crazyflie the communication protocol is described in [Communication
protocol Overview](https://www.bitcraze.io/documentation/repository/crazyflie-firmware/master/functional-areas/crtp/).