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

#define USB_ANSWER_MAX_LENGTH 128

#include <hal/nrf_radio.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb);

enum command_type {
    command_data,
    command_setup,
};

char* command_type_name[] = {
    "data",
    "setup",
};

struct data_command {
    char payload[USB_ANSWER_MAX_LENGTH];
    uint32_t length;
};

struct setup_command {
    struct usb_setup_packet setup_packet;
    uint32_t length;
    char data[16];
};

struct usb_command {
    enum command_type type;
    union {
        struct data_command data;
        struct setup_command setup;
    };
};

struct usb_response {
    uint32_t inline_seq;
    uint32_t length;
    char payload[USB_ANSWER_MAX_LENGTH];
};

K_MSGQ_DEFINE(command_queue, sizeof(struct usb_command), 10, 4);
K_MSGQ_DEFINE(sniffer_queue, sizeof(struct esbSnifferPacket_s), 8, 4);
K_MSGQ_DEFINE(response_queue, sizeof(struct usb_response), 10, 4);

K_MUTEX_DEFINE(usb_radio_mutex);
K_SEM_DEFINE(usb_in_ready, 1, 1);

static atomic_t sniffer_drop_count;
static atomic_t inline_tx_sequence;

static void fw_scan(uint8_t start, uint8_t stop, char* data, int data_length);
static void handle_vendor_command(struct setup_command* setup);

// state
static struct {
    uint8_t datarate;
	uint8_t channel;
    bool ack_enabled;
    uint8_t scan_result[ESB_MAX_PAYLOAD_LENGTH];
    int scan_result_length;
    bool inline_mode;
    bool inline_rssi_mode;
    bool sniffer_mode;
    char usb_answer[USB_ANSWER_MAX_LENGTH];
} state = {
    .datarate = 2,
	.channel = 42,
    .ack_enabled = true,
    .inline_mode = false,
    .inline_rssi_mode = false,
    .sniffer_mode = false,
};

// Inline mode out header
typedef struct {
    uint8_t length;        // Full length including this header
    uint8_t datarate: 2;
    uint8_t reserved_0: 2;
    uint8_t ack_enabled: 1;
    uint8_t reserved_1: 3;
    uint8_t channel;
    uint8_t address[5];
} __attribute__((packed)) inline_mode_out_header;

// Inline mode in header
typedef struct {
    uint8_t length;
    uint8_t ack_received: 1;
    uint8_t rssi_lt_64dbm: 1;
    uint8_t invalid_settings: 1;
    uint8_t reserved: 1;
    uint8_t arc_counter: 4;
} __attribute__((packed)) inline_mode_in_header;

// Inline with rssi mode in header
typedef struct {
    uint8_t length;
    uint8_t ack_received: 1;
    uint8_t rssi_lt_64dbm: 1;
    uint8_t invalid_settings: 1;
    uint8_t reserved: 1;
    uint8_t arc_counter: 4;
    uint8_t rssi_dbm;
} __attribute__((packed)) inline_rssi_mode_in_header;

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

static void sniffer_rx_callback(const struct esbSnifferPacket_s *pkt)
{
    if (k_msgq_put(&sniffer_queue, pkt, K_NO_WAIT) != 0) {
        atomic_inc(&sniffer_drop_count);
    }
}

static int inline_usb_write(uint32_t seq, const void *data, uint32_t length)
{
    struct usb_response response = {
        .inline_seq = seq,
        .length = MIN(length, USB_ANSWER_MAX_LENGTH),
    };

    memcpy(response.payload, data, response.length);

    int ret = k_msgq_put(&response_queue, &response, K_MSEC(100));
    if (ret != 0) {
        LOG_WRN("inline[%u] response queue full, dropping response: ret=%d len=%u", seq, ret, length);
    } else {
        LOG_DBG("inline[%u] queued USB IN response: len=%u", seq, response.length);
    }

    return ret;
}

