/*
 * circular_reading_buffer.c
 *
 *  Created on: Dec 9, 2025
 *      Author: k.rodolfo
 */


#include "circular_reading_buffer.h"
// This should maybe be called circular_buffer_reader. It doesn't create a circular buffer. It reads one to a linear buffer.

/* Append all bytes between the saved DMA consumer position and the latest
 * producer position. dma_write_position may equal dma_size when the producer
 * is exactly at the end of the circular buffer.
 */
uint16_t dma_to_rdg_buf(rdg_buf_struct* rdg_struct,
		const uint8_t* dma_buffer, uint16_t dma_size,
		uint16_t dma_write_position)
{
	if (rdg_struct == NULL || dma_buffer == NULL || dma_size == 0U ||
			dma_write_position > dma_size ||
			rdg_struct->dma_head >= dma_size)
	{
		return 0U;
	}

	uint16_t read_position = rdg_struct->dma_head;
	uint16_t bytes_available;
	if (dma_write_position == dma_size)
	{
		bytes_available = dma_size - read_position;
	}
	else if (dma_write_position >= read_position)
	{
		bytes_available = dma_write_position - read_position;
	}
	else
	{
		bytes_available = dma_size - read_position + dma_write_position;
	}

	if (bytes_available == 0U)
	{
		return 0U;
	}

	/* If malformed or oversized input filled the accumulator, discard the
	 * incomplete prefix and resynchronize from the newest DMA data.
	 */
	if (bytes_available > rdg_struct->buf_size - rdg_struct->tail)
	{
		rdg_struct->tail = 0U;
	}
	if (bytes_available > rdg_struct->buf_size)
	{
		rdg_struct->dma_head = (dma_write_position == dma_size) ?
				0U : dma_write_position;
		return 0U;
	}

	uint16_t first_copy = dma_size - read_position;
	if (first_copy > bytes_available)
	{
		first_copy = bytes_available;
	}

	memcpy(rdg_struct->buffer + rdg_struct->tail,
			dma_buffer + read_position, first_copy);
	uint16_t second_copy = bytes_available - first_copy;
	if (second_copy > 0U)
	{
		memcpy(rdg_struct->buffer + rdg_struct->tail + first_copy,
				dma_buffer, second_copy);
	}

	rdg_struct->tail += bytes_available;
	rdg_struct->dma_head = (dma_write_position == dma_size) ?
			0U : dma_write_position;

	return bytes_available;
}

// Echo the data from the rdg_buffer to the selected UART channel. Then reset/flush the reading buffer
void rdg_buf_echo(rdg_buf_struct* rdg_struct, UART_HandleTypeDef* huart){
	HAL_UART_Transmit(huart, (uint8_t*)rdg_struct->buffer, rdg_struct->tail, HAL_MAX_DELAY);
	//flush_buffer(rdg_struct);
}

// Clear the reading buffer following a successful process of the command
void flush_buffer(rdg_buf_struct* rdg_struct){
	rdg_struct->tail = 0;
}

/* Discard both a partial packet and any position saved from an old DMA run. */
void rdg_buf_reset(rdg_buf_struct* rdg_struct)
{
	if (rdg_struct == NULL)
	{
		return;
	}

	rdg_struct->tail = 0U;
	rdg_struct->dma_head = 0U;
}

// Initializes the reading buffer
rdg_buf_struct* rdg_buf_init(uint16_t size){
	rdg_buf_struct* rb = malloc(sizeof(rdg_buf_struct) + size);
	rb->buf_size = size;
	rb->tail = 0;
	rb->dma_head = 0;
	return rb;
}
