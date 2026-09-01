/*
 * structs.h
 *
 *  Created on: Dec 10, 2025
 *      Author: k.rodolfo
 */

#ifndef INC_STRUCTS_H_
#define INC_STRUCTS_H_

#include "circular_reading_buffer.h"
#include "data_tx_arrays.h"
#include "stim_command_manager.h"
#include "gp8403.h"
//#include "cmd_array.h"

// Global Structs ///
// Bluetooth UART reading buffer
extern rdg_buf_struct* dma_reader;

// Stimulation Handling Structs
extern stimCommandQueue stim_queue;
extern GP8403 dac;



#endif /* INC_STRUCTS_H_ */
