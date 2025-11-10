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

#include "fem.h"

#include <zephyr/sys/dlist.h>
#include <zephyr/toolchain.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <zephyr/dt-bindings/spi/spi.h>
#include <zephyr/drivers/spi.h>
#include <soc.h>

#include <hal/nrf_rtc.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_ccm.h>
#include <hal/nrf_aar.h>

#include "nrf.h"
#include "hal/nrf_gpio.h"
#include "hal/nrf_gpiote.h"
#include "hal/nrf_ppi.h"

#include "led.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fem);

// #include "util/mem.h"

// #include "hal/ccm.h"
// #include "hal/radio.h"
// #include "hal/ticker.h"

#include "radio_nrf5_fem.h"

static void write_register(uint8_t address, uint8_t value);
static uint8_t read_register(uint8_t address);

/* Converts the GPIO controller in a FEM property's GPIO specification
 * to its nRF register map pointer.
 *
 * Make sure to use NRF_DT_CHECK_GPIO_CTLR_IS_SOC to check the GPIO
 * controller has the right compatible wherever you use this.
 */
#define NRF_FEM_GPIO(prop) \
	((NRF_GPIO_Type *)DT_REG_ADDR(DT_GPIO_CTLR(FEM_NODE, prop)))

/* Converts GPIO specification to a PSEL value. */
#define NRF_FEM_PSEL(prop) NRF_DT_GPIOS_TO_PSEL(FEM_NODE, prop)

/* Check if GPIO flags are active low. */
#define ACTIVE_LOW(flags) ((flags) & GPIO_ACTIVE_LOW)

/* Check if GPIO flags contain unsupported values. */
#define BAD_FLAGS(flags) ((flags) & ~GPIO_ACTIVE_LOW)

/* GPIOTE OUTINIT setting for a pin's inactive level, from its
 * devicetree flags.
 */
#define OUTINIT_INACTIVE(flags)			\
	(ACTIVE_LOW(flags) ?				\
	 GPIOTE_CONFIG_OUTINIT_High :			\
	 GPIOTE_CONFIG_OUTINIT_Low)

#if defined(FEM_NODE)
BUILD_ASSERT(!HAL_RADIO_GPIO_PA_OFFSET_MISSING,
	     "fem node " DT_NODE_PATH(FEM_NODE) " has property "
	     HAL_RADIO_GPIO_PA_PROP_NAME " set, so you must also set "
	     HAL_RADIO_GPIO_PA_OFFSET_PROP_NAME);

BUILD_ASSERT(!HAL_RADIO_GPIO_LNA_OFFSET_MISSING,
	     "fem node " DT_NODE_PATH(FEM_NODE) " has property "
	     HAL_RADIO_GPIO_LNA_PROP_NAME " set, so you must also set "
	     HAL_RADIO_GPIO_LNA_OFFSET_PROP_NAME);
#endif	/* FEM_NODE */

/*
 * "Manual" conversions of devicetree values to register bits. We
 * can't use the Zephyr GPIO API here, so we need this extra
 * boilerplate.
 */

#if defined(HAL_RADIO_GPIO_HAVE_PA_PIN)
struct gpio_dt_spec tx_en_gpio = GPIO_DT_SPEC_GET(FEM_NODE, tx_en_gpios);
#endif /* HAL_RADIO_GPIO_HAVE_PA_PIN */

#if defined(HAL_RADIO_GPIO_HAVE_LNA_PIN)
struct gpio_dt_spec rx_en_gpio = GPIO_DT_SPEC_GET(FEM_NODE, rx_en_gpios);
#endif /* HAL_RADIO_GPIO_HAVE_LNA_PIN */

#if defined(HAL_RADIO_FEM_IS_NRF21540)

#if DT_NODE_HAS_PROP(FEM_NODE, pdn_gpios)
struct gpio_dt_spec pdn_gpio = GPIO_DT_SPEC_GET(FEM_NODE, pdn_gpios);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, pdn_gpios) */

#if DT_NODE_HAS_PROP(FEM_NODE, mode_gpios)
struct gpio_dt_spec mode_gpio = GPIO_DT_SPEC_GET(FEM_NODE, mode_gpios);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, mode_gpios) */

#if DT_NODE_HAS_PROP(FEM_NODE, ant_sel_gpios)
struct gpio_dt_spec ant_sel_gpio = GPIO_DT_SPEC_GET(FEM_NODE, ant_sel_gpios);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, ant_sel_gpios) */

/* CSN is special because it comes from the spi-if property. */
#if defined(HAL_RADIO_FEM_NRF21540_HAS_CSN)
struct gpio_dt_spec csn_gpio = SPI_CS_GPIOS_DT_SPEC_GET(FEM_SPI_DEV_NODE);
struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(FEM_SPI_DEV_NODE, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0);
#endif	/* HAL_RADIO_FEM_NRF21540_HAS_CSN */

#endif	/* HAL_RADIO_FEM_IS_NRF21540 */

