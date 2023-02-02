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

#include "led.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "rpc.h"

#define RED_LED_NODE DT_ALIAS(led0)
#define GREEN_LED_NODE DT_ALIAS(led1)
#define BLUE_LED_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec redLed = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);
static const struct gpio_dt_spec greenLed = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);
static const struct gpio_dt_spec blueLed = GPIO_DT_SPEC_GET(BLUE_LED_NODE, gpios);

void led_init()
{
	gpio_pin_configure_dt(&redLed, GPIO_OUTPUT_ACTIVE);
    led_set_red(false);

	gpio_pin_configure_dt(&greenLed, GPIO_OUTPUT_ACTIVE);
    led_set_green(false);

    gpio_pin_configure_dt(&blueLed, GPIO_OUTPUT_ACTIVE);
    led_set_blue(false);
}

void led_set_red(bool on)
{
    gpio_pin_set_dt(&redLed, (int)on);
}

void led_set_green(bool on)
{
    gpio_pin_set_dt(&greenLed, (int)on);
}

void led_set_blue(bool on)
{
    gpio_pin_set_dt(&blueLed, (int)on);
}

void led_set_rpc(const rpc_request_t *request, rpc_response_t *response) {
    // Check that the request is an array of 3 elements
    int len=0;
    cbor_value_get_array_length(&request->param, &len);
    if (!cbor_value_is_array(&request->param) || len != 3) {
        rpc_response_send_errorstr(response, "Invalid parameter");
        return;
    }

    CborValue colors;
    cbor_value_enter_container(&request->param, &colors);

    // First element is red
    bool redOn;
    cbor_value_get_boolean(&colors, &redOn);
    led_set_red(redOn);

    // Second element is green
    cbor_value_advance(&colors);
    bool greenOn;
    cbor_value_get_boolean(&colors, &greenOn);
    led_set_green(greenOn);

    // Third element is blue
    cbor_value_advance(&colors);
    bool blueOn;
    cbor_value_get_boolean(&colors, &blueOn);
    led_set_blue(blueOn);

    rpc_response_send(response);
}