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
#include <zephyr/drivers/gpio.h>

#include "rpc.h"


static const struct adc_dt_spec adc_chan0 =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static const struct adc_dt_spec adc_io1 =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

static const struct gpio_dt_spec io2 = GPIO_DT_SPEC_GET(DT_ALIAS(io2), gpios);
static const struct gpio_dt_spec io3 = GPIO_DT_SPEC_GET(DT_ALIAS(io3), gpios);

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

static int system_read_io1(float *measurement) {
    int ret;
    int16_t buf;

    if (!device_is_ready(adc_io1.dev)) {
        printk("ADC controller device not ready\n");
        return -ENODEV;
    }

    ret = adc_channel_setup_dt(&adc_io1);
    if (ret < 0) {
        printk("Could not setup channel with error %d\n", ret);
        return ret;
    }

	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

    adc_sequence_init_dt(&adc_io1, &sequence);

    ret = adc_read(adc_io1.dev, &sequence);
    if (ret < 0) {
        printk("ADC read failed with error %d\n", ret);
        return ret;
    }

    int32_t val_mv = buf;
    ret = adc_raw_to_millivolts_dt(&adc_io1, &val_mv);
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

void system_test_ios_rpc(const rpc_request_t *request, rpc_response_t *response) {
    gpio_pin_configure_dt(&io2, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&io3, GPIO_OUTPUT_ACTIVE);

    
    gpio_pin_set_dt(&io2, 1);
    gpio_pin_set_dt(&io3, 0);

    k_sleep(K_MSEC(100));

    float io1_0 = 0;
    int ret = system_read_io1(&io1_0);
    if (ret < 0) {
        rpc_response_send_errorstr(response, "Failed to read IO1");
        return;
    }

    gpio_pin_set_dt(&io2, 0);
    gpio_pin_set_dt(&io3, 1);

    k_sleep(K_MSEC(100));

    float io1_1 = 0;
    ret = system_read_io1(&io1_1);
    if (ret < 0) {
        rpc_response_send_errorstr(response, "Failed to read IO1");
        return;
    }

    CborEncoder *result = rpc_response_prepare_result(response);

    CborEncoder array;
    cbor_encoder_create_array(result, &array, 2);

    cbor_encode_float(&array, io1_0);
    cbor_encode_float(&array, io1_1);

    cbor_encoder_close_container(result, &array);

    rpc_response_send(response);
}

void system_reset_to_uf2(void) {
  NRF_POWER->GPREGRET = 0x57; // 0xA8 OTA, 0x4e Serial
  NVIC_SystemReset();         // or sd_nvic_SystemReset();
}