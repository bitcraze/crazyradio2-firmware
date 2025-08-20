#include <autoconf.h>

#include <zephyr/kernel.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/usb_device.h>
#include <usb_descriptor.h>
#include <zephyr/usb/bos.h>

#include "esb.h"
#include "fem.h"
#include "led.h"
#include "system.h"

#include <hal/nrf_radio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb);

struct usb_command {
    char payload[64];
    uint32_t length;
};

K_MSGQ_DEFINE(command_queue, sizeof(struct usb_command), 10, 4);

static void fw_scan(uint8_t start, uint8_t stop, char* data, int data_length);

// state
static struct {
    uint8_t datarate;
	uint8_t channel;
    bool ack_enabled;
    uint8_t scan_result[63];
    int scan_result_length;
} state = {
    .datarate = 2,
	.channel = 2,
    .ack_enabled = true,
};

#define CRAZYRADIO_NUM_EP 2
#define CRAZYRADIO_OUT_EP_ADDR 0x01
#define CRAZYRADIO_IN_EP_ADDR 0x81
#define CRAZYRADIO_BULK_EP_MPS 64

#define INITIALIZER_IF(num_ep, iface_class)				\
	{								\
		.bLength = sizeof(struct usb_if_descriptor),		\
		.bDescriptorType = USB_DESC_INTERFACE,			\
		.bInterfaceNumber = 0,					\
		.bAlternateSetting = 0,					\
		.bNumEndpoints = num_ep,				\
		.bInterfaceClass = iface_class,				\
		.bInterfaceSubClass = 0,				\
		.bInterfaceProtocol = 0,				\
		.iInterface = 0,					\
	}

#define INITIALIZER_IF_EP(addr, attr, mps, interval)			\
	{								\
		.bLength = sizeof(struct usb_ep_descriptor),		\
		.bDescriptorType = USB_DESC_ENDPOINT,			\
		.bEndpointAddress = addr,				\
		.bmAttributes = attr,					\
		.wMaxPacketSize = sys_cpu_to_le16(mps),			\
		.bInterval = interval,					\
	}

USBD_CLASS_DESCR_DEFINE(primary, 0) struct {
    struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_out_ep;
} __packed crazyradio_desc = {
	/* Interface descriptor 0 */
	.if0 = INITIALIZER_IF(CRAZYRADIO_NUM_EP, USB_BCC_VENDOR),
	.if0_out_ep = INITIALIZER_IF_EP(CRAZYRADIO_OUT_EP_ADDR, USB_DC_EP_BULK,
				       CRAZYRADIO_BULK_EP_MPS, 0),
	.if0_in_ep = INITIALIZER_IF_EP(CRAZYRADIO_IN_EP_ADDR, USB_DC_EP_BULK,
				       CRAZYRADIO_BULK_EP_MPS, 0),
};

void crazyradio_out_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
    uint32_t bytes_to_read;
    static struct usb_command command;

	usb_read(ep, NULL, 0, &bytes_to_read);
	LOG_DBG("ep 0x%x, bytes to read %d ", ep, bytes_to_read);
    if (bytes_to_read > 64) {
        bytes_to_read = 64;
    }
	usb_read(ep, command.payload, bytes_to_read, NULL);

	command.length = bytes_to_read;

    k_msgq_put(&command_queue, &command, K_FOREVER);

}

void crazyradio_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
	// Nop
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

//Vendor control messages and commands
#define SET_RADIO_CHANNEL 0x01
#define SET_RADIO_ADDRESS 0x02
#define SET_DATA_RATE     0x03
#define SET_RADIO_POWER   0x04
#define SET_RADIO_ARD     0x05
#define SET_RADIO_ARC     0x06
#define ACK_ENABLE        0x10
#define SET_CONT_CARRIER  0x20
#define CHANNEL_SCANN     0x21
#define SET_MODE          0x22
#define RESET_TO_BOOTLOADER 0xff

// nRF24 power mapping
// Those number are currently placeholder
// Todo: use hardware test results to set approximate values there
static uint8_t power_mapping[] = {
    1,   // -18dBm ->  2dBm for CRPA
    10,  // -12dBm ->  8dBm for CRPA
    20,  // -6dBm  -> 12dBm for CRPA
    31,  //  0dBm  -> 20dBm for CRPA
};

