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

#include "power_measurement.h"

#include <zephyr/kernel.h>

#include <hal/nrf_radio.h>
#include "fem.h"

static bool is_init = false;

void power_measurement_init() {
    if (is_init) {
        return;
    }

    nrf_radio_power_set(NRF_RADIO, true);
    nrf_radio_shorts_enable(NRF_RADIO, NRF_RADIO_SHORT_READY_START_MASK | NRF_RADIO_SHORT_END_DISABLE_MASK);

    is_init = true;
}

void power_measurement_deinit() {
    if (is_init) {
        is_init = false;
    }
}

// RPC methods
#include "rpc.h"

void power_measurement_measure_channel_rpc(const rpc_request_t *request, rpc_response_t *response)
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

    // // The second element is the number of measurement to carry
    // uint64_t count;
    // if (cbor_value_get_uint64(&request->param, &count) != CborNoError || count > 100) {
    //     rpc_response_send_errorstr(response, "BadRequest");
    //     return;
    // }

    nrf_radio_shorts_set(NRF_RADIO, 0);

    fem_rxen_set(true);

    // Start the radio in RX mode
    nrf_radio_frequency_set(NRF_RADIO, 2400+channel);

    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_RXEN);
    // Wait for the radio to be ready
    while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_READY)) {
        // Do nothing
    }

    // Start the RSSI measurement
    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_RSSISTART);
    // Wait for the RSSI measurement to complete
    while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_RSSIEND)) {
        // Do nothing
    }
    // Read the RSSI value
    int8_t rssi = nrf_radio_rssi_sample_get(NRF_RADIO);
    printk("RSSI: %d\n", rssi);

    // Stop the radio
    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);

    // Wait for the radio to be disabled
    while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED)) {
        // Do nothing
    }

    fem_rxen_set(false);

    nrf_radio_shorts_disable(NRF_RADIO, NRF_RADIO_SHORT_READY_START_MASK | NRF_RADIO_SHORT_END_DISABLE_MASK);

    // Send the response
    CborEncoder * encoder = rpc_response_prepare_result(response);

    cbor_encode_int(encoder, rssi);

    rpc_response_send(response);
}
