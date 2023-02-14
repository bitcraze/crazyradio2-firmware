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

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "rpc.h"


// #define ADC_NODE DT_NODELABEL(adc0)

// static const struct adc_channel_cfg ch0_cfg_dt =
//     ADC_CHANNEL_CFG_DT(DT_CHILD(DT_NODELABEL(adc), channel_1));

static const struct adc_dt_spec adc_chan0 =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static int system_read_vcc(float *measurement) {
    int ret;
    int16_t buf;

    if (!device_is_ready(adc_chan0.dev)) {
        printk("ADC controller device not ready\n");
        return -ENODEV;
    }

    ret = adc_channel_setup_dt(&adc_chan0);
    if (ret < 0) {
        printk("Could not setup channel with error %d\n", ret);
        return ret;
    }

	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

    adc_sequence_init_dt(&adc_chan0, &sequence);

    ret = adc_read(adc_chan0.dev, &sequence);
    if (ret < 0) {
        printk("ADC read failed with error %d\n", ret);
        return ret;
    }

    int32_t val_mv = buf;
    ret = adc_raw_to_millivolts_dt(&adc_chan0, &val_mv);
    if (ret < 0) {
        printk("ADC raw to millivolts failed with error %d\n", ret);
        return ret;
    }

    *measurement = val_mv / 1000.0f;

    return 0;
}

// RPC function to read the VCC voltage
void system_read_vcc_rpc(const rpc_request_t *request, rpc_response_t *response) {
    float vcc = 0;
    int ret = system_read_vcc(&vcc);
    if (ret < 0) {
        rpc_response_send_errorstr(response, "Failed to read VCC");
        return;
    }

    CborEncoder *result = rpc_response_prepare_result(response);

    cbor_encode_float(result, vcc);

    rpc_response_send(response);
}
