/*
 * crc.c
 *
 *  Created on: Dec 29, 2025
 *      Author: rdkee
 */



#include <limits.h>
#include "crc.h"
#include "circular_reading_buffer.h"
#include "data_tx_arrays.h"
#include "structs.h"

#define CMD_LENGTH 8 //length of a single command in bytes, 4 bytes for amp, 4 bytes for period

// Fill in Below for each new protocol ///////////////////////////////////////////////////////////////////////
char *payload_entries[] = {"status", "queue_length","queue_time","debug","frame"};

// Length of each entry, in bytes
uint16_t payload_length_key[] = {1, 4, 4, 1, 1};

// End of Fill out //////////////////////////////////////////////////////////////////////////////////////////

uint8_t compiled_payload[PAYLOAD_BYTES] = {0};
uint8_t rx_buffer[RX_BUF_LEN] = {0};
size_t rx_write_idx = 0;

// helper functions for clamping floats
static inline uint8_t clamp_u8_from_f32(float x)
{
    // Round to nearest integer
    int32_t v = (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));

    // Clamp to uint8_t range
    if (v < 0)
    {
        return 0;
    }
    else if (v > 255)
    {
        return 255;
    }
    else
    {
        return (uint8_t)v;
    }
}

static inline int16_t clamp_i16_from_f32(float x)
{
    // Round to nearest integer (half away from zero)
    int32_t v = (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));

    // Clamp to int16_t range
    if (v > INT16_MAX)
    {
        return INT16_MAX;
    }
    else if (v < INT16_MIN)
    {
        return INT16_MIN;
    }
    else
    {
        return (int16_t)v;
    }
}

// helper function for implementing memmem
static void* memmem(const void* haystack, size_t haystack_len,
			const void* needle, size_t needle_len)
{
    if (needle_len == 0 || haystack_len < needle_len)
    {
        return NULL;
    }

    const uint8_t *h = (const uint8_t *)haystack;
    const uint8_t *n = (const uint8_t *)needle;

    for (size_t i = 0; i <= haystack_len - needle_len; i++)
    {
        if (h[i] == n[0] &&
            memcmp(&h[i], n, needle_len) == 0)
        {
            return (void *)&h[i];
        }
    }

    return NULL;
}

// helper function for implementing CRC packets
static uint16_t crc16_ccitt(const uint8_t* buf, uint16_t len)
{
	uint16_t crc = 0xFFFF;
	for (uint16_t i = 0; i < len; i++)
	{
		crc ^= (uint16_t)buf[i] << 8;
		for (uint8_t j = 0; j < 8; j++)
		{
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
		}
	}
	return crc;
}
// Helper function to compile data from different memory locations into one contiguous source for sending
void compile_data_sources(uint8_t input_count, ...)
{
	va_list args;
	va_start(args, input_count);
	uint16_t write_idx = 0;

	// Check the input argument number. NOTE, IT IS IMPORTANT THAT YOU PASS AS MANY ARGUMENTS AS THERE ARE DATAFIELDS!
	// Otherwise, you're reading random memory
	if (input_count != PAYLOAD_DATA_FIELDS)
	{
		va_end(args);
		return; // Don't change the payload array. Echoing the same data will be the error state.
	}

	for (uint16_t i = 0; i < PAYLOAD_DATA_FIELDS; i++)
	{
		uint8_t* src = va_arg(args, uint8_t*);
		uint16_t len = payload_length_key[i];
		// Check for null pointer
		if (src == NULL)
		{
			va_end(args);
			return;
		}
		// Check bounds
		if (write_idx + len > PAYLOAD_BYTES)
		{
			va_end(args);
			return;
		}
		memcpy(&compiled_payload[write_idx], src, len);
		write_idx += payload_length_key[i];
	}
	va_end(args);
	return;
}

// Package data and send over UART
// Packet the predefined data payload (Non-generic)
void crc_uart_send_data(const uint8_t* src,
		UART_HandleTypeDef* huart)
{

	static volatile uint8_t pkt[PKT_BYTES];

	// 1. Header (preamble)
	pkt[0] = 0x55;
	pkt[1] = 0xAA;

    // 2. 2-byte payload length (little endian)
    pkt[2] = (uint8_t)(PAYLOAD_BYTES & 0xFF);        // LSB
    pkt[3] = (uint8_t)((PAYLOAD_BYTES >> 8) & 0xFF); // MSB

    // 3. Build the payload: Copy the data from the compiled array to the delivery packet.
    memcpy(&pkt[4], compiled_payload, PAYLOAD_BYTES);

    // 4. CRC over length + payload
    //    Starts from pkt[2], length = LEN_FIELD_BYTES + PAYLOAD_BYTES
    uint16_t crc = crc16_ccitt(&pkt[2], LEN_FIELD_BYTES + PAYLOAD_BYTES);
    pkt[4 + PAYLOAD_BYTES]     = (uint8_t)(crc & 0xFF);
    pkt[4 + PAYLOAD_BYTES + 1] = (uint8_t)(crc >> 8);

    // 5. Add 2-byte footer
    pkt[4 + PAYLOAD_BYTES + 2] = 0x6E;
    pkt[4 + PAYLOAD_BYTES + 3] = 0x2B;

    // 6. Transmit over UART
    huart2_try_send(pkt, PKT_BYTES);
//    HAL_UART_Transmit(huart, pkt, PKT_BYTES, HAL_MAX_DELAY); //HAL_MAX_DELAY

}

