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

#include "esb.h"

#include "fem.h"

#include <zephyr/kernel.h>

#include <hal/nrf_radio.h>
#include <nrfx_ppi.h>
#include <nrfx_timer.h>

#include <zephyr/types.h>
#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/random/rand32.h>

static K_MUTEX_DEFINE(radio_busy);
static K_SEM_DEFINE(radioXferDone, 0, 1);

static bool isInit = false;
static bool sending;
static bool timeout;
static uint8_t pid = 0;
static struct esbPacket_s * ackBuffer;

const nrfx_timer_t timer0 = NRFX_TIMER_INSTANCE(0);

static void radio_isr(void *arg)
{
    if (sending) {
        // Packet sent!, the radio is currently switching to RX mode
        // We need to setup the timeout timer, the END time is
        // captured in timer.CC0[2] and timer0.CC[1] is going to be
        // used for the timeout

        // Switch the FEM to receive mode
        fem_txen_set(false);
        fem_rxen_set(true);

        // Setup ack data address
        nrf_radio_packetptr_set(NRF_RADIO, ackBuffer);

        // Set timeout time
        uint32_t endTime = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL2);
        nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1, endTime + 500);

        // Configure PPI
        nrfx_ppi_channel_disable(NRF_PPI_CHANNEL27); // RADIO_END -> T0[2]
        nrfx_ppi_channel_enable(NRF_PPI_CHANNEL26);  // RADIO_ADDR -> T0[1] (Disables timeout!)
        nrfx_ppi_channel_enable(NRF_PPI_CHANNEL22);  // T0[1] -> RADIO_DISABLE (Timeout!)

        // Disable chaining short, disable will switch off the radio for good
        nrf_radio_shorts_disable(NRF_RADIO, RADIO_SHORTS_DISABLED_RXEN_Msk);

        // We are now in receiving mode and there has been not timeout yet
        sending = false;
        timeout = false;
    } else {
        // Packet received or timeout
        // Disable FEM
        fem_rxen_set(false);

        timeout = nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1);
        nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1);

        nrfx_ppi_channel_disable(NRF_PPI_CHANNEL26);  // RADIO_ADDR -> T0[1] (Disables timeout!)
        nrfx_ppi_channel_disable(NRF_PPI_CHANNEL22);  // T0[1] -> RADIO_DISABLE (Timeout!)


        k_sem_give(&radioXferDone);
    }

    nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
    nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_END);
}

void esb_init()
{
    // Timer0
    nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_frequency_set(NRF_TIMER0, NRF_TIMER_FREQ_1MHz);
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_CLEAR);
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_START);

    nrf_radio_power_set(NRF_RADIO, true);

    nrf_radio_txpower_set(NRF_RADIO, NRF_RADIO_TXPOWER_0DBM);

    // Low level packet configuration
    nrf_radio_packet_conf_t radioConfig = {0,};
    radioConfig.lflen = 6;
    radioConfig.s0len = 0;
    radioConfig.s1len = 3;
    radioConfig.maxlen = 32;
    radioConfig.statlen = 0;
    radioConfig.balen = 4;
    radioConfig.big_endian = true;
    radioConfig.whiteen = false;
    nrf_radio_packet_configure(NRF_RADIO, &radioConfig);

    // Configure channel and bitrate
    nrf_radio_mode_set(NRF_RADIO, NRF_RADIO_MODE_NRF_2MBIT);
    nrf_radio_frequency_set(NRF_RADIO, 2447);

    // Configure Addresses
    nrf_radio_base0_set(NRF_RADIO, 0xe7e7e7e7);
    nrf_radio_prefix0_set(NRF_RADIO, 0x000000e7);
    nrf_radio_txaddress_set(NRF_RADIO, 0);
    nrf_radio_rxaddresses_set(NRF_RADIO, 0x01u);

    // Configure CRC
    nrf_radio_crc_configure(NRF_RADIO, 2, NRF_RADIO_CRC_ADDR_INCLUDE, 0x11021UL);
    nrf_radio_crcinit_set(NRF_RADIO, 0xfffful);

    // Acquire RSSI at radio address
    nrf_radio_shorts_enable(NRF_RADIO, NRF_RADIO_SHORT_ADDRESS_RSSISTART_MASK | NRF_RADIO_SHORT_DISABLED_RSSISTOP_MASK);

    // Enable disabled interrupt only, the rest is handled by shorts
    nrf_radio_int_enable(NRF_RADIO, NRF_RADIO_INT_DISABLED_MASK);
    IRQ_CONNECT(RADIO_IRQn, 2, radio_isr, NULL, 0);
    irq_enable(RADIO_IRQn);

    fem_init();

    isInit = true;
}

void esb_deinit()
{
    k_mutex_lock(&radio_busy, K_FOREVER);

    isInit = false;

    nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
    // It can take up to 140us for the radio to disable itself
    k_sleep(K_USEC(200));
    nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_STOP);
    nrf_radio_power_set(NRF_RADIO, false);
    irq_disable(RADIO_IRQn);

    k_sem_reset(&radioXferDone);

    k_mutex_unlock(&radio_busy);
}

void esb_set_channel(uint8_t channel)
{
    if (channel <= 100) {
        nrf_radio_frequency_set(NRF_RADIO, 2400+channel);
    }
}

void esb_set_bitrate(esbBitrate_t bitrate)
{
    switch(bitrate) {
        case radioBitrate1M:
            nrf_radio_mode_set(NRF_RADIO, RADIO_MODE_MODE_Nrf_1Mbit);
            break;
        case radioBitrate2M:
            nrf_radio_mode_set(NRF_RADIO, RADIO_MODE_MODE_Nrf_2Mbit);
            break;
    }
}

