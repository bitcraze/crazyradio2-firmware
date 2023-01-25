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

#include "oryx.h"

#include <zephyr/kernel.h>

#include "oryx_ll.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(oryx);

static bool isInit;

K_MSGQ_DEFINE(scan_queue, sizeof(oryx_ll_detected_t), 10, 4);

void scan_detected_cb(oryx_ll_detected_t* detected)
{
    if(k_msgq_put(&scan_queue, detected, K_NO_WAIT) != 0) {
        LOG_ERR("Scan queue full, detection lost");
    }
}
static oryx_ll_detected_t dummy_detected = {0,};

void timeout_detected_cb()
{

    if(k_msgq_put(&scan_queue, &dummy_detected, K_NO_WAIT) != 0) {
        LOG_ERR("Scan queue full, timeout lost");
    }
}

void oryx_scan_rpc(const rpc_request_t *request, rpc_response_t *response)
{
    if (!isInit) {
        rpc_response_send_errorstr(response, "NotInitialized");
        return;
    }

    if(!cbor_value_is_array(&request->param)) goto bad_request;

    CborValue array;
    cbor_value_enter_container(&request->param, &array);

    // Decoding the channel
    if (!cbor_value_is_unsigned_integer(&array)) goto bad_request;
    uint64_t channel;
    cbor_value_get_uint64(&array, &channel);
    cbor_value_advance(&array);
    if (channel > 100) goto bad_request;

    // Decoding the timeout_ms
    if (!cbor_value_is_unsigned_integer(&array)) goto bad_request;
    uint64_t timeout_ms;
    cbor_value_get_uint64(&array, &timeout_ms);

    // Return a mac address as response
    CborEncoder *result = rpc_response_prepare_result(response);

    CborEncoder map;
    cbor_encoder_create_map(result, &map, CborIndefiniteLength);

    // Do something
    k_sleep(K_MSEC(timeout_ms));
    oryx_ll_scan(channel, timeout_ms, scan_detected_cb, timeout_detected_cb);

    while(1) {
        oryx_ll_detected_t detected;
        if( k_msgq_get(&scan_queue, &detected, K_MSEC(2 * timeout_ms)) != 0) {
            __ASSERT(0, "Internal error");
        }

        if(!memcmp(&dummy_detected, &detected, sizeof(oryx_ll_detected_t))) {
            break;
        }

        char mac_address[(6 * 2) + 1];
        sprintf(mac_address, "%02x%02x%02x%02x%02x%02x", detected.mac_address[0],
        detected.mac_address[1],detected.mac_address[2],detected.mac_address[3],
        detected.mac_address[4], detected.mac_address[5]);
        cbor_encode_text_stringz(&map, mac_address);
        // Todo, change to byte string instead

        CborEncoder inner_map;
        cbor_encoder_create_map(&map, &inner_map, 1);
        cbor_encode_text_stringz(&inner_map, "rssi");
        cbor_encode_int(&inner_map, detected.rssi);
        cbor_encoder_close_container(&map, &inner_map);

    }

    cbor_encoder_close_container(result, &map);

    rpc_response_send(response);

    return;
bad_request:
    rpc_response_send_errorstr(response, "BadRequest");
}

void oryx_init()
{
    isInit = true;
}

void oryx_deinit()
{
    isInit = false;
}
