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
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esb);

static K_MUTEX_DEFINE(radio_busy);
static K_SEM_DEFINE(radioXferDone, 0, 1);

static bool isInit = false;
static bool sending;
static bool timeout;
static uint8_t pid = 0;
static struct esbPacket_s * ackBuffer;
static bool ack_enabled = true;
static int arc = 3;
static int packet_loss_percent = 0;
static int ack_loss_percent = 0;

static bool continuous_carrier_enabled = false;

const nrfx_timer_t timer0 = NRFX_TIMER_INSTANCE(0);

static void radio_isr(void *arg)
{
    nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
    nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_END);

    if (sending) {
        // Packet sent!, the radio is currently switching to RX mode
        // We need to setup the timeout timer, the END time is
        // captured in timer.CC0[2] and timer0.CC[1] is going to be
        // used for the timeout

        if (ack_enabled) {
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
            k_sem_give(&radioXferDone);
        }
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
}

const char* radio_states[13] = {
    "Disabled",     // 0
    "RxRu",         // 1
    "RxIdle",       // 2
    "Rx",           // 3
    "RxDisable",    // 4
    "Invalid (5)",  // 5
    "Invalid (6)",  // 6
    "Invalid (7)",  // 7
    "Invalid (8)",  // 8
    "TxRu",         // 9
    "TxIdle",       // 10
    "Tx",           // 11
    "TxDisable"     // 12
};

