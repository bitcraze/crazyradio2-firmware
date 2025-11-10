---
title: Crazyradio 2.0 USB protocol
page_id: usb_protocol
---

Crazyradio 2.0 provides one proprietary USB interface to transfer data packets back and forth.
The interface has the following configuration in the USB descriptor:

| Descriptor         | Value | Note                  |
|--------------------|-------|-----------------------|
| bInterfaceClass    | 255   | Vendor Specific Class |
| bInterfaceSubClass | 0     |                       |
| bInterfaceProtocol | 0     |                       |

Note that there might be more interfaces on the Crazyradio 2.0 so addresses of the endpoints should not be assumed,
they should be fetched from the descriptor.

At low level the USB protocol is handled by one OUT and one IN endpoint.
Each endpoints allow to send and receive a stream of application packet using the same protocol.

| Endpoint address | Description    |
|------------------|----------------|
| 0x01             | Crazyradio Out |
| 0x81             | Crazyradio IN  |

The endpoints are setup with a max packet size of 64Bytes, though they are to be considered as streaming endpoints.
The application packets are not necessarily aligned on USB packets (though they will more often than not due to flushing algorithm).

## Protocol version

The protocol, as described in this document, is the protocol version **0**.
The version can be checked using the following control transfer:

| Field         | Value | Note                                    |
|---------------|-------|-----------------------------------------|
| bmRequestType | 0xC1  | Device-to-host Vendor Interface request |
| bRequest      | 0     | CRUSB_VERSION_REQUEST                   |
| wValue        | 0     |                                         |
| wIndex        | 0     |                                         |
| wLength       | 1     | The version length is 1 byte long       |

The request will return the USB protocol version.

Note that the USB protocol version does not match the Crazyradio 2.0 firmware version, it only applies to this protocol.

## Streaming protocol

Packets are transmitted one after each-other. First the size and then the data bytes:

| Byte | Description                                                         |
|------|---------------------------------------------------------------------|
| 0-1  | Little-endian 10 bits length. The 6 msb are reserved for future use |
| 2-...| data bytes                                                          |

If the stream does not fit in one 64 Bytes USB packet it continues in a subsequent packets.
A non-full USB packet is only allowed at the end of a packet.
The packet following a non-full (less than 64 bytes long) packet must start with the length field of a new packet.

The Crazyradio will always send a non-full USB packet at the end of a stream. If the stream fits precisely in 64 bytes
packets, an empty packet is sent after it.

## Streaming reset

In order to align both the PC driver and the Crazyradio when starting to communicate, it is necessary to reset the
stream to make sure the first bytes sent will be interpreted by the Crazyradio as the length. This is done by sending
a 0-length USB packet on the OUT endpoint. This should be done first by a USB driver before sending any application
packets.

The IN endpoint can be reset by receiving USB packets with a short timeout until either a timeout occurs or a non-full
packet (with a length below 64 bytes) is received. This guarantees that the next received USB packet will be
aligned with the length field.
