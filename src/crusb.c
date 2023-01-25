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

#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/bos.h>

#include "crusb.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb);

// Vendor requests
#define CRUSB_VERSION_REQUEST (0)

// Message queues
K_MSGQ_DEFINE(tx_queue, sizeof(struct crusb_message), 5, 4);
K_MSGQ_DEFINE(rx_queue, sizeof(struct crusb_message), 5, 4);

// Semaphores
K_SEM_DEFINE(tx_sem, 0, 1);

// Threads
static void tx_thread(void*, void*, void*);
#define TX_THREAD_PRIORITY 2
#define TX_THREAD_STACK_SIZE 500

K_THREAD_DEFINE(my_tid, TX_THREAD_STACK_SIZE,
                tx_thread, NULL, NULL, NULL,
                TX_THREAD_PRIORITY, 0, 0);


struct usb_crazyradio_config {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
} __packed;

#define CRAZYRADIO_OUT_EP_ADDR 0x01
#define CRAZYRADIO_IN_EP_ADDR 0x81
#define CONFIG_CRAZYRADIO_BULK_EP_MPS 64

USBD_CLASS_DESCR_DEFINE(primary, 0) struct usb_crazyradio_config crazyradio_cfg = {
	/* Interface descriptor 0 */
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},

	/* Data Endpoint OUT */
	.if0_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = CRAZYRADIO_OUT_EP_ADDR,
		.bmAttributes = USB_DC_EP_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(CONFIG_CRAZYRADIO_BULK_EP_MPS),
		.bInterval = 0x00,
	},

	/* Data Endpoint IN */
	.if0_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = CRAZYRADIO_IN_EP_ADDR,
		.bmAttributes = USB_DC_EP_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(CONFIG_CRAZYRADIO_BULK_EP_MPS),
		.bInterval = 0x00,
	},
};

// The out callback perform packet assembly from the packet stream.
// Every packet (messages) come as a 10 bit length followed by the data.
void crazyradio_out_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
	// State of the packet receiving FSM
	static enum {state_len0, state_len1, state_data} state = state_len0;
	// Number of data bytes already read when in data state
	static int data_bytes_read;
	static struct crusb_message message;

	uint32_t usb_data_left;
	usb_read(ep, NULL, 0, &usb_data_left);

	LOG_DBG("%d USB bytes available", usb_data_left);

	// Reset the state machine on receiving a 0 length packet
	if (usb_data_left == 0) {
		LOG_DBG("Reseting RX FSM");
		// A dummy read is required
		usb_read(ep, message.data, 64, NULL);
		state = state_len0;
		return;
	}

	while (usb_data_left > 0) {
		LOG_DBG("State: %s (%d, %d)", ((state==state_len0)?"len0":((state==state_len1)?"len1":"data")), message.length, data_bytes_read);

		switch (state) {
			case state_len0:
				message.length = 0;
				data_bytes_read = 0;

				char len0 = 0;
				usb_read(ep, &len0, 1, NULL);
				usb_data_left --;

				message.length |= (uint16_t)len0;

				state = state_len1;
				break;
			case state_len1: {
				char len1 = 0;
				usb_read(ep, &len1, 1, NULL);
				usb_data_left --;

				message.length |= ((uint16_t)len1) << 8;
				message.length &= 0x03FF;  // Limit the length to 10 bits, this guarantee we are within USB_MTU

				state = state_data;
			}
				if (message.length > 0) {
					break;
				}
			case state_data: {
				int to_read = MIN(message.length - data_bytes_read, usb_data_left);

				if (to_read > 0) {
					usb_read(ep, &message.data[data_bytes_read], to_read, NULL);
					usb_data_left -= to_read;
					data_bytes_read += to_read;
				}

				if (data_bytes_read == message.length) {
					LOG_DBG("Application packet of %d bytes received!", message.length);

					// We can block as long as required here, this implements flow control
					// This means that the rx_queue must be emptied otherwise it will freeze communication
					// TODO: See if a long timeout+assert is better here.
					k_msgq_put(&rx_queue, &message, K_FOREVER);

					// After the data, comes the length of the next message
					state = state_len0;
				} // Else, there is more data to receive in the next USB packet ...
			}
				break;
			default:
				__ASSERT(0, "USB OUT decoding invalid state")
		}
	}
}

void crazyradio_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
	LOG_DBG("In callback for EP %x", ep);
	k_sem_give(&tx_sem);
}

