/*
 * circular_reading_buffer.h
 *
 *  Created on: Dec 9, 2025
 *      Author: k.rodolfo
 */

#ifndef INC_CIRCULAR_READING_BUFFER_H_
#define INC_CIRCULAR_READING_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

//including the variables defined in those scripts (header files)
#include <main.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// defining the type of structure. Not creating the structure, just saying it exists.
typedef struct{
	uint16_t buf_size;
	volatile uint16_t tail;
	volatile uint16_t dma_head;
	uint8_t buffer[];
}rdg_buf_struct;

rdg_buf_struct* rdg_buf_init(uint16_t size); // Initializes the reading buffer
void dma_to_rdg_buf(rdg_buf_struct* rdg_struct, uint8_t* dma_buffer, uint8_t msg_size); // After msg reception, reads the data from the DMA buffer to the reading buffer
void rdg_buf_echo(rdg_buf_struct* rdg_struct, UART_HandleTypeDef *huart); // Echoes what is currently in the reading buffer via the selected UART Channel
void flush_buffer(rdg_buf_struct* rdg_struct); // After message handling, clears the reading buffer so that it is ready for the next set of data



#ifdef __cplusplus
}
#endif


#endif /* INC_CIRCULAR_READING_BUFFER_H_ */
