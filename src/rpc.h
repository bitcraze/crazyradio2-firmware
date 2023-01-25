/**
 * ,---------,       ____  _ __
 * |  ,-^-,  |      / __ )(_) /_______________ _____  ___
 * | (  O  ) |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * | / ,--´  |    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *    +------`   /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Copyright 2023 Bitcraze AB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_writer.h>

#define RPC_TYPE_REQUEST 0
#define RPC_TYPE_RESPONSE 1
#define RPC_TYPE_NOTIF 2

#define RPC_METHOD_MAX_LEN 63

// Size of RPC packet buffers
#define RPC_MTU 1023

typedef enum rpc_type {
    rpc_request = 0,
    rpc_response = 1,
    rpc_notification = 2,
} rpc_type_t;

typedef enum rpc_error {
    rpc_no_error = 0,
    rpc_bad_request = 1,
    rpc_unknown_method = 2,
    rpc_list_methods = 3,
} rpc_error_t;

struct rpc_method;
struct rpc_api;

typedef struct rpc_request {
    rpc_type_t type;
    struct rpc_method *method;
    uint64_t msgid;
    CborValue param;
    struct rpc_api *api;
} rpc_request_t;

typedef struct rpc_transport {
    size_t mtu;
    void (*send)(char* buffer, size_t length);
} rpc_transport_t;

typedef struct rpc_response {
    rpc_transport_t transport;
    uint64_t msgid;
    char *buffer;
    struct cbor_buf_writer writer;
    CborEncoder encoder;
    CborEncoder payload_encoder;
    bool prepared;
    bool is_error;
} rpc_response_t;

typedef struct rpc_notification {
    rpc_transport_t transport;
    char *buffer;
    struct cbor_buf_writer writer;
    CborEncoder encoder;
    CborEncoder payload_encoder;
    bool prepared;
    bool sent;
} rpc_notification_t;

typedef void (*rpc_method_fn)(const rpc_request_t *request, rpc_response_t *response);
typedef void (*rpc_notify_fn)(const rpc_request_t *request);

typedef struct rpc_method {
    const char * name;
    rpc_method_fn method;
    rpc_notify_fn notify;
} rpc_method_t;

typedef struct rpc_api {
    rpc_method_t *methods;
    size_t methods_length;
} __attribute__((packed)) rpc_api_t;


rpc_error_t rpc_dispatch(rpc_api_t *api, uint8_t *request_buffer, int request_len, rpc_transport_t transport, char *response_buffer);

/**
 * @brief Prepare the reponse and return a Cbor encoder to the result field
 *
 * This function can be called multiple time, it will reset the CBor encoder to the response field each time.
 *
 * @param response Pointer to the response object
 * @return CborEncoder*
 */
CborEncoder* rpc_response_prepare_result(rpc_response_t *response);

/**
 * @brief Prepare the response and return a Cbor encoder to the error field
 *
 * This allows to return a custom error. See [rpc_response_send_errorstr] to return a simple string error.
 *
 * This function can be called multiple time, it will reset the CBor encoder to the error field each time.
 *
 * @param response Pointer to the response object
 * @return CborEncoder*
 */
CborEncoder* rpc_response_prepare_error(rpc_response_t *response);

/**
 * @brief Finalize and send the response.
 *
 * If neither rpc_response_prepare_result() or rpc_response_prepare_error() have been called, both error and
 * result fields will be set to null.
 *
 * @param response Pointer to the response object
 */
void rpc_response_send(rpc_response_t *response);

/**
 * @brief Send a string error as a response
 *
 * This is a utility function that internally calls rpc_response_prepare_error(), encode the string and then call
 * rpc_response_send(). This should be the most common case of sending back an error.
 *
 * @param response Pointer to the response object
 * @param error Null terminated text string describing the error
 */
void rpc_response_send_errorstr(rpc_response_t *response, const char* error);

/**
 * @brief Make a notification object that can be used to send back notification related to a response
 *
 * The notification object can be used as many time as wanted, using the rpc_notification_prepare() and then
 * rpc_notification_send() functions.
 *
 * @param response Response the notification will be related to. The notification will use the same transport.
 * @param notification Pointer to the notification object to be created.
 * @param notification_buffer Data buffer to be used for the notification. The length of it must be RPC_MTU.
 */
void rpc_response_create_notification(const rpc_response_t *response, rpc_notification_t *notification, char* notification_buffer);

/**
 * @brief Prepare notification for sending
 *
 * @param notification Notification to prepare
 * @param method
 * @return Encoder to be used to write the notification param
 */
CborEncoder* rpc_notification_prepare_param(rpc_notification_t *notification, const char* method);

/**
 * @brief Send the notification.
 *
 * If rpc_notification_prepare_param() has not been called before on this notification object, this is a NOP
 *
 * If this is called twice with the same notification object without calling rpc_notification_prepare_param() in
 * between, the same notification is sent twice.
 *
 * @param notification Notification object to send.
 */
void rpc_notification_send(rpc_notification_t *notification);
