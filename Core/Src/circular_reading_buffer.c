/*
 * circular_reading_buffer.c
 *
 *  Created on: Dec 9, 2025
 *      Author: k.rodolfo
 */


#include "circular_reading_buffer.h"
// This should maybe be called circular_buffer_reader. It doesn't create a circular buffer. It reads one to a linear buffer.

// After message reception interrupt, reads the data from the DMA buffer to the reading buffer
inline void dma_to_rdg_buf(rdg_buf_struct* rdg_struct, uint8_t* dma_buffer, uint8_t msg_size){
	uint8_t empty_spaces = rdg_struct->buf_size - rdg_struct->dma_head;

	// Do nothing, the message is too large. TODO transmit an error to host.
	if (rdg_struct->buf_size < msg_size){
		return;
	}

	// The message traverses the crossover of the circular buffer, split the message into two reads and places into the reading buffer
	// then update the dma_head location
	if (empty_spaces < msg_size){
		// Transfer the pre-crossover data:
		memcpy(rdg_struct->buffer, dma_buffer+rdg_struct->dma_head, empty_spaces * sizeof(dma_buffer[0]));
		uint8_t rem_to_copy = msg_size - empty_spaces;
		rdg_struct->dma_head = 0;
		rdg_struct->tail += empty_spaces;
		memcpy(rdg_struct->buffer + rdg_struct->tail,dma_buffer+rdg_struct->dma_head, rem_to_copy * sizeof(dma_buffer[0]));
		rdg_struct->dma_head += rem_to_copy;
		rdg_struct->tail += rem_to_copy;
	// Transfer the post-crossover data:
	} else {
		memcpy(rdg_struct->buffer, dma_buffer+rdg_struct->dma_head, msg_size * sizeof(dma_buffer[0]));
		rdg_struct->dma_head += msg_size;
		rdg_struct->tail += msg_size;
	}

}

// Echo the data from the rdg_buffer to the selected UART channel. Then reset/flush the reading buffer
inline void rdg_buf_echo(rdg_buf_struct* rdg_struct, UART_HandleTypeDef* huart){
	HAL_UART_Transmit(huart, (uint8_t*)rdg_struct->buffer, rdg_struct->tail, HAL_MAX_DELAY);
	//flush_buffer(rdg_struct);
}

// Clear the reading buffer following a successful process of the command
inline void flush_buffer(rdg_buf_struct* rdg_struct){
	rdg_struct->tail = 0;
}

// Initializes the reading buffer
rdg_buf_struct* rdg_buf_init(uint16_t size){
	rdg_buf_struct* rb = malloc(sizeof(rdg_buf_struct) + size);
	rb->buf_size = size;
	rb->tail = 0;
	rb->dma_head = 0;
	return rb;
}
