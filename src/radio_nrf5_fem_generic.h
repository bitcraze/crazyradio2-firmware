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

/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file contains helper macros for dealing with the devicetree
 * radio node's fem property, in the case that it has compatible
 * "generic-fem-two-ctrl-pins".
 *
 * Do not include it directly.
 *
 * For these devices:
 *
 *  Value             Property
 *  ---------         --------
 *  PA pin            ctx-gpios
 *  PA offset         ctx-settle-time-us
 *  LNA pin           crx-gpios
 *  LNA offset        crx-settle-time-us
 */

#define HAL_RADIO_GPIO_PA_PROP_NAME         "ctx-gpios"
#define HAL_RADIO_GPIO_PA_OFFSET_PROP_NAME  "ctx-settle-time-us"
#define HAL_RADIO_GPIO_LNA_PROP_NAME        "crx-gpios"
#define HAL_RADIO_GPIO_LNA_OFFSET_PROP_NAME "crx-settle-time-us"

#if FEM_HAS_PROP(ctx_gpios)
#define HAL_RADIO_GPIO_HAVE_PA_PIN         1
#define HAL_RADIO_GPIO_PA_PROP             ctx_gpios

#define HAL_RADIO_GPIO_PA_OFFSET_MISSING   (!FEM_HAS_PROP(ctx_settle_time_us))
#define HAL_RADIO_GPIO_PA_OFFSET \
	DT_PROP_OR(FEM_NODE, ctx_settle_time_us, 0)
#else  /* !FEM_HAS_PROP(ctx_gpios) */
#define HAL_RADIO_GPIO_PA_OFFSET_MISSING 0
#endif	/* FEM_HAS_PROP(ctx_gpios) */

#if FEM_HAS_PROP(crx_gpios)
#define HAL_RADIO_GPIO_HAVE_LNA_PIN         1
#define HAL_RADIO_GPIO_LNA_PROP             crx_gpios

#define HAL_RADIO_GPIO_LNA_OFFSET_MISSING   (!FEM_HAS_PROP(crx_settle_time_us))
#define HAL_RADIO_GPIO_LNA_OFFSET \
	DT_PROP_OR(FEM_NODE, crx_settle_time_us, 0)
#else  /* !FEM_HAS_PROP(crx_gpios) */
#define HAL_RADIO_GPIO_LNA_OFFSET_MISSING 0
#endif	/* FEM_HAS_PROP(crx_gpios) */
