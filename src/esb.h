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

#pragma once
#include <stdint.h>
#include <stdbool.h>

void esb_init();
void esb_deinit();

/**
 * @brief Set the radio channel to be used for the next communications
 * 
 * The radio frequency will be 2400MHz + \p channel
 * 
 * @param channel A channel from 0 to 100
 */
void esb_set_channel(uint8_t channel);

/**
 * @brief Possible radio bitrate
 */
typedef enum {
    radioBitrate1M,
    radioBitrate2M
} esbBitrate_t;

/**
 * @brief Set the number of retries if no ack has been received
 * @param value Number of retries
 * 
 * @note This also affects no-ack (ie. broadcast) packets: no-ack packet will always be sent "arc" times in a row
 */
void esb_set_arc(int value);

/**
 * @brief Set if an ack will be received or not
 * @param enabled True to receive an ack after sending a packer. False to just send.
 * 
 * Setting ack_enabled to false is usually done to send broadcast packets
 */
void esb_set_ack_enabled(bool enabled);

/**
 * @brief Set the radio bitrate for the next communications
 * @param bitrate The bitrate to set
 */
void esb_set_bitrate(esbBitrate_t bitrate);

/**
 * @brief Set the radio address
 * @param address Radio address
 */
void esb_set_address(uint8_t address[5]);

/**
 * @brief ESB radio packet
 * 
 * This structure is a packed structure that contains all data that can be read and written
 * by the nRF radio hardware. The field \a s1 will be filled up by the esb_send_packet() function.
 */
struct esbPacket_s {
    uint8_t length;
    uint8_t s1;
    char data[32];
} __attribute__((packed));

/**
 * Send a packet and wait for and received an acknoledgement
 * 
 * @param packet ESB packet to send
 * @param ack Structure that will be filled up with the ESB ack packet
 * @param rssi Pointer to a u8 that will be filled up with the ACK RSSI
 * @param retry Number of retry required to received an ack
 * 
 * @return true if an ack has been properly received, false otherwise. the parameters
 *         ack and rssi are only filled up if an ack has been recievde.
 * 
 * @note This function will always return false if ack_enabled has been set to false.
*/
bool esb_send_packet(struct esbPacket_s *packet, struct esbPacket_s * ack, uint8_t *rssi, uint8_t *retry);

/**
 * @brief Enable or disable continuous carrier mode
 * 
 * When continuous carrier mode is enabled, the radio will transmit a continuous
 * unmodulated carrier wave on the set channel. This is useful for testing
 * purposes, such as measuring output power or spectrum analysis.
 * 
 * @param enable true to enable continuous carrier mode, false to disable it
 * @return true if the operation was successful, false if no change was made
 */
bool esb_set_continuous_carrier(bool enable);

