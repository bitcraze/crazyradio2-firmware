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

static void red_timer_expired(struct k_timer *dummy) {
    led_set_red(false);
}

static void green_timer_expired(struct k_timer *dummy) {
    led_set_green(false);
}

static void blue_timer_expired(struct k_timer *dummy) {
    led_set_blue(false);
}

K_TIMER_DEFINE(red_timer, red_timer_expired, NULL);
K_TIMER_DEFINE(green_timer, green_timer_expired, NULL);
K_TIMER_DEFINE(blue_timer, blue_timer_expired, NULL);

void led_pulse_red(k_timeout_t time) {
    led_set_red(true);
    k_timer_start(&red_timer, time, K_FOREVER);
}

void led_pulse_green(k_timeout_t time) {
    led_set_green(true);
    k_timer_start(&green_timer, time, K_FOREVER);
}

void led_pulse_blue(k_timeout_t time) {
    led_set_blue(true);
    k_timer_start(&blue_timer, time, K_FOREVER);
}
