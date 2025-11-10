---
title: Crazyradio 2.0 CBOR RPC specification
page_id: cbor_rpc_specification
---

## Abstract

Crazyradio 2.0 uses a generic Remote Procedure Call protocol on top of the [CBOR] serializing format.
This protocol is the only way to interact with Crazyradio 2.0 over [usb](usb-protocol.md) or possibly over other
transport in the future.

This protocol is greatly inspired by [MessagePack-RPC] but using CBOR instead of MessagePack as serialization format.

## Protocol flow

The protocol defines 3 messages types:
 - Request
 - Response
 - Notification

Request and responses are used for procedure calls. They both contain a message ID that allows to match a response with
the corresponding response. Notifications are sent and received without any message ID, they are identified by a
method name.

Typically a RPC server will receive requests and send back response. A client will send requests and receive response.
Both client and server can send and receive notifications.

## Messages format

All messages are encoded and sent on the wire as a CBOR array.

Methods name and errors starting with ".well-known" are reserved for the protocol itself and should not be used.

### Request message

    [Type (0), msgid, method, params]

 - **type**: Unsigned integer of value 0 to denote a request
 - **msgid**: Unsigned integer of up to 64Bits precision. The *Response* message from the server will have the same *msgid*.
 - **method**: String or index representing the method to call
 - **params**: Any CBOR element

### Response message

    [Type (1), msgid, error, result]

 - **type**: Unsigned integer of value 1 to denote a response
 - **msgid**: Unsigned integer of up to 64Bits precision. This has the same value as in the *Request* message that initiated this response.
 - **error**: *Null* if there is no error. Any CBOR element otherwise.
 - **result**: Any CBOR element

### Notification message

    [Type (2), method, param]

 - **type**: Unsigned integer of value 2 to denote a notification
 - **method**: String or index representing the method that initiated or will receive the notification.
 - **params**: Any CBOR element

## Method not found error

If a method is not found, the server answer with:

    [type (1), msgid, "well-known.NotFound", Null]

This is a special case to indicate that the method does not exist.

## Listing methods

A special method exists to list the supported methods of a server:

    [type (0), msgid, "well-known.methods", None]

The response result contains a CBOR Map with as key string of the method name and as value an unsigned integer that can be
used as method index.

## Method name compression

Since the intention of the protocol is to be used in a context where the list of method is static, methods can be called
by index instead of by name. This greatly improves efficiency since it can instantly select the method on the server
side instead of using a look-up table.

The [method listing procedure](#listing-methods) returns the list of all the methods associated with their index, the
index can then be used instead of the method name in Request and Notifications.

[CBOR]: https://cbor.io/
[MessagePack-RPC]: https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md