void esb_init()
{
    // Timer0
    nrf_timer_bit_width_set(NRF_TIMER0, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_prescaler_set(NRF_TIMER0, NRF_TIMER_FREQ_1MHz);
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
    nrf_radio_frequency_set(NRF_RADIO, 2442); // Channel 42

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

    // Disabled interrupt will be enabled when needed
    IRQ_CONNECT(RADIO_IRQn, 1, radio_isr, NULL, 0);
    irq_enable(RADIO_IRQn);

    fem_init();

    ack_enabled = true;
    arc = 3;

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

void esb_set_arc(int value) {
    k_mutex_lock(&radio_busy, K_FOREVER);
    arc = value & 0x0f;
    k_mutex_unlock(&radio_busy);
}

void esb_set_ack_enabled(bool enabled) {
    k_mutex_lock(&radio_busy, K_FOREVER);
    ack_enabled = enabled;
    k_mutex_unlock(&radio_busy);
}

void esb_set_channel(uint8_t channel)
{
    k_mutex_lock(&radio_busy, K_FOREVER);
    if (channel <= 100) {
        nrf_radio_frequency_set(NRF_RADIO, 2400+channel);
    }
    k_mutex_unlock(&radio_busy);
}

void esb_set_bitrate(esbBitrate_t bitrate)
{
    k_mutex_lock(&radio_busy, K_FOREVER);
    switch(bitrate) {
        case radioBitrate1M:
            nrf_radio_mode_set(NRF_RADIO, RADIO_MODE_MODE_Nrf_1Mbit);
            break;
        case radioBitrate2M:
            nrf_radio_mode_set(NRF_RADIO, RADIO_MODE_MODE_Nrf_2Mbit);
            break;
    }
    k_mutex_unlock(&radio_busy);
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
    k_mutex_lock(&radio_busy, K_FOREVER);
    uint32_t base0 = address[1]<<24 | address[2]<<16 | address[3]<<8 | address[4];
    nrf_radio_base0_set(NRF_RADIO, bytewise_bitswap(base0));
    uint32_t prefix0 = nrf_radio_prefix0_get(NRF_RADIO);
    prefix0 = (prefix0 & 0xffffff00) | (swap_bits(address[0]) & 0x0ff);
    nrf_radio_prefix0_set(NRF_RADIO, prefix0);
    k_mutex_unlock(&radio_busy);
}

void esb_set_packet_loss_simulation(uint8_t packet_loss, uint8_t ack_loss)
{
    k_mutex_lock(&radio_busy, K_FOREVER);
    packet_loss_percent = packet_loss;
    ack_loss_percent = ack_loss;
    k_mutex_unlock(&radio_busy);
}

bool esb_send_packet(struct esbPacket_s *packet, struct esbPacket_s * ack, uint8_t *rssi, uint8_t* retry)
{
    if (!isInit) {
        return false;
    }

    if (continuous_carrier_enabled) {
        return false;
    }

    k_mutex_lock(&radio_busy, K_FOREVER);

    static int lossCounter = 0;
    static int ackLossCounter = 0;

    // Drop packet occasionally
    if (packet_loss_percent != 0 && (sys_rand32_get() % 100) < packet_loss_percent) {
        lossCounter = 0;

        k_mutex_unlock(&radio_busy);

        return false;
    } else {
        // Handling packet PID. S1 format is | PID(2) | ACK flag |
        packet->s1 = ((pid & 0x03)<<1) | 1;
        pid++;

        bool ack_received = false;

        int arc_counter = 0;

        do {
            // Enable disabled interrupt only, the rest is handled by shorts
            nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
            nrf_radio_int_enable(NRF_RADIO, NRF_RADIO_INT_DISABLED_MASK);

            nrf_radio_shorts_enable(NRF_RADIO, RADIO_SHORTS_READY_START_Msk |
                                        RADIO_SHORTS_END_DISABLE_Msk);
            if (ack_enabled) {
                nrf_radio_shorts_enable(NRF_RADIO, RADIO_SHORTS_DISABLED_RXEN_Msk);
            }
            nrf_ppi_channel_enable(NRF_PPI, NRF_PPI_CHANNEL27); // END -> Timer0 Capture[2]
            nrfx_ppi_channel_enable(NRF_PPI_CHANNEL26);  // RADIO_ADDR -> T0[1]  (debug)

            nrf_radio_packetptr_set(NRF_RADIO, packet);
            ack->length = 0;
            ackBuffer = ack;

            // Enable FEM PA
            fem_txen_set(true);

            sending = true;
            nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);

            if (k_sem_take(&radioXferDone, K_MSEC(200)) != 0) {
                // The radio state machine is stuck! Reset the radio and returns that the packet is lost
                LOG_WRN("Radio state machine stuck, resetting radio");

                LOG_DBG("Interrupt state: sending: %d", sending);

                unsigned int radio_state = nrf_radio_state_get(NRF_RADIO);
                if (radio_state <= 12) {
                    LOG_DBG("Radio state: %s", radio_states[radio_state]);
                } else {
                    LOG_DBG("Radio state: Invalid (%d)", radio_state);
                }

                // Print all information about the radio packet
                LOG_DBG("Packet length: %d", packet->length);
                LOG_DBG("Packet PID: %d", packet->s1);
                LOG_HEXDUMP_DBG(packet, packet->length + 2, "Packet data:");

                irq_disable(RADIO_IRQn);
                nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
                k_sem_reset(&radioXferDone);
                k_mutex_unlock(&radio_busy);
                irq_enable(RADIO_IRQn);

                return false;
            }

            // We do not need the interrupt anymore
            nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_INT_DISABLED_MASK);

            // Clean up after ourselves
            nrf_radio_shorts_disable(NRF_RADIO, RADIO_SHORTS_READY_START_Msk |
                                        RADIO_SHORTS_END_DISABLE_Msk);
            if (ack_enabled) {
                nrf_radio_shorts_disable(NRF_RADIO, RADIO_SHORTS_DISABLED_RXEN_Msk);
            }
            nrf_ppi_channel_disable(NRF_PPI, NRF_PPI_CHANNEL27); // END -> Timer0 Capture[2]
            nrfx_ppi_channel_disable(NRF_PPI_CHANNEL26);  // RADIO_ADDR -> T0[1]  (debug)

            // Check if ack received
            ack_received = (!timeout) && nrf_radio_crc_status_check(NRF_RADIO) && ack_enabled;

            arc_counter += 1;

            // If ack is not enabled, it is normal to not receive an ack
            if (ack_received || !ack_enabled) {
                break;
            }

        } while (arc_counter <= arc);
        
        *rssi = nrf_radio_rssi_sample_get(NRF_RADIO);
        *retry = arc_counter - 1;

        // Drop ack packet occasionally
        if (ack_loss_percent != 0 && (sys_rand32_get() % 100) < ack_loss_percent) {
            ackLossCounter = 0;
            k_mutex_unlock(&radio_busy);
            return false;
        }

        k_mutex_unlock(&radio_busy);

        return ack_received;
    }
}

bool esb_set_continuous_carrier(bool enable) {
    if (!isInit) {
        return false;
    }

    if (enable == continuous_carrier_enabled) {
        return false;
    }

    k_mutex_lock(&radio_busy, K_FOREVER);
    if (enable) {
        fem_txen_set(true);

        nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);


    } else {
        nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);

        fem_txen_set(false);
    }


    continuous_carrier_enabled = enable;

    k_mutex_unlock(&radio_busy);
    return true;
}