static void tx_thread(void* p1, void* p2, void* p3) {
	static struct crusb_message message = {0,};
	static char usb_buffer[CONFIG_CRAZYRADIO_BULK_EP_MPS];
	int usb_buffer_len = 0;
	enum {state_len0, state_len1, state_data} state = state_len0;
	int sent_message_bytes = 0;
	bool done = false;

	LOG_DBG("Starting CRUSB TX task");

	while (1) {
		if (state == state_len0) {
			k_msgq_get(&tx_queue, &message, K_FOREVER);
			sent_message_bytes = 0;
		}

		done = false;
		while (usb_buffer_len < CONFIG_CRAZYRADIO_BULK_EP_MPS && !done) {
			switch (state) {
			case state_len0:
				usb_buffer[usb_buffer_len] = message.length & 0x0ff;
				usb_buffer_len ++;
				state = state_len1;
				break;
			case state_len1:
				usb_buffer[usb_buffer_len] = (message.length >> 8) & 0x0ff;
				usb_buffer_len ++;
				state = state_data;
				break;
			case state_data: {
				int to_send = MIN(message.length - sent_message_bytes, CONFIG_CRAZYRADIO_BULK_EP_MPS - usb_buffer_len);
				memcpy(&usb_buffer[usb_buffer_len], &message.data[sent_message_bytes], to_send);
				usb_buffer_len += to_send;
				sent_message_bytes += to_send;

				// Test if this is the end of the message!
				if (sent_message_bytes == message.length) {
					state = state_len0;

					// Check that we can send another queued message in the same packet
					if (usb_buffer_len < CONFIG_CRAZYRADIO_BULK_EP_MPS && k_msgq_get(&tx_queue, &message, K_NO_WAIT) == 0) {
						sent_message_bytes = 0;
					} else {
						// Otherwise, the loop will end and send what we have so far
						done = true;
					}
				} else {
					// Else, the packet will be sent and we will pack the rest in the next packet
					done = true;
				}
			}
			default:
				__ASSERT(0, "USB IN encoding invalid state")
			}
		}

		LOG_DBG("Sending a %d bytes packet", usb_buffer_len);
		usb_write(CRAZYRADIO_IN_EP_ADDR, usb_buffer, usb_buffer_len, NULL);
		// Wait for the packet to be sent ...
		// TODO: it might improve performance to wait before writing and so to
		// have a full new packet prepared before the next write.
		k_sem_take(&tx_sem, K_FOREVER);

		// If the buffer was exactly as large as the USB max packet size while there is no more message in queue
		// This unlock the receiver to make sure it does not timeout waiting for more data
		if (usb_buffer_len == CONFIG_CRAZYRADIO_BULK_EP_MPS && state == state_len0 && k_msgq_num_used_get(&tx_queue) == 0) {
			LOG_DBG("Sending a %d bytes packet", 0);
			usb_write(CRAZYRADIO_IN_EP_ADDR, usb_buffer, 0, NULL);
			k_sem_take(&tx_sem, K_FOREVER);
		}
		usb_buffer_len = 0;
	}
}

static struct usb_ep_cfg_data ep_cfg[] = {
	{
		.ep_cb = crazyradio_out_cb,
		.ep_addr = CRAZYRADIO_OUT_EP_ADDR,
	},
	{
		.ep_cb = crazyradio_in_cb,
		.ep_addr = CRAZYRADIO_IN_EP_ADDR,
	},
};

static int crazyradio_vendor_handler(struct usb_setup_packet *setup,
				   int32_t *len, uint8_t **data)
{
	if (setup->RequestType.recipient == USB_REQTYPE_RECIPIENT_INTERFACE
	    && setup->RequestType.direction == USB_REQTYPE_DIR_TO_HOST
		&& setup->bRequest == CRUSB_VERSION_REQUEST) {

		*len = setup->wLength;
		*data[0] = CRUSB_VERSION;
		return 0;

	}

	return -ENOTSUP;
}

void crazyradio_status_cb(struct usb_cfg_data * data, enum usb_dc_status_code cb_status, const uint8_t *param)
{
	;
}

void crazyradio_interface_config(struct usb_desc_header *head, uint8_t bInterfaceNumber)
{
	;
}

USBD_CFG_DATA_DEFINE(primary, crazyradio) struct usb_cfg_data crazyradio_config = {
	.usb_device_description = NULL,
	.interface_config = crazyradio_interface_config,
	.interface_descriptor = &crazyradio_cfg.if0,
	.cb_usb_status = crazyradio_status_cb,
	.interface = {
		.class_handler = NULL,
		.custom_handler = NULL,
		.vendor_handler = crazyradio_vendor_handler,
	},
	.num_endpoints = ARRAY_SIZE(ep_cfg),
	.endpoint = ep_cfg,
};

// ****************** Public API *****************

void crusb_send(const struct crusb_message *message) {
	k_msgq_put(&tx_queue, message, K_FOREVER);
}

void crusb_receive(struct crusb_message *message) {
	k_msgq_get(&rx_queue, message, K_FOREVER);
}
