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

/* Incoming command record: amplitude, period, GPIO mask, mode (all floats). */
#define CMD_FIELD_COUNT 4U
#define CMD_LENGTH      (CMD_FIELD_COUNT * sizeof(float))
#define GPIO_MASK_MAX   0x01FFU

// Fill in Below for each new protocol ///////////////////////////////////////////////////////////////////////
char *payload_entries[] = {"status", "queue_length","queue_time","debug","frame", "watchdog"};

// Length of each entry, in bytes
uint16_t payload_length_key[] = {1, 4, 4, 1, 1, 2};

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

static inline uint16_t clamp_gpio_mask_from_f32(float x)
{
    int32_t v = (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));

    if (v < 0)
    {
        return 0U;
    }
    else if (v > GPIO_MASK_MAX)
    {
        return GPIO_MASK_MAX;
    }
    else
    {
        return (uint16_t)v;
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
    huart3_try_send(pkt, PKT_BYTES);
//    HAL_UART_Transmit(huart, pkt, PKT_BYTES, HAL_MAX_DELAY); //HAL_MAX_DELAY

}

static void handle_valid_payload(const uint8_t* payload,
		uint16_t payload_length)
{
	if (payload_length < sizeof(float))
	{
		return;
	}

	float condition;
	memcpy(&condition, payload, sizeof(condition));
	if (condition == 1.0f)
	{
		if (payload_length != (2U * sizeof(float)))
		{
			return;
		}

		float command;
		memcpy(&command, payload + sizeof(float), sizeof(command));
		if (command == 1.0f)
		{
			stim_queue.clear_flag = 1;
		}
		else if (command == 2.0f)
		{
			stim_queue.watchdog_counter = 0;
		}
		return;
	}

	if (condition != 2.0f)
	{
		return;
	}

	uint16_t command_bytes = payload_length - (uint16_t)sizeof(float);
	if ((command_bytes == 0U) || ((command_bytes % CMD_LENGTH) != 0U))
	{
		return;
	}

	uint16_t incoming_cmd_size = command_bytes / CMD_LENGTH;
	uint8_t incoming_mode[incoming_cmd_size];
	uint16_t incoming_gpio[incoming_cmd_size];
	uint16_t incoming_amplitude[incoming_cmd_size];
	uint32_t incoming_period[incoming_cmd_size];

	for (uint16_t i = 0U; i < incoming_cmd_size; i++)
	{
		size_t command_start = sizeof(float) + ((size_t)i * CMD_LENGTH);

		float current_amp;
		memcpy(&current_amp, payload + command_start, sizeof(current_amp));
		incoming_amplitude[i] = (uint16_t)current_amp;

		float current_period;
		memcpy(&current_period, payload + command_start + sizeof(float),
				sizeof(current_period));
		incoming_period[i] = (uint32_t)current_period;

		float current_gpio;
		memcpy(&current_gpio, payload + command_start + 2U * sizeof(float),
				sizeof(current_gpio));
		incoming_gpio[i] = clamp_gpio_mask_from_f32(current_gpio);

		float current_mode;
		memcpy(&current_mode, payload + command_start + 3U * sizeof(float),
				sizeof(current_mode));
		incoming_mode[i] = clamp_u8_from_f32(current_mode);
	}

	pushCommand(&stim_queue, incoming_mode, incoming_gpio,
			incoming_amplitude, incoming_period, incoming_cmd_size);
}

/* Process every complete frame and retain a trailing partial frame for the
 * next DMA receive event.
 */
void crc_uart_rcv_data(rdg_buf_struct* rdg_struct, uint16_t length)
{
	static const uint8_t header[] = {0x55, 0xAA};

	if (rdg_struct == NULL)
	{
		return;
	}
	if (length > rdg_struct->tail)
	{
		length = rdg_struct->tail;
	}

	size_t search_offset = 0U;
	size_t keep_from = length;
	while (search_offset < length)
	{
		uint8_t* p_start = memmem(rdg_struct->buffer + search_offset,
				length - search_offset, header, sizeof(header));
		if (p_start == NULL)
		{
			/* Preserve a possible first header byte split across callbacks. */
			if (length > 0U && rdg_struct->buffer[length - 1U] == header[0])
			{
				keep_from = length - 1U;
			}
			else
			{
				keep_from = length;
			}
			break;
		}

		size_t start = (size_t)(p_start - rdg_struct->buffer);
		if (length - start < HEADER_BYTES + LEN_FIELD_BYTES)
		{
			keep_from = start;
			break;
		}

		uint16_t payload_length =
				(uint16_t)rdg_struct->buffer[start + HEADER_BYTES] |
				((uint16_t)rdg_struct->buffer[start + HEADER_BYTES + 1U] << 8U);
		size_t frame_length = HEADER_BYTES + LEN_FIELD_BYTES +
				(size_t)payload_length + CRC_BYTES;

		/* An advertised frame larger than the accumulator cannot be valid.
		 * Advance one byte and search for the next header instead of blocking.
		 */
		if (frame_length > rdg_struct->buf_size)
		{
			search_offset = start + 1U;
			continue;
		}
		if (length - start < frame_length)
		{
			keep_from = start;
			break;
		}

		size_t crc_position = start + HEADER_BYTES + LEN_FIELD_BYTES +
				payload_length;
		uint16_t crc_rx = (uint16_t)rdg_struct->buffer[crc_position] |
				((uint16_t)rdg_struct->buffer[crc_position + 1U] << 8U);
		uint16_t crc_calc = crc16_ccitt(
				&rdg_struct->buffer[start + HEADER_BYTES],
				LEN_FIELD_BYTES + payload_length);

		if (crc_rx == crc_calc)
		{
			handle_valid_payload(
					&rdg_struct->buffer[start + HEADER_BYTES + LEN_FIELD_BYTES],
					payload_length);
			search_offset = start + frame_length;
			keep_from = search_offset;
		}
		else
		{
			/* Resynchronize without trusting the corrupt frame's length. */
			search_offset = start + 1U;
		}
	}

	uint16_t bytes_to_keep = length - (uint16_t)keep_from;
	if (bytes_to_keep > 0U && keep_from > 0U)
	{
		memmove(rdg_struct->buffer, rdg_struct->buffer + keep_from,
				bytes_to_keep);
	}
	rdg_struct->tail = bytes_to_keep;
}