// Parses incoming information. This will be the most variable amongst implementations if reusing this file on other projects.
void crc_uart_rcv_data(rdg_buf_struct* rdg_struct, uint16_t length)
{
	// Find the header if it exists.
	uint8_t header[] = {0x55, 0xAA};
	uint8_t* p_start = memmem(rdg_struct->buffer, length, header, sizeof(header));
	// Center about the header if it exists.
	if (p_start == NULL)
	{
		// No header detected. For now, we will ignore the case when the header splits and accept loss of the cmd packet.
		return;
	}
	size_t start = (size_t)(p_start - rdg_struct->buffer);
	if (length < start + HEADER_BYTES + LEN_FIELD_BYTES)
	{
		// Not enough data for a full packet yet. Flush and wait for next loop. For now, we will do this all or nothing, where we get a complete
		// uninterrupted packet, or we do nothing. If this fails we'll deal with the edge cases. We transmit just a few bytes so I think this is ok.
		return;
	}
	// Pull out the payload length

	uint16_t payload_length = (uint16_t)rdg_struct->buffer[start+HEADER_BYTES] | ((uint16_t)rdg_struct->buffer[start+HEADER_BYTES+1] << 8);
	uint16_t frame_len = HEADER_BYTES + LEN_FIELD_BYTES + payload_length + CRC_BYTES;

	// if we do not have enough for a full transmission, do nothing.
	if (length < start + frame_len)
	{
		// Not a full packet. Discard the command.
		return;
	}

	// Lets calculate the check sum
	uint8_t frame[frame_len];
	memcpy(frame, &(rdg_struct->buffer[start]), frame_len);
	uint16_t crc_rx = (uint16_t)rdg_struct->buffer[start + HEADER_BYTES + LEN_FIELD_BYTES + payload_length] \
			| ((uint16_t)rdg_struct->buffer[start + HEADER_BYTES + LEN_FIELD_BYTES + payload_length + 1] << 8);
	// CRC is over length + payload
	uint16_t crc_calc = crc16_ccitt(&(rdg_struct->buffer[start + HEADER_BYTES]), frame_len- (HEADER_BYTES + CRC_BYTES));

	// If the packet is valid, handle appropriately
	if (crc_rx == crc_calc)
	{
		// Valid packet
		uint16_t payload_start = start + HEADER_BYTES + LEN_FIELD_BYTES;
		float condition;
		memcpy(&condition, &(rdg_struct->buffer[payload_start]), sizeof(condition));
		// In future this will use a callback
		if (condition == 0)
		// Mode packet
		{
			float incoming_mode;
			memcpy(&incoming_mode, &(rdg_struct->buffer[payload_start+sizeof(float)]), sizeof(float));
//			pushCommand(&stim_queue, amp, period, cmd_size);

			memcpy(queue_len, &incoming_mode, (size_t)sizeof(incoming_mode));
		}
		if (condition == 2)
		// Single shot stim packet
		{
			uint16_t incoming_cmd_size = (payload_length - 4) / CMD_LENGTH;// TODO add a check if this doesn't evaluate to a whole number
			uint32_t incoming_period[incoming_cmd_size];
			uint16_t incoming_amplitude[incoming_cmd_size];

			for (uint16_t i = 0; i < incoming_cmd_size; i++)
			{
				// Setting our idxs
				uint16_t amp_idx = 2*i;
				uint16_t period_idx = 2*i+1;
				// Add the amplitude into the array
				float current_amp;
				memcpy(&current_amp, &(rdg_struct->buffer[payload_start+sizeof(float)+sizeof(float)*amp_idx]), sizeof(float));
				uint16_t amp_int = (uint16_t)current_amp;
				incoming_amplitude[i] = amp_int;
				// Add the period into the array
				float current_period;
				memcpy(&current_period, &(rdg_struct->buffer[payload_start+sizeof(float)+sizeof(float)*period_idx]), sizeof(float));
				uint32_t period_int = (uint32_t)current_period;
				incoming_period[i] = period_int;
			}
			changeStimMode(&stim_queue, 0);
			pushCommand(&stim_queue, incoming_amplitude, incoming_period, incoming_cmd_size);
		}
		if (condition == 3)
		// continuous stim packet
		{
			uint16_t incoming_cmd_size = (payload_length - 4) / CMD_LENGTH;// TODO add a check if this doesn't evaluate to a whole number
			uint32_t incoming_period[incoming_cmd_size];
			uint16_t incoming_amplitude[incoming_cmd_size];

			for (uint16_t i = 0; i < incoming_cmd_size; i++)
			{
				// Setting our idxs
				uint16_t amp_idx = 2*i;
				uint16_t period_idx = 2*i+1;
				// Add the amplitude into the array
				float current_amp;
				memcpy(&current_amp, &(rdg_struct->buffer[payload_start+sizeof(float)+sizeof(float)*amp_idx]), sizeof(float));
				uint16_t amp_int = (uint16_t)current_amp;
				incoming_amplitude[i] = amp_int;
				// Add the period into the array
				float current_period;
				memcpy(&current_period, &(rdg_struct->buffer[payload_start+sizeof(float)+sizeof(float)*period_idx]), sizeof(float));
				uint32_t period_int = (uint32_t)current_period;
				incoming_period[i] = period_int;
			}
			changeStimMode(&stim_queue, 1);
			pushCommand(&stim_queue, incoming_amplitude, incoming_period, incoming_cmd_size);
		}


	}
	else
	{
		uint8_t crc_fail = 1;
	}


	return;

}