void crazyradio_out_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
    uint32_t bytes_to_read;
    static struct usb_command command;
    static bool accumulating = false;

    usb_read(ep, NULL, 0, &bytes_to_read);

    if (accumulating) {
        uint32_t offset = command.data.length;
        uint32_t space = sizeof(command.data.payload) - offset;
        if (bytes_to_read <= space) {
            usb_read(ep, &command.data.payload[offset], bytes_to_read, NULL);
            command.data.length += bytes_to_read;
        } else {
            usb_read(ep, &command.data.payload[offset], space, NULL);
            command.data.length += space;
            uint8_t scratch[CRAZYRADIO_BULK_EP_MPS];
            usb_read(ep, scratch, bytes_to_read - space, NULL);
        }
        if (bytes_to_read < CRAZYRADIO_BULK_EP_MPS) {
            accumulating = false;
            k_msgq_put(&command_queue, &command, K_FOREVER);
        }
        return;
    }

    command.type = command_data;
    if (bytes_to_read > sizeof(command.data.payload)) {
        usb_read(ep, command.data.payload, sizeof(command.data.payload), NULL);
        uint8_t scratch[CRAZYRADIO_BULK_EP_MPS];
        usb_read(ep, scratch, bytes_to_read - sizeof(command.data.payload), NULL);
        command.data.length = sizeof(command.data.payload);
    } else {
        usb_read(ep, command.data.payload, bytes_to_read, NULL);
        command.data.length = bytes_to_read;
    }

    if (bytes_to_read == CRAZYRADIO_BULK_EP_MPS) {
        accumulating = true;
        return;
    }
    k_msgq_put(&command_queue, &command, K_FOREVER);
}

