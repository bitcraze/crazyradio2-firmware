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

#include <stddef.h>

#include "esb.h"
#include "radio_mode.h"
#include "contwave.h"
#include "power_measurement.h"

static struct {
    char* name;
    void (*init)(void);
    void (*deinit)(void);
} modes[] = {
    {.name = "disable", .init = NULL, .deinit = NULL},
    {.name = "esb", .init = esb_init, .deinit = esb_deinit},
    {.name = "contWave", .init = contwave_init, .deinit = contwave_deinit},
    {.name = "powerMeasurement", .init = power_measurement_init, .deinit = power_measurement_deinit},
};

// static const int modes_length = sizeof(modes) / sizeof(modes[0]);

static int current_mode = 0;

void radio_mode_init() {
    if (modes[current_mode].init) {
        modes[current_mode].init();
    }
}
