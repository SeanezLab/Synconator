/*
 * data_arrays.h
 *
 *  Created on: Jan 7, 2026
 *      Author: k.rodolfo
 */

#ifndef INC_DATA_TX_ARRAYS_H_
#define INC_DATA_TX_ARRAYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

// Holds the data array variable for better readability


// Synconator state data
extern uint8_t status[];
extern uint8_t queue_len[];
extern uint8_t queue_time[];
extern uint8_t debug[];
// Debugging transmits
extern uint8_t frame[];




#ifdef __cplusplus
}
#endif


#endif /* INC_DATA_TX_ARRAYS_H_ */
