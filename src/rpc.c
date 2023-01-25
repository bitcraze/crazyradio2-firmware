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

#include "rpc.h"

#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rpc, LOG_LEVEL_DBG);

#define CHECK_IS_ARRAY(it) if (!cbor_value_is_array(it)) { goto error; }
#define CHECK_IS_UINT(it) if (!cbor_value_is_unsigned_integer(it)) { goto error; }
#define CHECK_IS_STRING(it) if (!cbor_value_is_text_string(it)) { goto error; }

static void list_methods_method(const rpc_request_t *request, rpc_response_t *response) {
	LOG_INF("Get methods called !!!");

	CborEncoder *encoder = rpc_response_prepare_result(response);

	CborEncoder map_encoder;
	cbor_encoder_create_map(encoder, &map_encoder, request->api->methods_length);

    for (int i=0; i<request->api->methods_length; i++) {
        cbor_encode_text_stringz(&map_encoder, request->api->methods[i].name);
        cbor_encode_uint(&map_encoder, i);
    }

	cbor_encoder_close_container(encoder, &map_encoder);

	rpc_response_send(response);
}

static rpc_method_t list_methods = {
    .name = "well-known.methods",
    .method = list_methods_method,
};

bool find_method(rpc_api_t *api, rpc_request_t *request, const char *method_name) {
    for (int i=0; i < api->methods_length; i++) {
        if (!strcmp(api->methods[i].name, method_name)) {
            request->method = &api->methods[i];
            return true;
        }
    }

    // Implement the list methods as a special case
    if (!strcmp(list_methods.name, method_name)) {
        request->method = &list_methods;
        request->api = api;
        return true;
    }

    return false;
}

rpc_error_t decode_rpc_request(rpc_api_t *api, const CborValue *it, rpc_request_t *request) {
    CHECK_IS_ARRAY(it);
    CborValue array;
    cbor_value_enter_container(it, &array);

    // Check that this is a request
    CHECK_IS_UINT(&array);
    int type;
    cbor_value_get_int(&array, &type);
    cbor_value_advance(&array);

    if (type == RPC_TYPE_REQUEST) {
        request->type = rpc_request;
        CHECK_IS_UINT(&array);
        cbor_value_get_uint64(&array, &request->msgid);
        cbor_value_advance(&array);
    } else if (type == RPC_TYPE_NOTIF) {
        request->type = rpc_notification;
    } else {
        // We do no handle response here, this is a server-only implementation
        goto error;
    }

    // If the method is an integer, it is an index of the method in the API
    if (cbor_value_is_unsigned_integer(&array)) {
        uint64_t index;
        cbor_value_get_uint64(&array, &index);
        cbor_value_advance(&array);

        if (index > api->methods_length) {
            return rpc_unknown_method;
        }

        request->method = &api->methods[index];
    } else {  // Otherwise it is the method name string
        CHECK_IS_STRING(&array);
        size_t method_length=0;
        char method_name[RPC_METHOD_MAX_LEN];
        cbor_value_get_string_length(&array, &method_length);
        if (method_length > RPC_METHOD_MAX_LEN) {
            goto error;
        }
        size_t method_bufer_max_len = RPC_METHOD_MAX_LEN+1;
        cbor_value_copy_text_string(&array, method_name, &method_bufer_max_len, &array);

        if (!find_method(api, request, method_name)) {
            return rpc_unknown_method;
        }
    }

    request->param = array;

    return rpc_no_error;
error:
    return rpc_bad_request;
}

static void rpc_send_unknown_method(const rpc_request_t *request, rpc_response_t *response) {
    rpc_response_send_errorstr(response, "well-known.NotFound");
}

rpc_error_t rpc_dispatch(rpc_api_t *api, uint8_t *request_buffer, int request_len, rpc_transport_t transport, char* response_buffer) {
    // Parsing the request
    CborParser parser;
    CborValue it;
    struct cbor_buf_reader reader;

    cbor_buf_reader_init(&reader, request_buffer, request_len);
    cbor_parser_init(&reader.r, request_len, &parser, &it);

    rpc_request_t request;
    rpc_error_t error = decode_rpc_request(api, &it, &request);

    // If the request is not found, inform the client
    if (error == rpc_unknown_method) {
        rpc_response_t response = {
            .transport = transport,
            .msgid = request.msgid,
            .buffer = response_buffer,
            .prepared = false,
        };

        rpc_send_unknown_method(&request, &response);
        // The error has been handled
        return rpc_no_error;
    }

    // Any other errors (including unknown notification), is reported up
    if (error) {
        return error;
    }

    if (request.type == rpc_request && request.method->method) {
        rpc_response_t response = {
            .transport = transport,
            .msgid = request.msgid,
            .buffer = response_buffer,
            .prepared = false,
        };

        request.method->method(&request, &response);
    } else if (request.type == rpc_notification && request.method->notify) {
        request.method->notify(&request);
    } else {
        return rpc_unknown_method;
    }

    return rpc_no_error;
}