static int crazyradio_vendor_handler(struct usb_setup_packet *setup,
				   int32_t *len, uint8_t **data)
{
	LOG_DBG("Class request: bRequest 0x%x bmRequestType 0x%x len %d",
		setup->bRequest, setup->bmRequestType, *len);
	
	if (USB_REQTYPE_GET_TYPE(setup->bmRequestType) == USB_REQTYPE_TYPE_VENDOR) {
		
		if (setup->bRequest == SET_RADIO_CHANNEL && setup->wLength == 0) {
			LOG_DBG("Setting radio channel %d", setup->wValue);
			uint16_t channel = setup->wValue;
			if (channel <= 100) {
				esb_set_channel(channel);
			}
			// If channel >100 used, packets will be ignored (ie. virtually not acked)
			state.channel = channel;
		} else if (setup->bRequest == SET_RADIO_ADDRESS && setup->wLength == 5) {
			LOG_DBG("Setting radio address %02x%02x%02x%02x%02x", (unsigned int)(*data)[0], (unsigned int)(*data)[1], (unsigned int)(*data)[2], (unsigned int)(*data)[3], (unsigned int)(*data)[4]);
			esb_set_address(*data);
		} else if (setup->bRequest == SET_DATA_RATE && setup->wLength == 0 && setup->wValue < 3) {
			char* datarates[] = {"250K", "1M", "2M"};
			LOG_DBG("Setting radio datarate to %s", datarates[setup->wValue]);
			uint32_t datarate = setup->wValue;
            if (datarate == 1) {
				esb_set_bitrate(radioBitrate1M);
            } else if (datarate == 2) {
                esb_set_bitrate(radioBitrate2M);
            }
			// If 250K is selected, packets will be ignored (ie. virtually not acked)
			state.datarate = datarate;
		} else if (setup->bRequest == SET_RADIO_POWER && setup->wLength == 0) {
			LOG_DBG("Setting radio power %d", setup->wValue);
            uint8_t power = MIN(setup->wValue, 3);
            uint8_t fem_power = power_mapping[power];
            fem_set_power(fem_power);
		} else if (setup->bRequest == SET_RADIO_ARD && setup->wLength == 0) {
			LOG_DBG("Setting radio ARD %d", setup->wValue);
		} else if (setup->bRequest == SET_RADIO_ARC && setup->wLength == 0) {
			LOG_DBG("Setting radio ARC %d", setup->wValue & 0x0f);
            esb_set_arc(setup->wValue & 0x0f);
		} else if (setup->bRequest == ACK_ENABLE && setup->wLength == 0) {
            bool enabled = setup->wValue != 0;
			LOG_DBG("Setting radio ACK Enable %s", enabled?"true":"false");
            state.ack_enabled = enabled;
            esb_set_ack_enabled(enabled);
		} else if (setup->bRequest == SET_CONT_CARRIER && setup->wLength == 0) {
			LOG_DBG("Setting radio Continious carrier %s", setup->wValue?"true":"false");

            if (setup->wValue) {
                fem_txen_set(true);

                nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);
            } else {
                nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);

                fem_txen_set(false);
            }
        } else if (setup->bRequest == CHANNEL_SCANN && usb_reqtype_is_to_device(setup)) {
            uint8_t start = setup->wValue;
            uint8_t stop = setup->wIndex;
            fw_scan(start, stop, *data,setup->wLength);
        } else if (setup->bRequest == CHANNEL_SCANN && usb_reqtype_is_to_host(setup)) {
            *data = state.scan_result;
            *len = MIN(state.scan_result_length, setup->wLength);
		} else if (setup->bRequest == SET_MODE && setup->wLength == 0) {
			LOG_DBG("Setting radio Mode %d", setup->wValue);
        } else if (setup->bRequest == RESET_TO_BOOTLOADER && setup->wLength == 0) {
            system_reset_to_uf2();
		} else {
			return -ENOTSUP;
		}
    }

	return 0;
}

void crazyradio_status_cb(struct usb_cfg_data * data, enum usb_dc_status_code cb_status, const uint8_t *param)
{
	;
}

void crazyradio_interface_config(struct usb_desc_header *head, uint8_t bInterfaceNumber)
{
	;
}