void fem_init() {
#if defined(HAL_RADIO_FEM_IS_NRF21540)

	/* Configure the FEM pins. */
#if defined(HAL_RADIO_GPIO_HAVE_PA_PIN)
	gpio_pin_configure_dt(&tx_en_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&tx_en_gpio, 0);
#endif /* HAL_RADIO_GPIO_HAVE_PA_PIN */

#if defined(HAL_RADIO_GPIO_HAVE_LNA_PIN)
	gpio_pin_configure_dt(&rx_en_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&rx_en_gpio, 0);
#endif /* HAL_RADIO_GPIO_HAVE_LNA_PIN */

	// Always enable the FEM.
	// TODO: Add support for enabling the FEM only when needed.
#if DT_NODE_HAS_PROP(FEM_NODE, pdn_gpios)
	gpio_pin_configure_dt(&pdn_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&pdn_gpio, 1);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, pdn_gpios) */

#if DT_NODE_HAS_PROP(FEM_NODE, mode_gpios)
	gpio_pin_configure_dt(&mode_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&mode_gpio, 0);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, mode_gpios) */

#if DT_NODE_HAS_PROP(FEM_NODE, ant_sel_gpios)
	gpio_pin_configure_dt(&ant_sel_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&ant_sel_gpio, 0);
#endif	/* DT_NODE_HAS_PROP(FEM_NODE, ant_sel_gpios) */

	struct spi_buf_set buffer = {
		.buffers = (struct spi_buf[]) {
			{
				.buf = (uint8_t[]) { 0x96, 0x00 },
				.len = 2,
			},
		},
		.count = 1,
	};

	struct spi_buf_set rxbuffer = {
		.buffers = (struct spi_buf[]) {
			{
				.buf = (uint8_t[]) { 0x96, 0x00 },
				.len = 2,
			},
		},
		.count = 1,
	};

	int ret = spi_transceive_dt(&spi_dev, &buffer, &rxbuffer);

    uint8_t *data = rxbuffer.buffers[0].buf;

	LOG_DBG("ret: %d, data: %x %x", ret, data[0], data[1]);

	if (data[1] != 0x02) {
		// TODO: Handle FEM communication error!
	}
#endif
}

void fem_txen_set(bool enable) {
#if defined(HAL_RADIO_GPIO_HAVE_PA_PIN)
	gpio_pin_set_dt(&tx_en_gpio, enable);
#endif
}

void fem_rxen_set(bool enable) {
#if defined(HAL_RADIO_GPIO_HAVE_LNA_PIN)
	gpio_pin_set_dt(&rx_en_gpio, enable);
#endif
}

void fem_set_power(uint8_t power) {
#if defined(HAL_RADIO_FEM_IS_NRF21540)
	uint8_t tx_gain = power & 0x1f;
	uint8_t confreg0 = tx_gain << 2;

	write_register(0x00, confreg0);
#endif
}

static void write_register(uint8_t address, uint8_t value) {
#if defined(HAL_RADIO_FEM_IS_NRF21540)
	uint8_t command = 0xC0 | (address & 0x3F);

	struct spi_buf_set buffer = {
		.buffers = (struct spi_buf[]) {
			{
				.buf = (uint8_t[]) { command, value },
				.len = 2,
			},
		},
		.count = 1,
	};

	LOG_DBG("write_register: %x %x", address, value);

	spi_write_dt(&spi_dev, &buffer);
#endif
}

static uint8_t read_register(uint8_t address) {
#if defined(HAL_RADIO_FEM_IS_NRF21540)
	uint8_t command = 0x80 | (address & 0x3F);

	struct spi_buf_set buffer = {
		.buffers = (struct spi_buf[]) {
			{
				.buf = (uint8_t[]) { command, 0x00 },
				.len = 2,
			},
		},
		.count = 1,
	};

	struct spi_buf_set rxbuffer = {
		.buffers = (struct spi_buf[]) {
			{
				.buf = (uint8_t[]) { 0x00, 0x00 },
				.len = 2,
			},
		},
		.count = 1,
	};

	spi_transceive_dt(&spi_dev, &buffer, &rxbuffer);

	uint8_t *data = rxbuffer.buffers[0].buf;

	LOG_DBG("read_register: %x %x", address, data[1]);

	return data[1];
#else
	return 0;
#endif
}

void fem_set_antenna(uint8_t antenna) {
#if defined(HAL_RADIO_FEM_IS_NRF21540)
	// Set antsel
	gpio_pin_set_dt(&ant_sel_gpio, antenna);
#endif
}

bool fem_is_lna_enabled(void) {
	uint8_t confreg1 = read_register(0x01);
	bool enabled = (confreg1 & 0x01) == 0x01;

	return enabled;
}

bool fem_is_pa_enabled(void) {
	uint8_t confreg0 = read_register(0x00);
	bool enabled = (confreg0 & 0x01) == 0x01;

	return enabled;
}