CborEncoder* rpc_response_prepare_result(rpc_response_t *response) {
    cbor_buf_writer_init(&response->writer, response->buffer, response->transport.mtu);
    cbor_encoder_init(&response->encoder, &response->writer.enc, 0);

    // An response is a 4 element array
    cbor_encoder_create_array(&response->encoder, &response->payload_encoder, 4);

    // Type
    cbor_encode_uint(&response->payload_encoder, rpc_response);

    // msg ID
    cbor_encode_uint(&response->payload_encoder, response->msgid);

    // Error is null if we return a result
    cbor_encode_null(&response->payload_encoder);

    // Set proper response flags
    response->is_error = false;
    response->prepared = true;

    // The method will fill-up the result payload
    return &response->payload_encoder;
}

CborEncoder* rpc_response_prepare_error(rpc_response_t *response) {
    cbor_buf_writer_init(&response->writer, response->buffer, response->transport.mtu);
    cbor_encoder_init(&response->encoder, &response->writer.enc, 0);

    // An response is a 4 element array
    cbor_encoder_create_array(&response->encoder, &response->payload_encoder, 4);

    // Type
    cbor_encode_uint(&response->payload_encoder, rpc_response);

    // msg ID
    cbor_encode_uint(&response->payload_encoder, response->msgid);

    // Set proper response flags
    response->is_error = true;
    response->prepared = true;

    // The method will fill-up the response error
    return &response->payload_encoder;
}

void rpc_response_send(rpc_response_t *response) {
    // If this response has not been prepared, prepare it and add a null result
    if (!response->prepared) {
        CborEncoder *result = rpc_response_prepare_result(response);
        cbor_encode_null(result);
    }

    // If this is an error, we need to add a null result
    if (response->is_error) {
        cbor_encode_null(&response->encoder);
    }

    cbor_encoder_close_container(&response->encoder, &response->payload_encoder);
    size_t length = cbor_buf_writer_buffer_size(&response->writer, response->buffer);

    response->transport.send(response->buffer, length);
}

void rpc_response_send_errorstr(rpc_response_t *response, const char* error) {
    CborEncoder *error_encoder = rpc_response_prepare_error(response);

    cbor_encode_text_stringz(error_encoder, error);

    rpc_response_send(response);
}

void rpc_response_create_notification(const rpc_response_t *response, rpc_notification_t *notification, char* notification_buffer) {
    notification->transport = response->transport;
    notification->buffer = notification_buffer;
    notification->prepared = false;
    notification->sent = false;
}

CborEncoder* rpc_notification_prepare_param(rpc_notification_t *notification, const char* method) {
    cbor_buf_writer_init(&notification->writer, notification->buffer, notification->transport.mtu);
    cbor_encoder_init(&notification->encoder, &notification->writer.enc, 0);

    // An response is a 3 element array
    cbor_encoder_create_array(&notification->encoder, &notification->payload_encoder, 3);

    // Type
    cbor_encode_uint(&notification->payload_encoder, rpc_notification);

    // Method string
    // TODO: See if we can use compression (method ID) here
    cbor_encode_text_stringz(&notification->payload_encoder, method);

    // Set proper notification flags
    notification->prepared = true;
    notification->sent = false;

    // The method will fill-up the param payload
    return &notification->payload_encoder;
}

void rpc_notification_send(rpc_notification_t *notification) {
    if (!notification->prepared) {
        return;
    }

    if (!notification->sent) {
        cbor_encoder_close_container(&notification->encoder, &notification->payload_encoder);
    }

    size_t length = cbor_buf_writer_buffer_size(&notification->writer, notification->buffer);

    notification->transport.send(notification->buffer, length);
}
