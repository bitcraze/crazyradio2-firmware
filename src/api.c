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

#include "api.h"

#include <zephyr/kernel.h>

#include "radio_mode.h"
#include "esb.h"
#include "oryx.h"
#include "contwave.h"
#include "led.h"
#include "button.h"

// Utility APIs

static void version(const rpc_request_t *request, rpc_response_t *response) {
	CborEncoder *result = rpc_response_prepare_result(response);

	CborEncoder version_encoder;
	cbor_encoder_create_map(result, &version_encoder, 1);

	cbor_encode_text_stringz(&version_encoder, "zephyr");
	CborEncoder zephyr_encoder;
	cbor_encoder_create_array(&version_encoder, &zephyr_encoder, 3);

	uint32_t zephyr_version = sys_kernel_version_get();
	cbor_encode_uint(&zephyr_encoder, SYS_KERNEL_VER_MAJOR(zephyr_version));
	cbor_encode_uint(&zephyr_encoder, SYS_KERNEL_VER_MINOR(zephyr_version));
	cbor_encode_uint(&zephyr_encoder, SYS_KERNEL_VER_PATCHLEVEL(zephyr_version));
	cbor_encoder_close_container(&version_encoder, &zephyr_encoder);

	cbor_encoder_close_container(result, &version_encoder);

	rpc_response_send(response);
}

static void notify_example(const rpc_request_t *request) {
	;
}

static rpc_notification_t test_notification;
static char test_notification_buffer[RPC_MTU];

static void test_send_notification(const rpc_request_t *request, rpc_response_t *response) {
	rpc_response_create_notification(response, &test_notification, test_notification_buffer);

	// Return answer to this request
	rpc_response_send(response);

	// Later when a notification needs to be sent ...
	CborEncoder *param = rpc_notification_prepare_param(&test_notification, "testNotification");

	cbor_encode_text_stringz(param, "hello notification");

	rpc_notification_send(&test_notification);
}

// API definition

static rpc_method_t methods[] = {
	{.name = "version", .method = version },
	{.name = "notify", .notify = notify_example},
	{.name = "testSendNotification", .method = test_send_notification},
    {.name = "radioMode.list", .method = radio_mode_list_rpc},
    {.name = "radioMode.get", .method = radio_mode_get_rpc},
    {.name = "radioMode.set", .method = radio_mode_set_rpc},
    {.name = "esb.sendPacket", .method = esb_send_packet_rpc},
	{.name = "oryx.scan", .method = oryx_scan_rpc},
	{.name = "contWave.start", .method = contwave_start_rpc},
	{.name = "contWave.stop", .method = contwave_stop_rpc},
	{.name = "led.set", .method = led_set_rpc},
	{.name = "button.get", .method = button_get_rpc},
};

rpc_api_t crazyradio2_rpc_api = {
	.methods = methods,
	.methods_length = sizeof(methods) / sizeof(methods[0]),
};
