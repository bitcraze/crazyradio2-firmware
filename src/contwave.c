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

#include "contwave.h"

#include <hal/nrf_radio.h>
#include "fem.h"

static bool is_init = false;

void contwave_init() {
    if (is_init) {
        return;
    }

    nrf_radio_mode_set(NRF_RADIO, NRF_RADIO_MODE_NRF_2MBIT);
    nrf_radio_txpower_set(NRF_RADIO, NRF_RADIO_TXPOWER_POS4DBM);

    is_init = true;
}

void contwave_deinit() {
    if (is_init) {
        is_init = false;
    }
}

// RPC methods
#include "rpc.h"

void contwave_start_rpc(const rpc_request_t *request, rpc_response_t *response)
{
    if (!is_init) {
        rpc_response_send_errorstr(response, "NotInitialized");
        return;
    }

    // The request should be an array, open it
    CborValue array;
    if (cbor_value_enter_container(&request->param, &array) != CborNoError) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }

    // The first element should be the channel
    uint64_t channel;
    if (cbor_value_get_uint64(&array, &channel) != CborNoError || channel > 100) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }

    // The second element should be the power
    int power;
    if (cbor_value_advance(&array) != CborNoError) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }
    if (cbor_value_get_int(&array, &power) != CborNoError) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }

    // If there is a third element, it should be the antenna
    if (cbor_value_advance(&array) == CborNoError) {
        uint64_t antenna;
        if (cbor_value_get_uint64(&array, &antenna) != CborNoError || antenna > 1) {
            rpc_response_send_errorstr(response, "BadRequest");
            return;
        }
        fem_set_antenna(antenna);
    }

    // Start the continuous wave
    nrf_radio_frequency_set(NRF_RADIO, 2400 + channel);
    // Set FEM TX power
    fem_set_power(power);
    // Enable FEM TX
    fem_txen_set(true);

    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);

    rpc_response_send(response);
}

void contwave_stop_rpc(const rpc_request_t *request, rpc_response_t *response)
{
    if (!is_init) {
        rpc_response_send_errorstr(response, "NotInitialized");
        return;
    }

    // Disable radio
    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);

    // Disable FEM TX
    fem_txen_set(false);

    rpc_response_send(response);
}
