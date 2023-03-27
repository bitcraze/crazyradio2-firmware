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

// This file implement radio mode switching to make sure only one algorithm/protocol is using the radio at any one time

#include "esb.h"
#include "radio_mode.h"
#include "oryx.h"
#include "contwave.h"
#include "power_measurement.h"

static struct {
    char* name;
    void (*init)(void);
    void (*deinit)(void);
} modes[] = {
    {.name = "disable", .init = NULL, .deinit = NULL},
    {.name = "esb", .init = esb_init, .deinit = esb_deinit},
    {.name = "oryx", .init = oryx_init, .deinit = oryx_deinit},
    {.name = "contWave", .init = contwave_init, .deinit = contwave_deinit},
    {.name = "powerMeasurement", .init = power_measurement_init, .deinit = power_measurement_deinit},
};

static const int modes_length = sizeof(modes) / sizeof(modes[0]);

static int current_mode = 0;

void radio_mode_init() {
    if (modes[current_mode].init) {
        modes[current_mode].init();
    }
}

void radio_mode_list_rpc(const rpc_request_t *request, rpc_response_t *response) {
    CborEncoder *result = rpc_response_prepare_result(response);

    CborEncoder array;
    cbor_encoder_create_array(result, &array, modes_length);

    for (int i=0; i<modes_length; i++) {
        cbor_encode_text_stringz(&array, modes[i].name);
    }

    cbor_encoder_close_container(result, &array);

    rpc_response_send(response);
}

void radio_mode_get_rpc(const rpc_request_t *request, rpc_response_t *response) {
    CborEncoder *result = rpc_response_prepare_result(response);
    cbor_encode_text_stringz(result, modes[current_mode].name);
    rpc_response_send(response);
}

void radio_mode_set_rpc(const rpc_request_t *request, rpc_response_t *response) {

    if (!cbor_value_is_text_string(&request->param)) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }

    char new_mode[64];
    size_t new_mode_length = 64;
    if (cbor_value_copy_text_string(&request->param, new_mode, &new_mode_length, NULL) != CborNoError) {
        rpc_response_send_errorstr(response, "BadRequest");
        return;
    }

    int i;
    for (i=0; i<modes_length; i++) {
        if (!strcmp(modes[i].name, new_mode)) {
            if (current_mode == i) {
                break;
            }

            if (modes[current_mode].deinit) {
                modes[current_mode].deinit();
            }
            current_mode = i;
            if (modes[current_mode].init) {
                modes[current_mode].init();
            }
            break;
        }
    }

    if (i >= modes_length) {
        rpc_response_send_errorstr(response, "NotFound");
        return;
    }

    // Empty response
    rpc_response_send(response);
}
