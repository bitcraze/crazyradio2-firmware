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

#include "oryx_ll.h"

#include <zephyr/kernel.h>

static K_SEM_DEFINE(scan_start, 0, 1);
static detected_cb_fn sim_detected_cb;
static timeout_cb_fn sim_timeout_cb;
static int sim_timeout_ms;

bool oryx_ll_scan(int channel, int timeout_ms, detected_cb_fn detected_cb, timeout_cb_fn timeout_cb) {
    sim_detected_cb = detected_cb;
    sim_timeout_cb = timeout_cb;
    sim_timeout_ms = timeout_ms;
    k_sem_give(&scan_start);
    return true;
}

static void oryx_ll_scan_sim() {
    static oryx_ll_detected_t sim_crazyflie = {
        .mac_address = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55},
        .rssi = 42,
    };

    while(1) {
        k_sem_take(&scan_start, K_FOREVER);
        sim_detected_cb(&sim_crazyflie);
        k_sleep(K_MSEC(sim_timeout_ms / 2));
        sim_crazyflie.rssi = 67;
        sim_detected_cb(&sim_crazyflie);
        k_sleep(K_MSEC(sim_timeout_ms / 2));
        sim_timeout_cb();
    }
}

static K_THREAD_DEFINE(simulator_thread, 300, oryx_ll_scan_sim, NULL, NULL, NULL,
		5, 0, 0);