static uint32_t swap_bits(uint32_t inp)
{
  uint32_t i;
  uint32_t retval = 0;

  inp = (inp & 0x000000FFUL);

  for(i = 0; i < 8; i++)
  {
    retval |= ((inp >> i) & 0x01) << (7 - i);
  }

  return retval;
}

static uint32_t bytewise_bitswap(uint32_t inp)
{
  return (swap_bits(inp >> 24) << 24)
       | (swap_bits(inp >> 16) << 16)
       | (swap_bits(inp >> 8) << 8)
       | (swap_bits(inp));
}

void esb_set_address(uint8_t address[5])
{
    uint32_t base0 = address[1]<<24 | address[2]<<16 | address[3]<<8 | address[4];
    nrf_radio_base0_set(NRF_RADIO, bytewise_bitswap(base0));
    uint32_t prefix0 = nrf_radio_prefix0_get(NRF_RADIO);
    prefix0 = (prefix0 & 0xffffff00) | (swap_bits(address[0]) & 0x0ff);
    nrf_radio_prefix0_set(NRF_RADIO, prefix0);
}

bool esb_send_packet(struct esbPacket_s *packet, struct esbPacket_s * ack, uint8_t *rssi)
{
    if (!isInit) {
        return false;
    }

    k_mutex_lock(&radio_busy, K_FOREVER);

    static int lossCounter = 0;
    static int ackLossCounter = 0;

    // Drop packet ocasionally
    if (CONFIG_ESB_PACKET_LOSS_PERCENT != 0 && (sys_rand32_get() % 100) < CONFIG_ESB_PACKET_LOSS_PERCENT) {
        lossCounter = 0;

        k_mutex_unlock(&radio_busy);

        return false;
    } else {
        // Handling packet PID
        packet->s1 = (pid<<1);
        pid++;

        nrf_radio_shorts_enable(NRF_RADIO, RADIO_SHORTS_READY_START_Msk |
                                        RADIO_SHORTS_END_DISABLE_Msk |
                                        RADIO_SHORTS_DISABLED_RXEN_Msk);
        nrf_ppi_channel_enable(NRF_PPI, NRF_PPI_CHANNEL27); // END -> Timer0 Capture[2]
        nrfx_ppi_channel_enable(NRF_PPI_CHANNEL26);  // RADIO_ADDR -> T0[1]  (debug)

        nrf_radio_packetptr_set(NRF_RADIO, packet);
        ack->length = 0;
        ackBuffer = ack;

        // Enable FEM PA
        fem_txen_set(true);

        sending = true;
        nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);

        k_sem_take(&radioXferDone, K_FOREVER);

        *rssi = nrf_radio_rssi_sample_get(NRF_RADIO);

        // Drop ack packet ocasionally
        if (CONFIG_ESB_ACK_LOSS_PERCENT != 0 && (sys_rand32_get() % 100) < CONFIG_ESB_ACK_LOSS_PERCENT) {
            ackLossCounter = 0;
            return false;
        }

        k_mutex_unlock(&radio_busy);

        return !timeout;
    }
}

// RPC API
void esb_send_packet_rpc(const rpc_request_t *request, rpc_response_t *response)
{
    struct esbPacket_s packet = {0,};
    struct esbPacket_s ackPacket = {0,};
    char * error = "Bad Request";

    if (!isInit) {
        rpc_response_send_errorstr(response, "NotInitialized");
        return;
    }

    if (!cbor_value_is_array(&request->param)) {
        goto bad_request;
    }
    CborValue array;
    cbor_value_enter_container(&request->param, &array);

    // Channel
    if (!cbor_value_is_unsigned_integer(&array)) {
        error = "Request is not an array";
        goto bad_request;
    }
    uint64_t channel;
    cbor_value_get_uint64(&array, &channel);
    cbor_value_advance(&array);
    if (channel > 100) {
        error = "Channel out of range";
        goto bad_request;
    }
    esb_set_channel(channel);

    // Address
    if (!cbor_value_is_byte_string(&array)) {
        error = "Address is not a byte string";
        goto bad_request;
    }
    size_t address_length;
    cbor_value_calculate_string_length(&array, &address_length);
    if (address_length != 5) {
        error = "Address is not 5 bytes long";
        goto bad_request;
    }
    uint8_t address[5];
    cbor_value_copy_byte_string(&array, address, &address_length, &array);
    esb_set_address(address);

    // Payload
    if (!cbor_value_is_byte_string(&array)) {
        error = "Payload is not a byte string";
        goto bad_request;
    }
    size_t payload_length;
    cbor_value_calculate_string_length(&array, &payload_length);
    if (payload_length > 32) {
        error = "Payload is too long";
        goto bad_request;
    }
    packet.length = payload_length;
    cbor_value_copy_byte_string(&array, packet.data, &payload_length, &array);

    // Send packet!
    uint8_t rssi;
    bool acked = esb_send_packet(&packet, &ackPacket, &rssi);

    // Report ack
    CborEncoder *result = rpc_response_prepare_result(response);

    CborEncoder result_array;
    cbor_encoder_create_array(result, &result_array, 3);

    cbor_encode_boolean(&result_array, acked);
    if (acked) {
        cbor_encode_byte_string(&result_array, ackPacket.data, ackPacket.length);
    } else {
        cbor_encode_null(&result_array);
    }

    cbor_encode_uint(&result_array, rssi);

    cbor_encoder_close_container(result, &result_array);

    rpc_response_send(response);

    return;
bad_request:
    rpc_response_send_errorstr(response, error);
}
