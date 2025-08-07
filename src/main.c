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

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <zephyr/usb/usb_device.h>

#include "radio_mode.h"
#include "led.h"
#include "fem.h"
#include "esb.h"

#include <nrfx_clock.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int startHFClock(void)
{
    nrfx_clock_hfclk_start();
	return 0;
}

void main(void)
{
    printk("Hello World! %s\n", CONFIG_BOARD);

    // HFCLK crystal is needed by the radio and USB
    startHFClock();

    led_init();
	led_pulse_green(K_MSEC(500));
	led_pulse_red(K_MSEC(500));
	led_pulse_blue(K_MSEC(500));

	esb_init();

	fem_init();

	// Test the FEM
	printk("Enaabling TX\n");
	fem_txen_set(true);
	k_sleep(K_MSEC(10));
	bool enabled = fem_is_pa_enabled();
	printk("PA enabled: %d\n", enabled);
	fem_txen_set(false);

	printk("Enaabling RX\n");
	fem_rxen_set(true);
	k_sleep(K_MSEC(10));
	enabled = fem_is_lna_enabled();
	printk("LNA enabled: %d\n", enabled);
	fem_rxen_set(false);

    int ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return;
	}

	while(1) {
		k_sleep(K_MSEC(1000));
	}
}