void crazyradio_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code cb_status)
{
    k_sem_give(&usb_in_ready);
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
#define SET_INLINE_MODE   0x23
#define SET_RADIO_MODE 0x24
#define SET_SNIFFER_ADDRESS 0x25
#define GET_SNIFFER_DROP_COUNT 0x26
#define SET_PACKET_LOSS_SIMULATION 0x30
#define RESET_TO_BOOTLOADER 0xff

// Inline mode values
#define INLINE_MODE_OFF 0
#define INLINE_MODE_ON 1
#define INLINE_MODE_ON_WITH_RSSI 2

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

    static struct usb_command command;

	if (USB_REQTYPE_GET_TYPE(setup->bmRequestType) == USB_REQTYPE_TYPE_VENDOR) {
        LOG_DBG("Vendor request: bRequest 0x%x bmRequestType 0x%x len %d",
                setup->bRequest, setup->bmRequestType, *len);

        if (setup->bRequest == SET_RADIO_CHANNEL ||
            setup->bRequest == SET_RADIO_ADDRESS ||
            setup->bRequest == SET_DATA_RATE ||
            setup->bRequest == SET_RADIO_POWER ||
            setup->bRequest == SET_RADIO_ARD ||
            setup->bRequest == SET_RADIO_ARC ||
            setup->bRequest == ACK_ENABLE ||
            setup->bRequest == SET_CONT_CARRIER ||
            setup->bRequest == SET_MODE ||
            (setup->bRequest == SET_INLINE_MODE && setup->wValue <= INLINE_MODE_ON_WITH_RSSI) ||
            setup->bRequest == SET_SNIFFER_ADDRESS ||
            (setup->bRequest == SET_RADIO_MODE && setup->wValue <= 1) ||
            setup->bRequest == SET_PACKET_LOSS_SIMULATION) {
            
            LOG_DBG("Queuing command %d", setup->bRequest);


            command.type = command_setup;
            uint32_t length = *len;
            memcpy(&command.setup.setup_packet, setup, sizeof(struct usb_setup_packet));
            if (length > sizeof(command.setup.data)) {
                length = sizeof(command.setup.data);
            }
            if (usb_reqtype_is_to_device(setup) && length > 0)
            {
                memcpy(command.setup.data, *data, length);
                command.setup.length = length;
            }
            k_msgq_put(&command_queue, &command, K_FOREVER);
        } 
        else if (setup->bRequest == CHANNEL_SCANN && usb_reqtype_is_to_device(setup)) {
            k_mutex_lock(&usb_radio_mutex, K_FOREVER);
            uint8_t start = setup->wValue;
            uint8_t stop = setup->wIndex;
            fw_scan(start, stop, *data, setup->wLength);
            k_mutex_unlock(&usb_radio_mutex);
        } else if (setup->bRequest == CHANNEL_SCANN && usb_reqtype_is_to_host(setup)) {
            *data = state.scan_result;
            *len = MIN(state.scan_result_length, setup->wLength);
        }
        else if (setup->bRequest == GET_SNIFFER_DROP_COUNT && usb_reqtype_is_to_host(setup)) {
            static uint32_t drop_count_le;
            drop_count_le = sys_cpu_to_le32(atomic_get(&sniffer_drop_count));
            *data = (uint8_t *)&drop_count_le;
            *len = MIN(4, setup->wLength);
        }
        else if (setup->bRequest == RESET_TO_BOOTLOADER) {
            LOG_DBG("Vendor request: RESET_TO_BOOTLOADER");
            system_reset_to_uf2();
        } else {
            return -ENOTSUP;

        }
        
		return 0;
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

#define USB_THREAD_STACK_SIZE 1024
#define USB_THREAD_PRIORITY 5

static void usb_thread(void *, void *, void *);
static void usb_in_thread(void *, void *, void *);

K_THREAD_DEFINE(usb_tid, USB_THREAD_STACK_SIZE,
                usb_thread, NULL, NULL, NULL,
                USB_THREAD_PRIORITY, 0, 0);

K_THREAD_DEFINE(usb_in_tid, USB_THREAD_STACK_SIZE,
                usb_in_thread, NULL, NULL, NULL,
                USB_THREAD_PRIORITY, 0, 0);

static void usb_in_thread(void *, void *, void *)
{
    static struct usb_response response;

    while (1) {
        k_msgq_get(&response_queue, &response, K_FOREVER);
        k_sem_take(&usb_in_ready, K_FOREVER);

        uint32_t written = 0;
        int ret = usb_write(CRAZYRADIO_IN_EP_ADDR, response.payload, response.length, &written);
        if (ret != 0 || written != response.length) {
            k_sem_give(&usb_in_ready);
            LOG_WRN("inline[%u] usb_write failed: ret=%d written=%u expected=%u",
                    response.inline_seq, ret, written, response.length);
        } else {
            LOG_DBG("inline[%u] usb_write ok: len=%u", response.inline_seq, response.length);
        }
    }
}

static void usb_thread(void *, void *, void *) {
    static struct usb_command command;
    static struct esbPacket_s packet;
    static struct esbPacket_s ack;
    uint8_t rssi;
    uint8_t arc_counter;

    while(1) {
        if (state.sniffer_mode) {
            // In sniffer mode: poll command queue for setup commands (non-blocking)
            if (k_msgq_get(&command_queue, &command, K_NO_WAIT) == 0) {
                if (command.type == command_setup) {
                    k_mutex_lock(&usb_radio_mutex, K_FOREVER);
                    handle_vendor_command(&command.setup);
                    k_mutex_unlock(&usb_radio_mutex);
                }
                else if (command.type == command_data) {
                    if (command.data.length < 6) {
                        // Need at least 5 address bytes + 1 byte payload
                        continue;
                    }
                    uint8_t address[5];
                    memcpy(address, command.data.payload, 5);
                    uint8_t payload_length = command.data.length - 5;
                    if (payload_length > ESB_MAX_PAYLOAD_LENGTH) {
                        payload_length = ESB_MAX_PAYLOAD_LENGTH;
                    }
                    packet.length = payload_length;
                    memcpy(packet.data, &command.data.payload[5], payload_length);

                    k_mutex_lock(&usb_radio_mutex, K_FOREVER);
                    esb_sniffer_send(&packet, address);
                    k_mutex_unlock(&usb_radio_mutex);

                    led_pulse_green(K_MSEC(50));
                }
            }

            // Poll sniffer queue for received packets
            static struct esbSnifferPacket_s sniffer_pkt;
            if (k_msgq_get(&sniffer_queue, &sniffer_pkt, K_MSEC(1)) == 0) {
                // Format: total_length(1) + rssi(1) + pipe(1) + timestamp(4) + payload(0-32)
                uint8_t total_length = 7 + sniffer_pkt.length;
                state.usb_answer[0] = total_length;
                state.usb_answer[1] = sniffer_pkt.rssi;
                state.usb_answer[2] = sniffer_pkt.pipe;
                memcpy(&state.usb_answer[3], &sniffer_pkt.timestamp_us, 4);
                if (sniffer_pkt.length > 0) {
                    memcpy(&state.usb_answer[7], sniffer_pkt.data, sniffer_pkt.length);
                }

                usb_write(CRAZYRADIO_IN_EP_ADDR, state.usb_answer, total_length, NULL);
                if (total_length == CRAZYRADIO_BULK_EP_MPS) {
                    usb_write(CRAZYRADIO_IN_EP_ADDR, state.usb_answer, 0, NULL);
                }
                led_pulse_green(K_MSEC(50));
            }
            continue;
        }

        k_msgq_get(&command_queue, &command, K_FOREVER);

        k_mutex_lock(&usb_radio_mutex, K_FOREVER);
        if (command.type == command_data) {
            uint32_t inline_seq = 0;
            
            if (state.inline_mode) {
                inline_seq = atomic_inc(&inline_tx_sequence) + 1;
                // Get the header
                inline_mode_out_header *header = (inline_mode_out_header *)command.data.payload;
                state.channel = header->channel;
                esb_set_channel(state.channel);
                state.datarate = header->datarate;
                esb_set_bitrate(state.datarate);
                state.ack_enabled = header->ack_enabled;
                esb_set_ack_enabled(state.ack_enabled);
                esb_set_address(header->address);
                // Prepare the packet data
                int payload_length = header->length - sizeof(inline_mode_out_header);
                if (payload_length > 32+8) {
                    payload_length = 32+8;
                }
                memcpy(packet.data, &command.data.payload[sizeof(inline_mode_out_header)], payload_length);
                packet.length = payload_length;

                LOG_DBG("inline[%u] USB OUT: usb_len=%u header_len=%u payload_len=%d chan=%d dr=%d ack=%d addr=%02x%02x%02x%02x%02x",
                        inline_seq, command.data.length, header->length, payload_length, state.channel,
                        state.datarate, state.ack_enabled, header->address[0], header->address[1],
                        header->address[2], header->address[3], header->address[4]);
                LOG_HEXDUMP_DBG(packet.data, packet.length, "inline radio TX payload");
            } else if (!state.ack_enabled && command.data.length > 32) {
                // If we are not receiving ack (ie. broadcast) and the received data is > 32 bytes,
                // this means that the buffer actually contains 2 packets to send
                // Send the first one right away
                memcpy(packet.data, command.data.payload, command.data.length/2);
                packet.length = command.data.length/2;
                esb_send_packet(&packet, &ack, &rssi, &arc_counter);

                // And prepare the second one to be send by the normal execution flow
                memcpy(packet.data, &command.data.payload[command.data.length/2], command.data.length/2);
                packet.length = command.data.length/2;
            } else {
                // Otherwise, cap to 32 bytes and prepare the unicast packets
                if (command.data.length > 32) {
                    command.data.length = 32;
                }
                memcpy(packet.data, command.data.payload, command.data.length);
                packet.length = command.data.length;
            }
            
            if (state.datarate != 0 && state.channel <= 100) {
                
                // Send the packet
                bool acked = esb_send_packet(&packet, &ack, &rssi, &arc_counter);

                if (state.inline_mode) {
                    LOG_DBG("inline[%u] radio result: acked=%d ack_len=%u rssi=%u retry=%u",
                            inline_seq, acked, ack.length, rssi, arc_counter);
                    if (acked && ack.length > 0) {
                        LOG_HEXDUMP_DBG(ack.data, ack.length, "inline radio ACK payload");
                    }
                }

                if (acked || !state.ack_enabled) {
                    led_pulse_green(K_MSEC(50));
                } else {
                    led_pulse_red(K_MSEC(50));
                }

                if (ack.length > 32) {
                    LOG_ERR("Got an ack of size %d!", ack.length);
                    ack.length = 32;
                }

                if (state.inline_mode && !state.inline_rssi_mode) {
                    // Prepare the inline mode header
                    inline_mode_in_header *usb_header = (inline_mode_in_header *)state.usb_answer;
                    memset(usb_header, 0, sizeof(inline_mode_in_header));
                    usb_header->length = ack.length + sizeof(inline_mode_in_header);
                    usb_header->ack_received = acked ? 1 : 0;
                    if (state.ack_enabled) usb_header->rssi_lt_64dbm = (rssi < 64) ? 1 : 0;
                    usb_header->invalid_settings = (state.datarate == 0 || state.channel > 100) ? 1 : 0;
                    if (state.ack_enabled) usb_header->arc_counter = arc_counter & 0x0f;

                    // Shift the ack data
                    if (acked && ack.length > 0) {
                        memcpy(&state.usb_answer[sizeof(inline_mode_in_header)], ack.data, ack.length);
                    }

                    inline_usb_write(inline_seq, state.usb_answer, usb_header->length);
                } else if (state.inline_mode && state.inline_rssi_mode) {
                    // Prepare the inline with rssi mode header
                    inline_rssi_mode_in_header *usb_header = (inline_rssi_mode_in_header *)state.usb_answer;
                    memset(usb_header, 0, sizeof(inline_rssi_mode_in_header));
                    usb_header->length = ack.length + sizeof(inline_rssi_mode_in_header);
                    usb_header->ack_received = acked ? 1 : 0;
                    if (state.ack_enabled) usb_header->rssi_lt_64dbm = (rssi < 64) ? 1 : 0;
                    usb_header->invalid_settings = (state.datarate == 0 || state.channel > 100) ? 1 : 0;
                    if (state.ack_enabled) usb_header->arc_counter = arc_counter & 0x0f;
                    usb_header->rssi_dbm = rssi;

                    // Shift the ack data
                    if (acked && ack.length > 0) {
                        memcpy(&state.usb_answer[sizeof(inline_rssi_mode_in_header)], ack.data, ack.length);
                    }

                    inline_usb_write(inline_seq, state.usb_answer, usb_header->length);
                } else {
                    if (!state.ack_enabled) {
                        led_pulse_green(K_MSEC(50));
                    } else if (acked && ack.length <= 32) {
                        static char usb_answer[33];
                        usb_answer[0] = (arc_counter & 0x0f) << 4 | (rssi < 64)<<1 | 1;
                        memcpy(&usb_answer[1], ack.data, ack.length);
                    
                        if (usb_write(CRAZYRADIO_IN_EP_ADDR, usb_answer, ack.length + 1, NULL)) {
                            LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
                        }
                    } else {
                        char no_ack_answer[1] = {0};
                
                        if (usb_write(CRAZYRADIO_IN_EP_ADDR, no_ack_answer, 1, NULL)) {
                            LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
                        }
                    }
                }
                

            } else {
                LOG_DBG("Not sending, radio settings not handled!");
                if (state.inline_mode && !state.inline_rssi_mode) {
                    // Prepare the inline mode header
                    inline_mode_in_header invalid_settings_header = {
                        .length = sizeof(inline_mode_in_header),
                        .ack_received = 0,
                        .rssi_lt_64dbm = 0,
                        .invalid_settings = 1,
                        .arc_counter = 0,
                    };

                    inline_usb_write(inline_seq, &invalid_settings_header, invalid_settings_header.length);
                } else if (state.inline_mode && state.inline_rssi_mode) {
                    // Prepare the inline with rssi mode header
                    inline_rssi_mode_in_header invalid_settings_header = {
                        .length = sizeof(inline_rssi_mode_in_header),
                        .ack_received = 0,
                        .rssi_lt_64dbm = 0,
                        .invalid_settings = 1,
                        .arc_counter = 0,
                        .rssi_dbm = 0,
                    };

                    inline_usb_write(inline_seq, &invalid_settings_header, invalid_settings_header.length);
                } else {
                    char no_ack_answer[1] = {0};
            
                    if (usb_write(CRAZYRADIO_IN_EP_ADDR, no_ack_answer, 1, NULL)) {
                        LOG_DBG("ep 0x%x", CRAZYRADIO_IN_EP_ADDR);
                    }
                }

                led_pulse_red(K_MSEC(50));
            }
        } else if (command.type == command_setup) {
            LOG_DBG("Handling setup command %d", command.setup.setup_packet.bRequest);
            handle_vendor_command(&command.setup);
        }
        k_mutex_unlock(&usb_radio_mutex);
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
            if (state.scan_result_length >= ESB_MAX_PAYLOAD_LENGTH) {
                return;
            }
        } else {
            led_pulse_red(K_MSEC(50));
        }
    }
}

static void handle_vendor_command(struct setup_command* setup) {
    if (setup->setup_packet.bRequest == SET_RADIO_CHANNEL && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio channel %d", setup->setup_packet.wValue);
        uint16_t channel = setup->setup_packet.wValue;
        if (channel <= 100) {
            esb_set_channel(channel);
        }
        // If channel >100 used, packets will be ignored (ie. virtually not acked)
        state.channel = channel;
        // Reset inline mode
        state.inline_mode = false;
    } else if (setup->setup_packet.bRequest == SET_RADIO_ADDRESS && setup->setup_packet.wLength == 5) {
        LOG_DBG("Setting radio address %02x%02x%02x%02x%02x", (unsigned int)(setup->data)[0], (unsigned int)(setup->data)[1], (unsigned int)(setup->data)[2], (unsigned int)(setup->data)[3], (unsigned int)(setup->data)[4]);
        esb_set_address(setup->data);
        // Reset inline mode
        state.inline_mode = false;
    } else if (setup->setup_packet.bRequest == SET_DATA_RATE && setup->setup_packet.wLength == 0 && setup->setup_packet.wValue < 3) {
        char* datarates[] = {"250K", "1M", "2M"};
        LOG_DBG("Setting radio datarate to %s", datarates[setup->setup_packet.wValue]);
        uint32_t datarate = setup->setup_packet.wValue;
        if (datarate == 1) {
            esb_set_bitrate(radioBitrate1M);
        } else if (datarate == 2) {
            esb_set_bitrate(radioBitrate2M);
        }
        // If 250K is selected, packets will be ignored (ie. virtually not acked)
        state.datarate = datarate;
        // Reset inline mode
        state.inline_mode = false;
    } else if (setup->setup_packet.bRequest == SET_RADIO_POWER && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio power %d", setup->setup_packet.wValue);
        uint8_t power = MIN(setup->setup_packet.wValue, 3);
        uint8_t fem_power = power_mapping[power];
        fem_set_power(fem_power);
    } else if (setup->setup_packet.bRequest == SET_RADIO_ARD && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio ARD %d", setup->setup_packet.wValue);
    } else if (setup->setup_packet.bRequest == SET_RADIO_ARC && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio ARC %d", setup->setup_packet.wValue & 0x0f);
        esb_set_arc(setup->setup_packet.wValue & 0x0f);
    } else if (setup->setup_packet.bRequest == ACK_ENABLE && setup->setup_packet.wLength == 0) {
        bool enabled = setup->setup_packet.wValue != 0;
        LOG_DBG("Setting radio ACK Enable %s", enabled?"true":"false");
        state.ack_enabled = enabled;
        esb_set_ack_enabled(enabled);
        // Reset inline mode
        state.inline_mode = false;
        state.inline_rssi_mode = false;
    } else if (setup->setup_packet.bRequest == SET_CONT_CARRIER && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio Continious carrier %s", setup->setup_packet.wValue?"true":"false");
        bool enable = setup->setup_packet.wValue != 0;
        esb_set_continuous_carrier(enable);
    } else if (setup->setup_packet.bRequest == SET_MODE && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio Mode %d", setup->setup_packet.wValue);
    } else if (setup->setup_packet.bRequest == SET_RADIO_MODE && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio mode %d", setup->setup_packet.wValue);
        if (setup->setup_packet.wValue == 1) {
            // Enter sniffer mode
            state.sniffer_mode = true;
            state.inline_mode = false;
            state.inline_rssi_mode = false;
            k_msgq_purge(&sniffer_queue);
            atomic_set(&sniffer_drop_count, 0);
            esb_sniffer_start(sniffer_rx_callback);
            led_set_blue(true);
        } else if (setup->setup_packet.wValue == 0) {
            // Exit sniffer mode, back to normal PTX mode
            esb_sniffer_stop();
            state.sniffer_mode = false;
            led_set_blue(false);
        }
    } else if (setup->setup_packet.bRequest == SET_SNIFFER_ADDRESS && setup->setup_packet.wLength == 5) {
        LOG_DBG("Setting sniffer address pipe %d", setup->setup_packet.wValue);
        if (setup->setup_packet.wValue == 0) {
            esb_set_address(setup->data);
        } else if (setup->setup_packet.wValue == 1) {
            esb_set_address_pipe1(setup->data);
        }
    } else if (setup->setup_packet.bRequest == SET_INLINE_MODE && setup->setup_packet.wLength == 0) {
        LOG_DBG("Setting radio Inline Mode %d", setup->setup_packet.wValue);
        state.inline_mode = setup->setup_packet.wValue != 0;
        state.inline_rssi_mode = setup->setup_packet.wValue == INLINE_MODE_ON_WITH_RSSI;
    } else if (setup->setup_packet.bRequest == SET_PACKET_LOSS_SIMULATION && setup->setup_packet.wLength == 2) {
        uint8_t packet_loss_percent = setup->data[0];
        uint8_t ack_loss_percent = setup->data[1];
        LOG_DBG("Setting packet loss simulation: packet loss %d%%, ack loss %d%%", packet_loss_percent, ack_loss_percent);
        esb_set_packet_loss_simulation(packet_loss_percent, ack_loss_percent);
    } else {
        LOG_DBG("Unhandled vendor command %d", setup->setup_packet.bRequest);
    }
}