// USBD_CFG_DATA_DEFINE(primary, crazyradio) struct usb_cfg_data crazyradio_config = {
USBD_DEFINE_CFG_DATA(crazyradio_config) = {
	.usb_device_description = NULL,
	.interface_config = crazyradio_interface_config,
	.interface_descriptor = &crazyradio_desc.if0,
	.cb_usb_status = crazyradio_status_cb,
	.interface = {
		.class_handler = NULL,
		.custom_handler = NULL,
		.vendor_handler = crazyradio_vendor_handler,
	},
	.num_endpoints = ARRAY_SIZE(ep_cfg),
	.endpoint = ep_cfg,
};

#define USB_THREAD_STACK_SIZE 500
#define USB_THREAD_PRIORITY 5

static void usb_thread(void *, void *, void *);

K_THREAD_DEFINE(usb_tid, USB_THREAD_STACK_SIZE,
                usb_thread, NULL, NULL, NULL,
                USB_THREAD_PRIORITY, 0, 0);

static void usb_thread(void *, void *, void *) {
    static struct usb_command command;
    static struct esbPacket_s packet;
    static struct esbPacket_s ack;
    uint8_t rssi;
    uint8_t arc_counter;

    while(1) {
        k_msgq_get(&command_queue, &command, K_FOREVER);

        // If we are not receiving ack (ie. broadcast) and the received data is > 32 bytes,
        // this means that the buffer actually contains 2 packets to send
        if (!state.ack_enabled && command.length > 32) {
            // Send the first one right away
            memcpy(packet.data, command.payload, command.length/2);
            packet.length = command.length/2;
            esb_send_packet(&packet, &ack, &rssi, &arc_counter);

            // And prepare the second one to be send by the normal execution flow
            memcpy(packet.data, &command.payload[command.length/2], command.length/2);
            packet.length = command.length/2;
        } else {
            // Otherwise, cap to 32 bytes and prepare the unicast packets
            if (command.length > 32) {
                command.length = 32;
            }
            memcpy(packet.data, command.payload, command.length);
            packet.length = command.length;
        }
        
        if (state.datarate != 0 && state.channel <= 100) {
            bool acked = esb_send_packet(&packet, &ack, &rssi, &arc_counter);
            if (ack.length > 32) {
                LOG_DBG("Got an ack of size %d!", ack.length);
            }
            if (!state.ack_enabled) {
                led_pulse_green(K_MSEC(50));
            } else if (acked && ack.length <= 32) {
                static char usb_answer[33];
                usb_answer[0] = (arc_counter & 0x0f) << 4 | (rssi < 64)<<1 | 1;
                memcpy(&usb_answer[1], ack.data, ack.length);
            
                if (usb_write(CRAZYRADIO_IN_EP_ADDR, usb_answer, ack.length + 1, NULL)) {
                    LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
                }

                led_pulse_green(K_MSEC(50));
            } else {
                char no_ack_answer[1] = {0};
        
                if (usb_write(CRAZYRADIO_IN_EP_ADDR, no_ack_answer, 1, NULL)) {
                    LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
                }

                led_pulse_red(K_MSEC(50));
            }

        } else {
            LOG_DBG("Not sending, radio settings not handled!");
            char no_ack_answer[1] = {0};
        
            if (usb_write(CRAZYRADIO_IN_EP_ADDR, no_ack_answer, 1, NULL)) {
                LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
            }

            led_pulse_red(K_MSEC(50));
        }

    }
}

static void fw_scan(uint8_t start, uint8_t stop, char* data, int data_length) {
    state.scan_result_length = 0;

    if (stop < start) {
        return;
    }

    static struct esbPacket_s packet;
    static struct esbPacket_s ack;
    uint8_t rssi;
    uint8_t retry;

    if (data_length > 32) {
        data_length = 32;
    }

    memcpy(packet.data, data, data_length);
    packet.length = data_length;

    for (int channel = start; channel <= stop; channel++) {
        esb_set_channel(channel);
        if (esb_send_packet(&packet, &ack, &rssi, &retry)) {
            led_pulse_green(K_MSEC(50));
            state.scan_result[state.scan_result_length++] = channel;
            if (state.scan_result_length >= 63) {
                return;
            }
        } else {
            led_pulse_red(K_MSEC(50));
        }
    }
}
